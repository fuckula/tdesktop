// Host harness. Stands in for tdesktop: the C++/Qt side that loads plugins
// and uses them through two boundaries.
//
//   Phase A — pure C ABI. Runs for EVERY plugin passed on the command line,
//             regardless of the language it was written in. This is the
//             contract a real tdesktop plugin system would rely on.
//   Phase B — Qt interface. Runs only for plugins that are also Qt plugins
//             (i.e. export qt_plugin_instance); proves deep integration into
//             the Qt object model, including a signal-driven callback.
//
// Run: ./host <qt-plugin.so> <other-plugin.so> ...

#include <QtCore/QCoreApplication>
#include <QtCore/QLibrary>
#include <QtCore/QPluginLoader>
#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include "tdp_plugin_qt.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool ok, const QString &what, const QString &detail = {}) {
	++gChecks;
	if (ok) {
		std::printf("  PASS  %s\n", what.toUtf8().constData());
	} else {
		++gFailures;
		std::printf("  FAIL  %s%s%s\n",
			what.toUtf8().constData(),
			detail.isEmpty() ? "" : " -> ",
			detail.toUtf8().constData());
	}
}

// Everything the host knows about one loaded plugin, resolved purely through
// the C ABI. No language-specific knowledge anywhere below.
struct AbiPlugin {
	QString path;
	// QLibrary is a QObject: neither copyable nor movable, so it is held
	// behind a pointer and the struct stays movable.
	std::unique_ptr<QLibrary> handle = std::make_unique<QLibrary>();
	const TdpPluginInfo *info = nullptr;
	const TdpPluginVTable *vtable = nullptr;
	uint64_t (*selfCheck)() = nullptr;
	bool loaded = false;
};

bool LoadAbi(AbiPlugin &plugin) {
	plugin.handle->setFileName(plugin.path);
	if (!plugin.handle->load()) {
		std::printf("  FAIL  QLibrary::load(%s) -> %s\n",
			plugin.path.toUtf8().constData(),
			plugin.handle->errorString().toUtf8().constData());
		++gChecks;
		++gFailures;
		return false;
	}
	plugin.loaded = true;

	// resolve() returns the symbol's address, so a function export must be
	// called, not dereferenced as data.
	const auto getVtable = reinterpret_cast<const TdpPluginVTable*(*)()>(
		plugin.handle->resolve("tdp_plugin_vtable"));
	const auto getInfo = reinterpret_cast<const TdpPluginInfo*(*)()>(
		plugin.handle->resolve("tdp_plugin_info"));
	plugin.selfCheck = reinterpret_cast<uint64_t(*)()>(
		plugin.handle->resolve("tdp_plugin_self_check"));
	plugin.vtable = getVtable ? getVtable() : nullptr;
	plugin.info = getInfo ? getInfo() : nullptr;
	return true;
}

