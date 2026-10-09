// A C++ plugin implementing the exact same ABI as the Rust one.
//
// Purpose: prove the boundary is language-agnostic. If the same header, the
// same vtable and the same host work for a Rust cdylib and a C++ shared
// library, then the ABI — not the language — is the contract, and a plugin
// system can host both without special cases.

#include "tdp_plugin.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool gInitialized = false;
std::vector<std::string> gDeleted;

std::string Transform(const std::string &input) {
	// Deliberately a different transformation than the Rust plugin, so the
	// host can tell which plugin answered.
	constexpr auto kPrefix = "/reverse ";
	if (input.rfind(kPrefix, 0) != 0) {
		return {};
	}
	auto rest = input.substr(std::strlen(kPrefix));
	std::reverse(rest.begin(), rest.end());
	return rest;
}

void Init(const void *) {
	gInitialized = true;
	gDeleted.clear();
}

int32_t TransformText(
		const uint8_t *text,
		int32_t textLen,
		uint8_t *out,
		int32_t outCapacity) {
	if (!text || !out || textLen < 0 || outCapacity <= 0) {
		return -1;
	}
	const auto input = std::string(
		reinterpret_cast<const char*>(text),
		size_t(textLen));
	const auto result = Transform(input);
	if (result.empty()) {
		return -1;
	}
	if (int32_t(result.size()) + 1 > outCapacity) {
		return -2;
	}
	std::memcpy(out, result.data(), result.size());
	out[result.size()] = 0;
	return int32_t(result.size());
}

const char *ContextMenu(const char *) {
	return "cpp.reverse\tReverse text\0";
}

void DeletedMessage(int64_t dialogId, int64_t messageId, const uint8_t *payload, int32_t len) {
	if (!payload || len < 0) {
		return;
	}
	gDeleted.push_back(std::string(
		reinterpret_cast<const char*>(payload),
		size_t(len)));
	(void)dialogId;
	(void)messageId;
}

const TdpPluginInfo kInfo = {
	TDP_PLUGIN_ABI_VERSION,
	0,
	"dev.example.cpp-poc",
	"C++ PoC Plugin",
	"0.1.0",
	"tdesktop-rust-research",
	uint64_t(TdpCapabilityTransformOutgoingText) | uint64_t(TdpCapabilityContextMenuItems),
};

const TdpPluginVTable kVTable = {
	TDP_PLUGIN_ABI_VERSION,
	0,
	&Init,
	&TransformText,
	&ContextMenu,
	&DeletedMessage,
};

} // namespace

extern "C" {

const TdpPluginInfo *tdp_plugin_info(void) {
	return &kInfo;
}

const TdpPluginVTable *tdp_plugin_vtable(void) {
	return &kVTable;
}

// Encodes the same observable state shape as the Rust plugin.
uint64_t tdp_plugin_self_check(void) {
	uint64_t initialized = gInitialized ? 1 : 0;
	uint64_t deleted = gDeleted.size();
	return initialized | (deleted << 24) | (deleted << 48);
}

} // extern "C"
