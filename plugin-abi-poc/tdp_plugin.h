// Host-side plugin contract for the tdesktop-rust PoC.
//
// This header is the PURE C ABI and deliberately includes no Qt. It is the
// stable surface a plugin implements; it survives Qt upgrades, compiler
// changes and the Rust/C++ split. The Qt-facing half lives in tdp_plugin_qt.h
// and is optional — a plugin that only needs the stable ABI (like
// cppplugin/main.cpp) includes this file alone.
#pragma once

#include <cstdint>

#define TDP_PLUGIN_ABI_VERSION 1

extern "C" {

// Version negotiation. Host refuses the plugin if the major differs.
struct TdpPluginInfo {
	uint32_t abiMajor;
	uint32_t abiMinor;
	const char *id;
	const char *name;
	const char *version;
	const char *author;
	// Bitmask of TdpCapability values the plugin wants the host to call.
	uint64_t capabilities;
};

enum TdpCapability : uint64_t {
	TdpCapabilityNone = 0,
	TdpCapabilityTransformOutgoingText = 1u << 0,
	TdpCapabilityContextMenuItems = 1u << 1,
	TdpCapabilityDeletedMessageSink = 1u << 2,
};

// Called once after the host accepted the plugin.
// `hostApi` is a host-provided vtable; NULL-tolerant by contract.
typedef void (*TdpInitFn)(const void *hostApi);

// Message pipeline hook. The host owns both buffers; the plugin writes into
// `out` (capacity `outCapacity`, including NUL) and returns the used length
// excluding the NUL, or a negative value to mean "leave unchanged".
typedef int32_t (*TdpTransformTextFn)(
	const uint8_t *textUtf8,
	int32_t textLen,
	uint8_t *out,
	int32_t outCapacity);

// Context-menu contribution. The host calls this with a menu kind id and
// receives a NUL-separated list of "id\tlabel" entries, or NULL for none.
typedef const char *(*TdpContextMenuFn)(const char *menuKind);

// Deleted-message sink (the AyuGram "deleted messages" feature shape).
// The host hands over serialized bytes; the plugin persists them.
typedef void (*TdpDeletedMessageFn)(
	int64_t dialogId,
	int64_t messageId,
	const uint8_t *payload,
	int32_t payloadLen);

struct TdpPluginVTable {
	uint32_t abiMajor;
	uint32_t abiMinor;
	TdpInitFn init;
	TdpTransformTextFn transformText;
	TdpContextMenuFn contextMenu;
	TdpDeletedMessageFn deletedMessage;
};

// Exported by every plugin. Stable name, stable signature.
const TdpPluginInfo *tdp_plugin_info(void);
const TdpPluginVTable *tdp_plugin_vtable(void);
// Optional: lets the host verify it loaded the plugin it thinks it did.
uint64_t tdp_plugin_self_check(void);

} // extern "C"
