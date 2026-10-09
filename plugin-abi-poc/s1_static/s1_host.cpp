// S1 host probe. Answers two questions per plugin, separately:
//
//   1. Does QPluginLoader see it?   (the path that a static Qt breaks)
//   2. Does dlopen + dlsym see it?  (the path the plugin system will use)
//
// It deliberately does NOT use QLibrary, so the second answer does not
// depend on any Qt code at all. Prints one machine-readable line per probe.

#include <QtCore/QCoreApplication>
#include <QtCore/QPluginLoader>
#include <QtCore/QString>

#include <cstdio>
#include <dlfcn.h>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool ok, const char *what, const char *detail) {
	++gChecks;
	if (!ok) {
		++gFailures;
	}
	std::printf("  %s  %-46s %s\n",
		ok ? "PASS" : "FAIL",
		what,
		detail ? detail : "");
}

void Probe(const QString &path, bool expectQtLoadable) {
	std::printf("\n-- %s --\n", path.toUtf8().constData());

	// 1. QPluginLoader: the classic Qt path.
	QPluginLoader loader(path);
	const bool qtLoaded = loader.load();
	const auto qtError = loader.errorString().toUtf8();
	std::printf("        QPluginLoader::load() = %s%s%s\n",
		qtLoaded ? "true" : "false",
		qtError.isEmpty() ? "" : " -> ",
		qtError.isEmpty() ? "" : qtError.constData());
	check(qtLoaded == expectQtLoadable,
		expectQtLoadable
			? "QPluginLoader loads it (shared-Qt build)"
			: "QPluginLoader rejects it (static-Qt build)",
		qtError.constData());

	// 2. dlopen + dlsym: the path the plugin system will use.
	::dlerror();
	void *handle = ::dlopen(path.toUtf8().constData(), RTLD_NOW | RTLD_LOCAL);
	if (!handle) {
		check(false, "dlopen succeeds", ::dlerror());
		return;
	}
	check(true, "dlopen succeeds", nullptr);

	const auto ping = reinterpret_cast<int (*)()>(
		::dlsym(handle, "s1_c_abi_ping"));
	check(ping != nullptr, "dlsym finds the C ABI symbol", ::dlerror());

	// 3. The Qt entry point, resolved by name rather than by metadata.
	::dlerror();
	const void *instance = ::dlsym(handle, "qt_plugin_instance");
	check((instance != nullptr) == expectQtLoadable,
		expectQtLoadable
			? "extern \"C\" qt_plugin_instance is exported"
			: "extern \"C\" qt_plugin_instance is absent",
		instance ? nullptr : "absent");

	if (ping) {
		const int value = ping();
		char buf[32];
		std::snprintf(buf, sizeof buf, "returned %d", value);
		check(value == 4242, "C ABI call returns the expected value", buf);
	}

	::dlclose(handle);
}

} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	if (argc < 3) {
		std::fprintf(stderr, "usage: s1_host <qt-loadable|qt-rejected> <plugin.so>...\n");
		return 2;
	}
	const auto mode = QString::fromLocal8Bit(argv[1]);
	const bool expectQtLoadable = (mode == "qt-loadable");
	std::printf("expectation: Qt plugin path %s\n",
		expectQtLoadable ? "WORKS" : "BROKEN");
	for (int i = 2; i < argc; ++i) {
		Probe(QString::fromLocal8Bit(argv[i]), expectQtLoadable);
	}
	std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