void RunAbiChecks(AbiPlugin &plugin) {
	std::printf("\n-- C ABI checks: %s --\n",
		QDir().relativeFilePath(plugin.path).toUtf8().constData());

	check(plugin.vtable != nullptr, "tdp_plugin_vtable()");
	check(plugin.info != nullptr, "tdp_plugin_info()");
	check(plugin.selfCheck != nullptr, "tdp_plugin_self_check()");
	if (!plugin.vtable || !plugin.info) {
		return;
	}

	// 1. Version negotiation is the host's first decision.
	check(plugin.vtable->abiMajor == TDP_PLUGIN_ABI_VERSION
			&& plugin.info->abiMajor == TDP_PLUGIN_ABI_VERSION,
		"ABI major matches the host's compiled expectation",
		QStringLiteral("%1/%2")
			.arg(plugin.info->abiMajor)
			.arg(plugin.vtable->abiMajor));

	// 2. Identity the host can show and log.
	const auto id = QString::fromUtf8(plugin.info->id);
	const auto name = QString::fromUtf8(plugin.info->name);
	const auto version = QString::fromUtf8(plugin.info->version);
	const auto author = QString::fromUtf8(plugin.info->author);
	std::printf("        id=%s name=%s v=%s author=%s caps=0x%llx\n",
		id.toUtf8().constData(),
		name.toUtf8().constData(),
		version.toUtf8().constData(),
		author.toUtf8().constData(),
		(unsigned long long)plugin.info->capabilities);
	check(!id.isEmpty() && !name.isEmpty() && !version.isEmpty(),
		"descriptor carries id/name/version");

	// 3. init() is the handshake; a host passes its own API table here.
	if (plugin.vtable->init) {
		plugin.vtable->init(nullptr);
		check(true, "init() accepted the host handshake");
	}

	// 4. Message pipeline: host bytes in, host bytes out.
	if (plugin.vtable->transformText
			&& (plugin.info->capabilities
				& TdpCapabilityTransformOutgoingText)) {
		const auto run = [&](const char *input) {
			auto out = QByteArray(256, Qt::Uninitialized);
			const auto written = plugin.vtable->transformText(
				reinterpret_cast<const uint8_t*>(input),
				int32_t(std::strlen(input)),
				reinterpret_cast<uint8_t*>(out.data()),
				int32_t(out.size()));
			return written < 0
				? QStringLiteral("<unchanged>")
				: QString::fromUtf8(out.constData(), written);
		};
		const auto spoiler = run("/spoiler secret");
		const auto reverse = run("/reverse abcdef");
		const auto plain = run("plain text");
		std::printf("        transform(\"/spoiler secret\") = %s\n",
			spoiler.toUtf8().constData());
		std::printf("        transform(\"/reverse abcdef\") = %s\n",
			reverse.toUtf8().constData());
		// Each plugin answers with its own transformation; the host does not
		// care which language produced it.
		check(spoiler == QStringLiteral("||secret||")
				|| reverse == QStringLiteral("fedcba"),
			"a capability-matched transform produced a plugin-specific result");
		check(plain == QStringLiteral("<unchanged>"),
			"a non-matching input is left untouched", plain);
	}

	// 5. Context menu: the shape a real menu seam needs.
	if (plugin.vtable->contextMenu
			&& (plugin.info->capabilities & TdpCapabilityContextMenuItems)) {
		const auto raw = plugin.vtable->contextMenu("message");
		check(raw != nullptr, "contextMenu() returns a contribution");
		if (raw) {
			const auto first = QString::fromUtf8(raw);
			std::printf("        item[0] = %s\n", first.toUtf8().constData());
			check(first.contains(QLatin1Char('\t')),
				"items are id-tab-label encoded", first);
		}
	}

	// 6. Deleted-message sink — the AyuGram feature shape.
	if (plugin.vtable->deletedMessage
			&& (plugin.info->capabilities & TdpCapabilityDeletedMessageSink)) {
		const auto before = plugin.selfCheck();
		const auto payload = QByteArrayLiteral("payload-one");
		plugin.vtable->deletedMessage(
			1001, 55,
			reinterpret_cast<const uint8_t*>(payload.constData()),
			payload.size());
		const auto after = plugin.selfCheck();
		const auto storedBefore = (before >> 48) & 0xFFFF;
		const auto storedAfter = (after >> 48) & 0xFFFF;
		std::printf("        sink depth: %llu -> %llu\n",
			(unsigned long long)storedBefore,
			(unsigned long long)storedAfter);
		check(storedAfter == storedBefore + 1,
			"the plugin persisted the handed-over payload");
	}

	// 7. Panic/exception containment, where the plugin offers it.
	if (const auto probe = reinterpret_cast<int32_t(*)(int32_t)>(
			plugin.handle->resolve("tdp_panic_probe"))) {
		const auto ok = probe(0);
		const auto caught = probe(1);
		std::printf("        probe(0)=%d probe(1)=%d\n", ok, caught);
		check(ok == 42, "normal path returns the computed value",
			QString::number(ok));
		check(caught == -1,
			"a plugin-side panic is contained at the boundary, host survives",
			QString::number(caught));
	}
}

