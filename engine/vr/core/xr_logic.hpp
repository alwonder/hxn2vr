// xr_logic.hpp -- pure OpenXR decision helpers (no XR or GL calls), unit-tested.
// Module-internal; used by backend_openxr.cpp.
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <string>
#include <vector>

namespace hxvr {

// Swapchain colour format (ruling R20). GL_SRGB8_ALPHA8 (0x8C43) > GL_RGBA8 (0x8058) > the first
// offered colour format. Depth/stencil formats are never chosen. format 0 = no colour format offered.
// gammaPassthrough: the engine's gamma-encoded output is stored as-is in an sRGB image (the
// compositor decodes it), so GL_FRAMEBUFFER_SRGB must stay disabled (EyeTargets ensures it). Without
// it (UNORM image) the runtime treats the values as linear and the picture may look washed out.
struct FormatChoice { int64_t format; bool gammaPassthrough; };
FormatChoice chooseSwapchainFormat(const std::vector<int64_t>& offered);

// True if GL depth/stencil internal format (0x81A5..7, 0x8CAC, 0x8CAD, 0x88F0, 0x8D48).
bool isDepthFormat(int64_t format);

// Does the context's GL major.minor satisfy the runtime's [minApi, maxApi]? Below the minimum:
// false, `why` names both versions. Above the tested maximum: true (the spec allows newer
// compatible versions), `why` holds a warning. Otherwise true and `why` is cleared.
bool glVersionOk(int major, int minor, XrVersion minApi, XrVersion maxApi, std::string& why);

// What the backend does for a session state (on its change event, and per frame).
//  READY -> beginSession; SYNCHRONIZED -> running, no rendering; VISIBLE -> render, not focused;
//  FOCUSED -> render, focused; STOPPING -> endSession; LOSS_PENDING / EXITING -> lost;
//  IDLE / UNKNOWN -> sleep (beginFrame sleeps ~10 ms and does not call xrWaitFrame).
struct SessionStep { bool beginSession; bool endSession; bool lost; bool render; bool focused; bool sleep; };
SessionStep onSessionState(XrSessionState s);

// Per-eye swapchain dimension: round(recommended * scale), clamped to [16, maxDim]
// (maxDim < 16 or a non-positive / non-finite scale -> the recommended size, clamped).
int scaledEyeDim(uint32_t recommended, float scale, uint32_t maxDim);

// A located pose is usable only with both ORIENTATION_VALID and POSITION_VALID set
// (tracked or not). Works for XrSpaceLocationFlags and XrViewStateFlags (same bit values).
bool poseFlagsValid(uint64_t flags);

// What to do after xrWaitSwapchainImage returned `r` on attempt `attempt` (1-based) of
// `maxAttempts`. Ready: the image was waited (any success code other than XR_TIMEOUT_EXPIRED).
// Retry: XR_TIMEOUT_EXPIRED with attempts left. Lost: still timing out after the last attempt --
// the image is acquired but not waited, so it must NOT be released (CALL_ORDER_INVALID); the
// swapchain is out of step and the session must be restarted. Release: a genuine failure; give
// the acquired image back.
enum class SwapchainWait { Ready, Retry, Release, Lost };
SwapchainWait swapchainWaitAction(XrResult r, int attempt, int maxAttempts);

// OPAQUE if offered, else the first offered mode; XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM if none.
XrEnvironmentBlendMode chooseBlendMode(const std::vector<XrEnvironmentBlendMode>& offered);

// Parses "major.minor..." from the start of a GL_VERSION string (desktop GL only).
bool parseGlVersion(const char* version, int& major, int& minor);

// Name of an XrResult without an instance (fallback for xrResultToString), e.g.
// "XR_ERROR_FORM_FACTOR_UNAVAILABLE"; unknown codes give "XR_RESULT_<n>".
std::string xrResultName(XrResult r);

}  // namespace hxvr
