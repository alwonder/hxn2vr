#include "doctest.h"
#include "xr_logic.hpp"

#include <string>
#include <vector>

using hxvr::chooseSwapchainFormat;
using hxvr::FormatChoice;
using hxvr::SessionStep;

TEST_CASE("swapchain format: RGBA8 > SRGB8_ALPHA8 (passthrough) > first colour format") {
	FormatChoice c = chooseSwapchainFormat({0x8C43, 0x8058});
	CHECK(c.format == 0x8058);
	CHECK_FALSE(c.gammaPassthrough);

	c = chooseSwapchainFormat({0x8C43});
	CHECK(c.format == 0x8C43);
	CHECK(c.gammaPassthrough);

	c = chooseSwapchainFormat({0x881A});
	CHECK(c.format == 0x881A);
	CHECK_FALSE(c.gammaPassthrough);

	// the list the VDXR runtime offered in the Phase-0 spike
	c = chooseSwapchainFormat({0x881A, 0x8C43, 0x8058, 0x8CAC, 0x8CAD, 0x88F0, 0x81A5});
	CHECK(c.format == 0x8058);
}

TEST_CASE("swapchain format: depth formats are never a colour fallback") {
	FormatChoice c = chooseSwapchainFormat({0x81A5, 0x8CAC, 0x881A});
	CHECK(c.format == 0x881A);
	c = chooseSwapchainFormat({0x81A5, 0x81A6, 0x81A7, 0x8CAC, 0x8CAD, 0x88F0, 0x8D48});
	CHECK(c.format == 0);
	c = chooseSwapchainFormat({});
	CHECK(c.format == 0);
	CHECK(hxvr::isDepthFormat(0x88F0));
	CHECK_FALSE(hxvr::isDepthFormat(0x8058));
}

TEST_CASE("GL version against the runtime requirements") {
	const XrVersion min40 = XR_MAKE_VERSION(4, 0, 0);
	const XrVersion max50 = XR_MAKE_VERSION(5, 0, 0);
	std::string why = "stale";
	CHECK(hxvr::glVersionOk(4, 6, min40, max50, why));
	CHECK(why.empty());

	CHECK_FALSE(hxvr::glVersionOk(3, 3, min40, max50, why));
	CHECK(why.find("4.0") != std::string::npos);
	CHECK(why.find("3.3") != std::string::npos);

	CHECK(hxvr::glVersionOk(4, 0, min40, max50, why));   // exactly the minimum
	// newer than the tested maximum: allowed, with a warning
	CHECK(hxvr::glVersionOk(4, 6, min40, XR_MAKE_VERSION(4, 5, 0), why));
	CHECK(why.find("4.5") != std::string::npos);
	// patch numbers of the requirement are ignored (the context only has major.minor)
	CHECK(hxvr::glVersionOk(4, 6, XR_MAKE_VERSION(4, 6, 3), max50, why));
}

namespace {
bool same(const SessionStep& a, const SessionStep& b) {
	return a.beginSession == b.beginSession && a.endSession == b.endSession && a.lost == b.lost &&
	       a.render == b.render && a.focused == b.focused && a.sleep == b.sleep;
}
}  // namespace

TEST_CASE("session state table") {
	//                                 begin  end    lost   render focused sleep
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_IDLE),         {false, false, false, false, false, true}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_READY),        {true,  false, false, false, false, false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_SYNCHRONIZED), {false, false, false, false, false, false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_VISIBLE),      {false, false, false, true,  false, false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_FOCUSED),      {false, false, false, true,  true,  false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_STOPPING),     {false, true,  false, false, false, false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_LOSS_PENDING), {false, false, true,  false, false, false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_EXITING),      {false, false, true,  false, false, false}));
	CHECK(same(hxvr::onSessionState(XR_SESSION_STATE_UNKNOWN),      {false, false, false, false, false, true}));
}

TEST_CASE("eye swapchain size: recommended x scale, rounded, clamped to [16, max]") {
	CHECK(hxvr::scaledEyeDim(3072, 1.0f, 16384) == 3072);
	CHECK(hxvr::scaledEyeDim(3072, 0.5f, 16384) == 1536);
	CHECK(hxvr::scaledEyeDim(1001, 0.5f, 16384) == 501);   // 500.5 rounds away from zero
	CHECK(hxvr::scaledEyeDim(3072, 2.0f, 4096) == 4096);
	CHECK(hxvr::scaledEyeDim(100, 0.01f, 4096) == 16);
	CHECK(hxvr::scaledEyeDim(3072, 0.0f, 4096) == 3072);
	CHECK(hxvr::scaledEyeDim(3072, -1.0f, 4096) == 3072);
}

TEST_CASE("pose validity needs both orientation and position valid bits") {
	const uint64_t o = XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	const uint64_t p = XR_SPACE_LOCATION_POSITION_VALID_BIT;
	const uint64_t pt = XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
	CHECK(hxvr::poseFlagsValid(o | p));
	CHECK(hxvr::poseFlagsValid(o | p | pt));
	CHECK_FALSE(hxvr::poseFlagsValid(o));
	CHECK_FALSE(hxvr::poseFlagsValid(p | pt));
	CHECK_FALSE(hxvr::poseFlagsValid(0));
	CHECK(hxvr::poseFlagsValid(XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT));
}

TEST_CASE("XrResult names without an instance") {
	CHECK(hxvr::xrResultName(XR_ERROR_FORM_FACTOR_UNAVAILABLE) == "XR_ERROR_FORM_FACTOR_UNAVAILABLE");
	CHECK(hxvr::xrResultName(XR_ERROR_GRAPHICS_DEVICE_INVALID) == "XR_ERROR_GRAPHICS_DEVICE_INVALID");
	CHECK(hxvr::xrResultName(XR_SUCCESS) == "XR_SUCCESS");
	CHECK(hxvr::xrResultName(static_cast<XrResult>(-987654)) == "XR_RESULT_-987654");
}

TEST_CASE("environment blend mode: OPAQUE if offered, else the first") {
	CHECK(hxvr::chooseBlendMode({XR_ENVIRONMENT_BLEND_MODE_ADDITIVE, XR_ENVIRONMENT_BLEND_MODE_OPAQUE}) ==
	      XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
	CHECK(hxvr::chooseBlendMode({XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND, XR_ENVIRONMENT_BLEND_MODE_ADDITIVE}) ==
	      XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND);
	CHECK(hxvr::chooseBlendMode({}) == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM);
}

TEST_CASE("GL_VERSION string parsing") {
	int major = -1, minor = -1;
	CHECK(hxvr::parseGlVersion("4.6.0 NVIDIA 560.94", major, minor));
	CHECK(major == 4);
	CHECK(minor == 6);
	CHECK(hxvr::parseGlVersion("3.3 Mesa", major, minor));
	CHECK(major == 3);
	CHECK(minor == 3);
	CHECK_FALSE(hxvr::parseGlVersion("OpenGL ES 3.2", major, minor));
	CHECK_FALSE(hxvr::parseGlVersion("", major, minor));
	CHECK_FALSE(hxvr::parseGlVersion(nullptr, major, minor));
}