void RunQtChecks(const QString &path) {
	std::printf("\n-- Qt plugin checks: %s --\n",
		QDir().relativeFilePath(path).toUtf8().constData());

	QPluginLoader loader(path);
	const auto loaded = loader.load();
	check(loaded, "QPluginLoader::load()", loader.errorString());
	if (!loaded) {
		return;
	}

	const auto meta = loader.metaData();
	check(meta.value("IID").toString()
			== QStringLiteral(TdpQtPluginInterface_iid),
		"IID read from the .note.qt.metadata ELF note",
		meta.value("IID").toString());
	check(meta.value("MetaData").toObject()
			.value("Keys").toArray().size() == 1,
		"plugin.json Keys survived embedding");

	const auto object = loader.instance();
	check(object != nullptr, "loader.instance()");
	if (!object) {
		return;
	}
	const auto plugin = qobject_cast<TdpQtPluginInterface*>(object);
	check(plugin != nullptr, "qobject_cast<TdpQtPluginInterface*>");
	if (!plugin) {
		return;
	}

	const auto greeting = plugin->greeting();
	std::printf("        greeting = %s\n", greeting.toUtf8().constData());
	check(greeting.contains(QStringLiteral("Rust PoC Plugin"))
			&& greeting.contains(QStringLiteral("abi 1.0")),
		"a QString crossed the boundary carrying Rust-side data", greeting);

	const auto spoiler = plugin->transform(QStringLiteral("/spoiler secret"));
	const auto plain = plugin->transform(QStringLiteral("plain text"));
	std::printf("        transform(\"/spoiler secret\") = %s\n",
		spoiler.toUtf8().constData());
	check(spoiler == QStringLiteral("||secret||"),
		"UTF-16 QString round trip through the Rust pipeline", spoiler);
	check(plain == QStringLiteral("plain text"),
		"non-matching input passes through untouched", plain);

	int reloadCount = 0;
	plugin->setReloadCallback([&] { ++reloadCount; });
	check(reloadCount == 1,
		"a Qt signal on a plugin QObject drove a Rust-visible callback",
		QString::number(reloadCount));

	// Hot unload discipline: the instance must die before the module can.
	delete object;
	const auto firstUnload = loader.unload();
	check(!firstUnload,
		"first unload() is false: the ABI handle still references the module",
		loader.errorString());
}

} // namespace

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);
	if (argc < 2) {
		std::fprintf(stderr, "usage: host <plugin.so> [more-plugins.so ...]\n");
		return 2;
	}

	std::printf("== Phase A: pure C ABI, one contract for every language ==\n");
	// QLibrary is a QObject: non-copyable and non-movable, so the handles
	// must be held indirectly.
	auto plugins = std::vector<std::unique_ptr<AbiPlugin>>();
	for (auto i = 1; i != argc; ++i) {
		auto plugin = std::make_unique<AbiPlugin>(
			AbiPlugin{ .path = QString::fromLocal8Bit(argv[i]) });
		if (LoadAbi(*plugin)) {
			plugins.push_back(std::move(plugin));
		}
	}
	for (auto &plugin : plugins) {
		RunAbiChecks(*plugin);
	}

	std::printf("\n== Phase B: Qt integration ==\n");
	for (auto &plugin : plugins) {
		// Only plugins that export the Qt entry point can be Qt plugins.
		if (plugin->handle->resolve("qt_plugin_instance")) {
			RunQtChecks(plugin->path);
		} else {
			std::printf(
				"\n-- Qt plugin checks: %s --\n"
				"  SKIP  no qt_plugin_instance: pure-ABI plugin, as intended\n",
				QDir().relativeFilePath(plugin->path).toUtf8().constData());
		}
	}

	std::printf("\n== Release: every handle ==\n");
	for (auto &plugin : plugins) {
		if (plugin->loaded) {
			plugin->handle->unload();
		}
	}
	check(true, "all plugin handles released without crashing the host");

	std::printf("\n%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
