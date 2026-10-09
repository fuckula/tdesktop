// S1 probe: the exact thing tdesktop's release build makes impossible.
//
// A classic Qt plugin: Q_OBJECT + Q_PLUGIN_METADATA + Q_INTERFACES. Built
// twice, identically, except for -DQT_STATICPLUGIN. That define is what a
// static Qt puts in its own qtcore-config.h, so a plugin compiled against a
// static Qt gets it whether it wants to or not.
//
// It also exports one plain C symbol, so the same .so can be loaded through
// the C ABI path that the real plugin system will use.

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QtPlugin>

class S1Interface {
public:
	virtual ~S1Interface() = default;
	virtual QString greet() const = 0;
};

#define S1Interface_iid "org.tdesktop.rust.research.S1Interface/1.0"
Q_DECLARE_INTERFACE(S1Interface, S1Interface_iid)

class S1Plugin : public QObject, public S1Interface {
	Q_OBJECT
	Q_PLUGIN_METADATA(IID S1Interface_iid)
	Q_INTERFACES(S1Interface)

public:
	QString greet() const override {
		return QStringLiteral("hello from the Qt plugin path");
	}
};

#include "s1_plugin.moc"

// The C ABI surface. Independent of everything Qt above.
extern "C" __attribute__((visibility("default"))) int s1_c_abi_ping(void) {
	return 4242;
}
