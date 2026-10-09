// The Qt half of the Rust plugin. This is what makes the .so loadable by
// QPluginLoader: it produces `qt_plugin_instance` plus the ELF note
// `.note.qt.metadata` that Qt 6.3+ looks for (see
// /usr/include/qt6/QtCore/qplugin.h:37-260).
//
// Note the deliberate split: the Qt QObject is built in C++ (moc needs C++),
// while all logic lives in Rust. That is the practical pattern for a real
// plugin system, because moc cannot process Rust.

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QtPlugin>

#include <functional>

#include "tdp_plugin_qt.h"

// ---- Rust side ----------------------------------------------------------

extern "C" {
// Implemented in Rust (src/lib.rs).
const TdpPluginInfo *tdp_plugin_info(void);
const TdpPluginVTable *tdp_plugin_vtable(void);
uint64_t tdp_plugin_self_check(void);
}

// Implemented here, called from Rust.
QString tdp_qt_greeting();
QString tdp_qt_transform(const QString &text);
uint64_t tdp_qt_self_check();

// ---- the QObject --------------------------------------------------------

class TdpRustPluginObject final
	: public QObject
	, public TdpQtPluginInterface {
	Q_OBJECT
	Q_PLUGIN_METADATA(IID TdpQtPluginInterface_iid FILE "plugin.json")
	Q_INTERFACES(TdpQtPluginInterface)

public:
	explicit TdpRustPluginObject(QObject *parent = nullptr);
	~TdpRustPluginObject() override;

	QString greeting() const override;
	QString transform(const QString &text) const override;
	void setReloadCallback(std::function<void()> callback) override;

signals:
	void reloadRequested();

private:
	std::function<void()> _reloadCallback;
	QMetaObject::Connection _reloadConnection;
};

TdpRustPluginObject::TdpRustPluginObject(QObject *parent) : QObject(parent) {
	// Hand the Rust side the vtable once, exactly like the host would.
	if (const auto vtable = tdp_plugin_vtable()) {
		if (vtable->init) {
			vtable->init(nullptr);
		}
	}
}

TdpRustPluginObject::~TdpRustPluginObject() = default;

QString TdpRustPluginObject::greeting() const {
	const auto info = tdp_plugin_info();
	const auto name = info ? QString::fromUtf8(info->name) : QString();
	const auto version = info ? QString::fromUtf8(info->version) : QString();
	return QStringLiteral("Rust plugin '%1' v%2, abi %3.%4").arg(
		name,
		version,
		QString::number(info ? info->abiMajor : 0),
		QString::number(info ? info->abiMinor : 0));
}

QString TdpRustPluginObject::transform(const QString &text) const {
	const auto vtable = tdp_plugin_vtable();
	if (!vtable || !vtable->transformText) {
		return text;
	}
	const auto utf8 = text.toUtf8();
	QByteArray out(utf8.size() * 4 + 64, Qt::Uninitialized);
	const auto written = vtable->transformText(
		reinterpret_cast<const uint8_t*>(utf8.constData()),
		utf8.size(),
		reinterpret_cast<uint8_t*>(out.data()),
		out.size());
	if (written < 0) {
		return text;
	}
	return QString::fromUtf8(out.constData(), written);
}

void TdpRustPluginObject::setReloadCallback(std::function<void()> callback) {
	_reloadCallback = std::move(callback);
	// Prove the Qt signal system works from a plugin-owned QObject.
	// Note: Qt::UniqueConnection is NOT supported by the functor-based
	// connect() overload - passing it makes the connection silently fail.
	// A plugin host must guard against duplicate connections itself.
	_reloadConnection = connect(
		this,
		&TdpRustPluginObject::reloadRequested,
		this,
		[this] { if (_reloadCallback) { _reloadCallback(); } });
	emit reloadRequested();
}

QString tdp_qt_greeting() {
	return QStringLiteral("hello from Qt, driven by Rust");
}

QString tdp_qt_transform(const QString &text) {
	return text.toUpper();
}

uint64_t tdp_qt_self_check() {
	return tdp_plugin_self_check();
}

#include "qt_shim.moc"
