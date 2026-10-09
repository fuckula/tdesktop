// Qt layer of the plugin contract.
//
// Deliberately a SEPARATE header from tdp_plugin.h: the pure C ABI must stay
// Qt-free, so a plugin that only wants the stable ABI never has to include Qt
// at all (see cppplugin/main.cpp, which includes only tdp_plugin.h).
//
// This layer exists to prove the deeper integration story: a plugin that also
// wants to hand the host real Qt objects (a QObject with signals, QString
// payloads) can do so through QPluginLoader, while the language-neutral ABI
// stays the primary contract.
#pragma once

#include "tdp_plugin.h"

#include <QtCore/QObject>
#include <QtCore/QString>

#include <functional>

// Implemented by the plugin, consumed by the host through qobject_cast.
class TdpQtPluginInterface {
public:
	virtual ~TdpQtPluginInterface() = default;

	// Human-facing summary, used to prove a real QObject crossed the
	// boundary and not just a C struct.
	virtual QString greeting() const = 0;
	// Runs the plugin's text pipeline and returns the result as a QString.
	virtual QString transform(const QString &text) const = 0;

	// A callback the host registers; the plugin drives it from a Qt signal.
	// This is what "deep UI integration" requires: the plugin owns a QObject
	// whose signals the host can connect to.
	virtual void setReloadCallback(std::function<void()> callback) = 0;
};

#define TdpQtPluginInterface_iid "org.tdesktop.rust.poc.TdpQtPluginInterface/1.0"
Q_DECLARE_INTERFACE(TdpQtPluginInterface, TdpQtPluginInterface_iid)
