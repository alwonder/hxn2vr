// xr_logic.cpp -- pure OpenXR decision helpers (no XR or GL calls).
#include "xr_logic.hpp"

#include <openxr/openxr_reflection.h>

#include <algorithm>
#include <cmath>

namespace hxvr {
namespace {

constexpr int64_t kGlRgba8 = 0x8058;
constexpr int64_t kGlSrgb8Alpha8 = 0x8C43;
constexpr int kMinEyeDim = 16;

}  // namespace

bool isDepthFormat(int64_t f) {
	switch (f) {
	case 0x81A5:   // GL_DEPTH_COMPONENT16
	case 0x81A6:   // GL_DEPTH_COMPONENT24
	case 0x81A7:   // GL_DEPTH_COMPONENT32
	case 0x8CAC:   // GL_DEPTH_COMPONENT32F
	case 0x8CAD:   // GL_DEPTH32F_STENCIL8
	case 0x88F0:   // GL_DEPTH24_STENCIL8
	case 0x8D48:   // GL_STENCIL_INDEX8
		return true;
	default:
		return false;
	}
}

FormatChoice chooseSwapchainFormat(const std::vector<int64_t>& offered) {
	const auto has = [&](int64_t f) { return std::find(offered.begin(), offered.end(), f) != offered.end(); };
	if (has(kGlRgba8)) return FormatChoice{kGlRgba8, false};
	if (has(kGlSrgb8Alpha8)) return FormatChoice{kGlSrgb8Alpha8, true};
	for (int64_t f : offered)
		if (!isDepthFormat(f)) return FormatChoice{f, false};
	return FormatChoice{0, false};
}

bool glVersionOk(int major, int minor, XrVersion minApi, XrVersion maxApi, std::string& why) {
	// Compare major.minor only: a GL context has no patch number.
	const auto mm = [](XrVersion v) { return (static_cast<uint64_t>(XR_VERSION_MAJOR(v)) << 16) | XR_VERSION_MINOR(v); };
	const uint64_t have = (static_cast<uint64_t>(major < 0 ? 0 : major) << 16) | static_cast<uint64_t>(minor < 0 ? 0 : minor);
	const auto str = [](uint64_t v) { return std::to_string(v >> 16) + "." + std::to_string(v & 0xFFFF); };
	why.clear();
	if (have < mm(minApi)) {
		why = "OpenGL " + str(have) + " is below the runtime's minimum " + str(mm(minApi));
		return false;
	}
	if (have > mm(maxApi))
		why = "OpenGL " + str(have) + " is newer than the runtime's tested maximum " + str(mm(maxApi));
	return true;
}

SessionStep onSessionState(XrSessionState s) {
	SessionStep r{false, false, false, false, false, false};
	switch (s) {
	case XR_SESSION_STATE_READY: r.beginSession = true; break;
	case XR_SESSION_STATE_SYNCHRONIZED: break;
	case XR_SESSION_STATE_VISIBLE: r.render = true; break;
	case XR_SESSION_STATE_FOCUSED: r.render = true; r.focused = true; break;
	case XR_SESSION_STATE_STOPPING: r.endSession = true; break;
	case XR_SESSION_STATE_LOSS_PENDING:
	case XR_SESSION_STATE_EXITING: r.lost = true; break;
	case XR_SESSION_STATE_IDLE:
	default: r.sleep = true; break;   // IDLE, UNKNOWN
	}
	return r;
}

int scaledEyeDim(uint32_t recommended, float scale, uint32_t maxDim) {
	double v = static_cast<double>(recommended);
	if (std::isfinite(scale) && scale > 0.0f) v = std::round(v * static_cast<double>(scale));
	const double hi = maxDim >= static_cast<uint32_t>(kMinEyeDim) ? static_cast<double>(maxDim) : 1e9;
	v = std::clamp(v, static_cast<double>(kMinEyeDim), hi);
	return static_cast<int>(v);
}

bool poseFlagsValid(uint64_t flags) {
	constexpr uint64_t need = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	static_assert(XR_SPACE_LOCATION_ORIENTATION_VALID_BIT == XR_VIEW_STATE_ORIENTATION_VALID_BIT &&
	              XR_SPACE_LOCATION_POSITION_VALID_BIT == XR_VIEW_STATE_POSITION_VALID_BIT);
	return (flags & need) == need;
}

XrEnvironmentBlendMode chooseBlendMode(const std::vector<XrEnvironmentBlendMode>& offered) {
	if (offered.empty()) return XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM;
	for (XrEnvironmentBlendMode m : offered)
		if (m == XR_ENVIRONMENT_BLEND_MODE_OPAQUE) return m;
	return offered.front();
}

bool parseGlVersion(const char* version, int& major, int& minor) {
	if (!version) return false;
	const auto digit = [](char c) { return c >= '0' && c <= '9'; };
	const char* p = version;
	if (!digit(*p)) return false;
	int ma = 0, mi = 0;
	while (digit(*p)) ma = ma * 10 + (*p++ - '0');
	if (*p++ != '.' || !digit(*p)) return false;
	while (digit(*p)) mi = mi * 10 + (*p++ - '0');
	major = ma;
	minor = mi;
	return true;
}

std::string xrResultName(XrResult r) {
	switch (r) {
#define HXVR_XR_RESULT_CASE(name, value) case name: return #name;
	XR_LIST_ENUM_XrResult(HXVR_XR_RESULT_CASE)
#undef HXVR_XR_RESULT_CASE
	default: return "XR_RESULT_" + std::to_string(static_cast<int>(r));
	}
}

}  // namespace hxvr
