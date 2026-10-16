// backend_openxr.cpp -- hxvr::Backend on a real OpenXR runtime (XR_KHR_opengl_enable) using the
// engine's legacy WGL context. Based on the Phase-0 spike (branch spike/openxr), with its review
// findings applied. Decisions (state table, formats, GL version, sizes) live in xr_logic.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include "backend.hpp"
#include "gl_ext.hpp"
#include "xr_bindings.hpp"
#include "xr_logic.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hxvr {
namespace {

constexpr int kMaxGlErrorClears = 16;                     // bounded glGetError drain (SteamVR)
constexpr XrDuration kSwapchainWaitNs = 1000000000;       // 1 s for xrWaitSwapchainImage
constexpr int kSwapchainWaitAttempts = 3;                  // XR_TIMEOUT_EXPIRED retries before giving up
constexpr auto kIdleSleep = std::chrono::milliseconds(10);
constexpr auto kExitWait = std::chrono::milliseconds(1500);   // bounded wait for STOPPING on stop()
constexpr std::size_t kMaxDebugMessages = 32;
constexpr XrViewConfigurationType kViewConfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

Pose toPose(const XrPosef& p) {
	return Pose{glm::vec3(p.position.x, p.position.y, p.position.z),
	            glm::quat(p.orientation.w, p.orientation.x, p.orientation.y, p.orientation.z)};
}

Fov toFov(const XrFovf& f) { return Fov{f.angleLeft, f.angleRight, f.angleUp, f.angleDown}; }

// The "gameplay" action set, indexed by Act (xr_bindings.hpp). Every action has both hands as
// subaction paths.
struct ActionDesc { const char* name; const char* localized; XrActionType type; };
constexpr ActionDesc kActions[kActCount] = {
	{"grip_pose", "Grip pose", XR_ACTION_TYPE_POSE_INPUT},
	{"aim_pose", "Aim pose", XR_ACTION_TYPE_POSE_INPUT},
	{"trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT},
	{"squeeze", "Squeeze", XR_ACTION_TYPE_FLOAT_INPUT},
	{"thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
	{"thumbstick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT},
	{"lower", "Lower button", XR_ACTION_TYPE_BOOLEAN_INPUT},
	{"upper", "Upper button", XR_ACTION_TYPE_BOOLEAN_INPUT},
	{"menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT},
};
constexpr const char* kHandPaths[2] = {"/user/hand/left", "/user/hand/right"};
constexpr const char* kHandNames[2] = {"left", "right"};

constexpr int actIndex(Act a) { return static_cast<int>(a); }

// NaN-safe clamp of a runtime-reported analog value.
float clampAxis(float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : 0.0f; }

const char* stateName(XrSessionState s) {
	switch (s) {
#define HXVR_XR_STATE_CASE(name, value) case name: return #name + 17;   // strip "XR_SESSION_STATE_"
	XR_LIST_ENUM_XrSessionState(HXVR_XR_STATE_CASE)
#undef HXVR_XR_STATE_CASE
	default: return "?";
	}
}

std::string versionStr(XrVersion v) {
	return std::to_string(XR_VERSION_MAJOR(v)) + "." + std::to_string(XR_VERSION_MINOR(v)) + "." +
	       std::to_string(XR_VERSION_PATCH(v));
}

std::string hexStr(int64_t v) {
	char b[32];
	std::snprintf(b, sizeof(b), "0x%llX", static_cast<unsigned long long>(v));
	return b;
}

// Debug-utils messages may arrive on runtime threads: queued here, printed on the engine thread.
struct DebugQueue {
	std::mutex m;
	std::vector<std::string> msgs;
	unsigned dropped = 0;
};

XRAPI_ATTR XrBool32 XRAPI_CALL debugCallback(XrDebugUtilsMessageSeverityFlagsEXT sev, XrDebugUtilsMessageTypeFlagsEXT,
                                             const XrDebugUtilsMessengerCallbackDataEXT* data, void* user) {
	auto* q = static_cast<DebugQueue*>(user);
	if (!q || !data) return XR_FALSE;
	std::lock_guard<std::mutex> lock(q->m);
	if (q->msgs.size() >= kMaxDebugMessages) {
		++q->dropped;
		return XR_FALSE;
	}
	std::string s = (sev & XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "VR: XR debug error: " : "VR: XR debug warning: ";
	if (data->functionName) s += std::string(data->functionName) + ": ";
	if (data->message) s += data->message;
	q->msgs.push_back(std::move(s));
	return XR_FALSE;
}

class OpenXrBackend final : public Backend {
public:
	OpenXrBackend(const GlExt* gl, void (*print)(const char*)) : gl_(gl), print_(print) {}
	~OpenXrBackend() override { destroyAll(); }
	OpenXrBackend(const OpenXrBackend&) = delete;
	OpenXrBackend& operator=(const OpenXrBackend&) = delete;

	bool start(const StartParams& p, std::string& err) override {
		if (inst_ != XR_NULL_HANDLE) {
			err = "OpenXR: backend already started";
			return false;
		}
		if (!gl_) {
			err = "OpenXR: no OpenGL entry points";
			return false;
		}
		if (!std::isfinite(p.eyeHeightM) || !(p.eyeHeightM > 0.0f)) {
			err = "OpenXR: eye height must be a positive number of metres (vr_eye_height)";
			return false;
		}
		dc_ = wglGetCurrentDC();
		rc_ = wglGetCurrentContext();
		if (!dc_ || !rc_) {
			err = "OpenXR: no current OpenGL context";
			return false;
		}
		params_ = p;
		head_ = Pose{glm::vec3(0.0f, p.eyeHeightM, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
		eyePose_[0] = eyePose_[1] = head_;
		const bool ok = createInstance(err) && getSystem(err) && checkGl(err) && createSession(err) &&
		                createSpaces(err) && createSwapchains(err);
		if (ok) setupInput();   // never fails the start: without it the session runs without controllers
		flushDebug();
		if (!ok) {
			destroyAll();   // the module never calls stop() after a failed start()
			return false;
		}
		say("OpenXR session created; waiting for the headset (session READY)");
		return true;
	}

	void stop() override {
		if (inst_ == XR_NULL_HANDLE) return;   // idempotent
		shutdownSession();
		flushDebug();
		destroyAll();
	}

	FrameInfo beginFrame(double) override {
		FrameInfo fi{false, false, false};
		if (frameBegun_) endFrame();   // the contract forbids it; never xrBeginFrame twice
		frameBegun_ = renderFrame_ = false;
		eyeDone_[0] = eyeDone_[1] = false;
		hands_[0] = hands_[1] = HandState{};   // inactive unless this frame syncs them (FOCUSED)
		if (sess_ == XR_NULL_HANDLE) {
			fi.lost = true;
			return fi;
		}
		if (!lost_ && !contextOk()) markLost("the OpenGL context changed under the OpenXR session; VR must be restarted");
		if (!lost_) pollEvents();
		flushDebug();
		if (lost_) {
			fi.lost = true;
			return fi;
		}

		const SessionStep st = onSessionState(state_);
		fi.focused = st.focused;
		if (!running_ || st.sleep) {   // IDLE / UNKNOWN / not begun: no xrWaitFrame, no busy loop
			std::this_thread::sleep_for(kIdleSleep);
			return fi;
		}

		XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState fs{XR_TYPE_FRAME_STATE};
		XrResult r = callGl("xrWaitFrame", [&] { return xrWaitFrame(sess_, &wi, &fs); });
		if (XR_FAILED(r)) {
			markLost("xrWaitFrame failed");
			fi.lost = true;
			return fi;
		}
		XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
		r = callGl("xrBeginFrame", [&] { return xrBeginFrame(sess_, &bi); });
		if (XR_FAILED(r)) {
			markLost("xrBeginFrame failed");
			fi.lost = true;
			return fi;
		}
		frameBegun_ = true;
		displayTime_ = fs.predictedDisplayTime;
		if (st.focused) updateHands(displayTime_);
		if (recenterPending_ && displayTime_ >= recenterAt_) {   // poses of this frame are in the changed space
			recenterPending_ = false;
			fi.recenter = true;
		}
		// render => the views AND the head pose were located with valid orientation and position.
		renderFrame_ = st.render && fs.shouldRender && locate(displayTime_);
		fi.render = renderFrame_;
		return fi;
	}

	Pose headPose() const override { return head_; }
	Pose eyePose(int eye) const override { return eyePose_[eye == 0 ? 0 : 1]; }
	Fov eyeFov(int eye) const override { return fov_[eye == 0 ? 0 : 1]; }
	glm::ivec2 eyeSize() const override { return size_; }
	HandState hand(int h) const override { return hands_[h == 0 ? 0 : 1]; }

	BackendInfo info() const override {
		std::string state = lost_ ? "lost" : stateName(state_);
		for (char& ch : state) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return BackendInfo{"openxr", state, runtime_, format_, {profile_[0], profile_[1]},
		                   appSpaceType_ == XR_REFERENCE_SPACE_TYPE_STAGE};
	}

	unsigned acquireEye(int eye) override {
		if (eye < 0 || eye > 1 || !renderFrame_ || eyes_[eye].acquired || lost_) return 0;
		if (!contextOk()) {
			markLost("the OpenGL context changed under the OpenXR session; VR must be restarted");
			return 0;
		}
		Eye& e = eyes_[eye];
		const GlBindings saved = saveBindings();
		uint32_t idx = 0;
		XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
		XrResult r = call("xrAcquireSwapchainImage", [&] { return xrAcquireSwapchainImage(e.sc, &ai, &idx); });
		if (XR_FAILED(r)) {
			restoreBindings(saved);
			return 0;
		}
		XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
		wi.timeout = kSwapchainWaitNs;
		SwapchainWait action = SwapchainWait::Retry;
		for (int attempt = 1; action == SwapchainWait::Retry; ++attempt) {
			r = call("xrWaitSwapchainImage", [&] { return xrWaitSwapchainImage(e.sc, &wi); });
			action = swapchainWaitAction(r, attempt, kSwapchainWaitAttempts);
		}
		if (action == SwapchainWait::Lost) {
			// Acquired but never waited: releasing it is not allowed, so the swapchain is out of step.
			restoreBindings(saved);
			markLost("xrWaitSwapchainImage timed out repeatedly; VR must be restarted");
			return 0;
		}
		if (action == SwapchainWait::Release || idx >= e.images.size()) {   // give the image back
			XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
			call("xrReleaseSwapchainImage", [&] { return xrReleaseSwapchainImage(e.sc, &ri); });
			restoreBindings(saved);
			return 0;
		}
		restoreBindings(saved);
		e.acquired = true;
		return e.images[idx].image;
	}

	void releaseEye(int eye) override {
		if (eye < 0 || eye > 1 || !eyes_[eye].acquired) return;
		eyes_[eye].acquired = false;
		if (!contextOk()) {
			markLost("the OpenGL context changed under the OpenXR session; VR must be restarted");
			return;
		}
		gl_->glFlush();   // the eye's GL commands are submitted before the runtime takes the image
		const GlBindings saved = saveBindings();
		XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
		const XrResult r = call("xrReleaseSwapchainImage",
		                        [&] { return xrReleaseSwapchainImage(eyes_[eye].sc, &ri); });
		restoreBindings(saved);
		eyeDone_[eye] = XR_SUCCEEDED(r);
	}

	void endFrame() override {
		if (!frameBegun_) return;   // beginFrame did not begin a runtime frame
		frameBegun_ = false;
		for (int eye = 0; eye < 2; ++eye) releaseEye(eye);   // contract says released; be safe
		if (lost_ || !contextOk()) {
			if (!lost_) markLost("the OpenGL context changed under the OpenXR session; VR must be restarted");
			renderFrame_ = false;
			return;
		}
		XrCompositionLayerProjectionView pv[2];
		XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
		const XrCompositionLayerBaseHeader* layers[1] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
		uint32_t layerCount = 0;
		if (renderFrame_ && eyeDone_[0] && eyeDone_[1]) {
			for (int i = 0; i < 2; ++i) {
				pv[i] = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
				pv[i].pose = views_[i].pose;
				pv[i].fov = views_[i].fov;
				pv[i].subImage.swapchain = eyes_[i].sc;
				pv[i].subImage.imageRect.offset = {0, 0};
				pv[i].subImage.imageRect.extent = {size_.x, size_.y};
				pv[i].subImage.imageArrayIndex = 0;
			}
			layer.space = appSpace_;
			layer.viewCount = 2;
			layer.views = pv;
			layerCount = 1;
		}
		renderFrame_ = false;
		XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
		ei.displayTime = displayTime_;
		ei.environmentBlendMode = blend_;
		ei.layerCount = layerCount;
		ei.layers = layerCount ? layers : nullptr;
		const GlBindings saved = saveBindings();
		const XrResult r = call("xrEndFrame", [&] { return xrEndFrame(sess_, &ei); }, !endFrameWarned_);
		restoreBindings(saved);
		if (r == XR_ERROR_SESSION_LOST || r == XR_ERROR_INSTANCE_LOST) markLost("xrEndFrame: session lost");
		else if (XR_FAILED(r)) endFrameWarned_ = true;   // logged once; frames keep going
	}

private:
	struct Eye {
		XrSwapchain sc = XR_NULL_HANDLE;
		std::vector<XrSwapchainImageOpenGLKHR> images;
		bool acquired = false;
	};
	struct GlBindings { gl::GLint tex = 0, drawFbo = 0, readFbo = 0; };

	// --- helpers ---------------------------------------------------------------------------

	void say(const std::string& msg) const {
		if (print_) print_(("VR: " + msg).c_str());
	}

	bool contextOk() const { return rc_ && wglGetCurrentContext() == rc_; }

	// Bounded glGetError drain before every XR call (SteamVR checks the GL error state).
	void clearGlErrors() const {
		if (!gl_ || !contextOk()) return;
		for (int i = 0; i < kMaxGlErrorClears && gl_->glGetError() != 0; ++i) {
		}
	}

	std::string resultStr(XrResult r) const {
		char buf[XR_MAX_RESULT_STRING_SIZE] = {};
		std::string name;
		if (inst_ != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(inst_, r, buf))) name = buf;
		else name = xrResultName(r);
		return name + " (" + std::to_string(static_cast<int>(r)) + ")";
	}

	// Every XR call goes through here: GL errors cleared first, failures logged by name.
	template <class F>
	XrResult call(const char* what, F&& f, bool log = true) {
		clearGlErrors();
		const XrResult r = f();
		if (XR_FAILED(r) && log) say(std::string(what) + " failed: " + resultStr(r));
		return r;
	}

	// Same, for XR calls in which the runtime may use GL in our context (frame and session calls,
	// session/swapchain creation and destruction): the engine's texture and framebuffer bindings
	// are saved before and restored after (only while our context is current).
	template <class F>
	XrResult callGl(const char* what, F&& f, bool log = true) {
		const bool guard = gl_ && contextOk();
		GlBindings saved;
		if (guard) saved = saveBindings();
		const XrResult r = call(what, std::forward<F>(f), log);
		if (guard) restoreBindings(saved);
		return r;
	}

	// Same, for start(): the failure goes into `err` (VR_Start reports it), not the console.
	template <class F>
	bool callOrFail(const char* what, F&& f, std::string& err, const char* hint = nullptr) {
		const XrResult r = call(what, std::forward<F>(f), false);
		if (XR_SUCCEEDED(r)) return true;
		err = std::string("OpenXR: ") + what + " failed: " + resultStr(r);
		if (hint) err += std::string(" -- ") + hint;
		return false;
	}

	// The runtime creates and touches GL textures/framebuffers in our context; the engine caches
	// its texture binding, so keep the bindings it sees unchanged across XR calls.
	GlBindings saveBindings() const {
		GlBindings b;
		gl_->glGetIntegerv(gl::kTextureBinding2D, &b.tex);
		gl_->glGetIntegerv(gl::kFramebufferBinding, &b.drawFbo);
		gl_->glGetIntegerv(gl::kReadFramebufferBinding, &b.readFbo);
		return b;
	}
	void restoreBindings(const GlBindings& b) const {
		const GlBindings now = saveBindings();
		if (now.tex != b.tex) gl_->glBindTexture(gl::kTexture2D, static_cast<gl::GLuint>(b.tex));
		if (now.drawFbo != b.drawFbo) gl_->glBindFramebuffer(gl::kDrawFramebuffer, static_cast<gl::GLuint>(b.drawFbo));
		if (now.readFbo != b.readFbo) gl_->glBindFramebuffer(gl::kReadFramebuffer, static_cast<gl::GLuint>(b.readFbo));
	}

	// Asks the glue to recenter on the first frame displayed at or after `at` (0 = the next frame);
	// with a request already pending the later time wins.
	void requestRecenter(XrTime at) {
		recenterAt_ = recenterPending_ ? std::max(recenterAt_, at) : at;
		recenterPending_ = true;
	}

	void markLost(const std::string& why) {
		if (lost_) return;
		lost_ = true;
		if (!stopping_) say(why);
	}

	void flushDebug() {
		std::vector<std::string> msgs;
		unsigned dropped = 0;
		{
			std::lock_guard<std::mutex> lock(debug_.m);
			msgs.swap(debug_.msgs);
			dropped = debug_.dropped;
			debug_.dropped = 0;
		}
		for (const std::string& m : msgs)
			if (print_) print_(m.c_str());
		if (dropped) say(std::to_string(dropped) + " XR debug messages dropped");
	}

	// --- start ----------------------------------------------------------------------------

	bool createInstance(std::string& err) {
		uint32_t n = 0;
		if (!callOrFail("xrEnumerateInstanceExtensionProperties",
		                [&] { return xrEnumerateInstanceExtensionProperties(nullptr, 0, &n, nullptr); }, err,
		                "no OpenXR runtime installed or active?"))
			return false;
		std::vector<XrExtensionProperties> exts(n, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
		if (!callOrFail("xrEnumerateInstanceExtensionProperties",
		                [&] { return xrEnumerateInstanceExtensionProperties(nullptr, n, &n, exts.data()); }, err))
			return false;
		exts.resize(n);
		const auto has = [&](const char* name) {
			return std::any_of(exts.begin(), exts.end(),
			                   [&](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
		};
		if (!has(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
			err = "OpenXR: the runtime does not support OpenGL (XR_KHR_opengl_enable)";
			return false;
		}
		std::vector<const char*> enable{XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};
		bool debugUtils = false;
#ifndef NDEBUG
		debugUtils = has(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (debugUtils) enable.push_back(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

		XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
		std::strncpy(ici.applicationInfo.applicationName, "hxn2vr", XR_MAX_APPLICATION_NAME_SIZE - 1);
		ici.applicationInfo.applicationVersion = 1;
		std::strncpy(ici.applicationInfo.engineName, "uHexen2", XR_MAX_ENGINE_NAME_SIZE - 1);
		ici.applicationInfo.engineVersion = 1;
		ici.applicationInfo.apiVersion = XR_API_VERSION_1_0;
		ici.enabledExtensionCount = static_cast<uint32_t>(enable.size());
		ici.enabledExtensionNames = enable.data();
		if (!callOrFail("xrCreateInstance", [&] { return xrCreateInstance(&ici, &inst_); }, err)) {
			inst_ = XR_NULL_HANDLE;
			return false;
		}
		XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
		if (XR_SUCCEEDED(call("xrGetInstanceProperties", [&] { return xrGetInstanceProperties(inst_, &ip); }))) {
			runtime_ = std::string(ip.runtimeName) + " " + versionStr(ip.runtimeVersion);
			say(std::string("OpenXR runtime \"") + ip.runtimeName + "\" " + versionStr(ip.runtimeVersion));
		}
		if (debugUtils) createDebugMessenger();
		return true;
	}

	void createDebugMessenger() {
		PFN_xrCreateDebugUtilsMessengerEXT create = nullptr;
		if (XR_FAILED(call("xrGetInstanceProcAddr(xrCreateDebugUtilsMessengerEXT)", [&] {
			    return xrGetInstanceProcAddr(inst_, "xrCreateDebugUtilsMessengerEXT",
			                                 reinterpret_cast<PFN_xrVoidFunction*>(&create));
		    })) || !create)
			return;
		XrDebugUtilsMessengerCreateInfoEXT ci{XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
		ci.messageSeverities = XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		ci.messageTypes = XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                  XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
		ci.userCallback = debugCallback;
		ci.userData = &debug_;
		if (XR_FAILED(call("xrCreateDebugUtilsMessengerEXT", [&] { return create(inst_, &ci, &messenger_); })))
			messenger_ = XR_NULL_HANDLE;
	}

	bool getSystem(std::string& err) {
		XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
		sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		if (!callOrFail("xrGetSystem(HMD)", [&] { return xrGetSystem(inst_, &sgi, &sys_); }, err,
		                "headset not connected, or the streamer / runtime not running?"))
			return false;
		XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
		if (!callOrFail("xrGetSystemProperties", [&] { return xrGetSystemProperties(inst_, sys_, &sp); }, err))
			return false;
		say(std::string("OpenXR system \"") + sp.systemName + "\", tracking: orientation " +
		    (sp.trackingProperties.orientationTracking ? "yes" : "no") + ", position " +
		    (sp.trackingProperties.positionTracking ? "yes" : "no"));
		maxSwapchainW_ = sp.graphicsProperties.maxSwapchainImageWidth;
		maxSwapchainH_ = sp.graphicsProperties.maxSwapchainImageHeight;

		uint32_t n = 0;
		if (!callOrFail("xrEnumerateViewConfigurationViews",
		                [&] { return xrEnumerateViewConfigurationViews(inst_, sys_, kViewConfig, 0, &n, nullptr); }, err))
			return false;
		if (n != 2) {
			err = "OpenXR: the headset has no stereo view configuration";
			return false;
		}
		viewConf_[0] = viewConf_[1] = XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW};
		if (!callOrFail("xrEnumerateViewConfigurationViews",
		                [&] { return xrEnumerateViewConfigurationViews(inst_, sys_, kViewConfig, 2, &n, viewConf_); }, err))
			return false;

		uint32_t bn = 0;
		if (!callOrFail("xrEnumerateEnvironmentBlendModes",
		                [&] { return xrEnumerateEnvironmentBlendModes(inst_, sys_, kViewConfig, 0, &bn, nullptr); }, err))
			return false;
		std::vector<XrEnvironmentBlendMode> modes(bn);
		if (!callOrFail("xrEnumerateEnvironmentBlendModes", [&] {
			    return xrEnumerateEnvironmentBlendModes(inst_, sys_, kViewConfig, bn, &bn, modes.data());
		    }, err))
			return false;
		modes.resize(bn);
		blend_ = chooseBlendMode(modes);
		if (blend_ == XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM) {
			err = "OpenXR: the runtime offers no environment blend mode";
			return false;
		}
		return true;
	}

	bool checkGl(std::string& err) {
		PFN_xrGetOpenGLGraphicsRequirementsKHR getReq = nullptr;
		if (!callOrFail("xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)", [&] {
			    return xrGetInstanceProcAddr(inst_, "xrGetOpenGLGraphicsRequirementsKHR",
			                                 reinterpret_cast<PFN_xrVoidFunction*>(&getReq));
		    }, err))
			return false;
		if (!getReq) {
			err = "OpenXR: xrGetOpenGLGraphicsRequirementsKHR not available";
			return false;
		}
		XrGraphicsRequirementsOpenGLKHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR};
		if (!callOrFail("xrGetOpenGLGraphicsRequirementsKHR", [&] { return getReq(inst_, sys_, &req); }, err))
			return false;
		int major = 0, minor = 0;
		const char* ver = reinterpret_cast<const char*>(gl_->glGetString(gl::kVersion));
		if (!parseGlVersion(ver, major, minor)) {
			err = std::string("OpenXR: cannot read the OpenGL version (\"") + (ver ? ver : "null") + "\")";
			return false;
		}
		std::string why;
		const bool ok = glVersionOk(major, minor, req.minApiVersionSupported, req.maxApiVersionSupported, why);
		say("OpenGL " + std::to_string(major) + "." + std::to_string(minor) + ", runtime requires " +
		    versionStr(req.minApiVersionSupported) + " .. " + versionStr(req.maxApiVersionSupported));
		if (!ok) {
			err = "OpenXR: " + why;
			return false;
		}
		if (!why.empty()) say(why);
		return true;
	}

	bool createSession(std::string& err) {
		XrGraphicsBindingOpenGLWin32KHR gb{XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR};
		gb.hDC = dc_;
		gb.hGLRC = rc_;
		XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
		sci.next = &gb;
		sci.systemId = sys_;
		const XrResult r = callGl("xrCreateSession", [&] { return xrCreateSession(inst_, &sci, &sess_); }, false);
		if (XR_SUCCEEDED(r)) return true;
		sess_ = XR_NULL_HANDLE;
		err = "OpenXR: xrCreateSession failed: " + resultStr(r);
		if (r == XR_ERROR_GRAPHICS_DEVICE_INVALID)
			err += " -- the OpenGL context is not on the GPU the headset is connected to "
			       "(on a laptop with hybrid graphics, select the discrete GPU)";
		return false;
	}

	bool createSpaces(std::string& err) {
		uint32_t n = 0;
		if (!callOrFail("xrEnumerateReferenceSpaces", [&] { return xrEnumerateReferenceSpaces(sess_, 0, &n, nullptr); }, err))
			return false;
		std::vector<XrReferenceSpaceType> types(n);
		if (!callOrFail("xrEnumerateReferenceSpaces",
		                [&] { return xrEnumerateReferenceSpaces(sess_, n, &n, types.data()); }, err))
			return false;
		types.resize(n);
		const bool stage = std::find(types.begin(), types.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != types.end();
		appSpaceType_ = stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;

		// Poses are floor-relative: STAGE has its origin on the floor. LOCAL's origin is at the
		// initial head position, so the app space is LOCAL shifted down by the eye height.
		XrReferenceSpaceCreateInfo rsi{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
		rsi.referenceSpaceType = stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
		rsi.poseInReferenceSpace.orientation.w = 1.0f;
		if (!stage) rsi.poseInReferenceSpace.position.y = -params_.eyeHeightM;
		if (!callOrFail("xrCreateReferenceSpace", [&] { return xrCreateReferenceSpace(sess_, &rsi, &appSpace_); }, err)) {
			appSpace_ = XR_NULL_HANDLE;
			return false;
		}
		rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
		rsi.poseInReferenceSpace = XrPosef{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
		if (!callOrFail("xrCreateReferenceSpace(VIEW)", [&] { return xrCreateReferenceSpace(sess_, &rsi, &viewSpace_); }, err)) {
			viewSpace_ = XR_NULL_HANDLE;
			return false;
		}
		say(stage ? std::string("OpenXR tracking space STAGE (floor level)")
		          : "OpenXR tracking space LOCAL + " + std::to_string(params_.eyeHeightM).substr(0, 4) +
		                " m (no STAGE space)");
		return true;
	}

	bool createSwapchains(std::string& err) {
		uint32_t n = 0;
		if (!callOrFail("xrEnumerateSwapchainFormats", [&] { return xrEnumerateSwapchainFormats(sess_, 0, &n, nullptr); }, err))
			return false;
		std::vector<int64_t> formats(n);
		if (!callOrFail("xrEnumerateSwapchainFormats",
		                [&] { return xrEnumerateSwapchainFormats(sess_, n, &n, formats.data()); }, err))
			return false;
		formats.resize(n);
		const FormatChoice fc = chooseSwapchainFormat(formats);
		if (!fc.format) {
			err = "OpenXR: the runtime offers no colour swapchain format";
			return false;
		}

		// One size for both eyes, fixed for the session.
		const uint32_t recW = std::max(viewConf_[0].recommendedImageRectWidth, viewConf_[1].recommendedImageRectWidth);
		const uint32_t recH = std::max(viewConf_[0].recommendedImageRectHeight, viewConf_[1].recommendedImageRectHeight);
		uint32_t maxW = std::min(viewConf_[0].maxImageRectWidth, viewConf_[1].maxImageRectWidth);
		uint32_t maxH = std::min(viewConf_[0].maxImageRectHeight, viewConf_[1].maxImageRectHeight);
		if (maxSwapchainW_) maxW = std::min(maxW, maxSwapchainW_);
		if (maxSwapchainH_) maxH = std::min(maxH, maxSwapchainH_);
		size_ = glm::ivec2(scaledEyeDim(recW, params_.renderScale, maxW), scaledEyeDim(recH, params_.renderScale, maxH));

		const GlBindings saved = saveBindings();
		bool ok = true;
		for (int i = 0; i < 2 && ok; ++i) {
			XrSwapchainCreateInfo sc{XR_TYPE_SWAPCHAIN_CREATE_INFO};
			sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
			sc.format = fc.format;
			sc.sampleCount = 1;
			sc.width = static_cast<uint32_t>(size_.x);
			sc.height = static_cast<uint32_t>(size_.y);
			sc.faceCount = 1;
			sc.arraySize = 1;
			sc.mipCount = 1;
			Eye& e = eyes_[i];
			ok = callOrFail("xrCreateSwapchain", [&] { return xrCreateSwapchain(sess_, &sc, &e.sc); }, err);
			if (!ok) {
				e.sc = XR_NULL_HANDLE;
				break;
			}
			uint32_t cnt = 0;
			ok = callOrFail("xrEnumerateSwapchainImages", [&] { return xrEnumerateSwapchainImages(e.sc, 0, &cnt, nullptr); }, err);
			if (!ok) break;
			e.images.assign(cnt, XrSwapchainImageOpenGLKHR{XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR});
			ok = callOrFail("xrEnumerateSwapchainImages", [&] {
				return xrEnumerateSwapchainImages(e.sc, cnt, &cnt,
				                                  reinterpret_cast<XrSwapchainImageBaseHeader*>(e.images.data()));
			}, err);
			if (!ok) break;
			e.images.resize(cnt);
			if (e.images.empty()) {
				err = "OpenXR: swapchain has no images";
				ok = false;
			}
		}
		restoreBindings(saved);
		if (!ok) return false;
		format_ = hexStr(fc.format) + (fc.gammaPassthrough ? " (sRGB)" : "");
		say("OpenXR swapchain format " + hexStr(fc.format) + (fc.gammaPassthrough ? " (sRGB, gamma passthrough)" : "") +
		    ", " + std::to_string(size_.x) + "x" + std::to_string(size_.y) + " per eye (recommended " +
		    std::to_string(recW) + "x" + std::to_string(recH) + "), " + std::to_string(eyes_[0].images.size()) +
		    " images");
		if (!fc.gammaPassthrough)
			say("warning: the runtime offers no sRGB swapchain format; the headset image may look washed out");
		return true;
	}

	// --- input ----------------------------------------------------------------------------

	// Same as call() without the glGetError drain: for the per-frame input calls (xrSyncActions,
	// action states, hand spaces), which never touch GL; keeps ~25 glGetError round trips per frame
	// off the driver.
	template <class F>
	XrResult callInput(const char* what, F&& f, bool log) {
		const XrResult r = f();
		if (XR_FAILED(r) && log) say(std::string(what) + " failed: " + resultStr(r));
		return r;
	}

	// Action set, actions, suggested bindings, attach, action spaces. Any failure is logged and leaves
	// the session without controllers (every hand inactive); it never fails start().
	void setupInput() {
		if (!createActions()) {
			destroyInput();
			say("controllers unavailable: the OpenXR actions could not be created");
			return;
		}
		suggestBindings();
		XrSessionActionSetsAttachInfo ai{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
		ai.countActionSets = 1;
		ai.actionSets = &actionSet_;
		if (XR_FAILED(call("xrAttachSessionActionSets", [&] { return xrAttachSessionActionSets(sess_, &ai); }))) {
			destroyInput();
			say("controllers unavailable: the OpenXR action set could not be attached");
			return;
		}
		// A missing action space only makes that pose never valid (logged by call()).
		for (int h = 0; h < 2; ++h) {
			gripSpace_[h] = createActionSpace(Act::GripPose, h);
			aimSpace_[h] = createActionSpace(Act::AimPose, h);
		}
		inputReady_ = true;
	}

	bool createActions() {
		for (int h = 0; h < 2; ++h)
			if (XR_FAILED(call("xrStringToPath", [&] { return xrStringToPath(inst_, kHandPaths[h], &handPath_[h]); })))
				return false;
		XrActionSetCreateInfo si{XR_TYPE_ACTION_SET_CREATE_INFO};
		std::strncpy(si.actionSetName, "gameplay", XR_MAX_ACTION_SET_NAME_SIZE - 1);
		std::strncpy(si.localizedActionSetName, "Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
		si.priority = 0;
		if (XR_FAILED(call("xrCreateActionSet", [&] { return xrCreateActionSet(inst_, &si, &actionSet_); }))) {
			actionSet_ = XR_NULL_HANDLE;
			return false;
		}
		for (int a = 0; a < kActCount; ++a) {
			XrActionCreateInfo ci{XR_TYPE_ACTION_CREATE_INFO};
			std::strncpy(ci.actionName, kActions[a].name, XR_MAX_ACTION_NAME_SIZE - 1);
			std::strncpy(ci.localizedActionName, kActions[a].localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
			ci.actionType = kActions[a].type;
			ci.countSubactionPaths = 2;
			ci.subactionPaths = handPath_;
			if (XR_FAILED(call("xrCreateAction", [&] { return xrCreateAction(actionSet_, &ci, &actions_[a]); }))) {
				actions_[a] = XR_NULL_HANDLE;
				return false;
			}
		}
		return true;
	}

	// One suggestion per profile of the table; prints "<profile>: <n> of <m> bindings accepted".
	void suggestBindings() {
		for (const ProfileBindings& pb : suggestedBindings()) {
			XrPath profile = XR_NULL_PATH;
			std::vector<XrActionSuggestedBinding> cands;
			if (XR_SUCCEEDED(call("xrStringToPath", [&] { return xrStringToPath(inst_, pb.profile, &profile); }))) {
				for (const Binding& b : pb.bindings) {
					XrPath p = XR_NULL_PATH;
					if (XR_SUCCEEDED(call("xrStringToPath", [&] { return xrStringToPath(inst_, b.path, &p); })))
						cands.push_back(XrActionSuggestedBinding{actions_[actIndex(b.act)], p});
				}
			}
			const std::size_t accepted = profile != XR_NULL_PATH ? suggestProfile(profile, cands) : 0;
			say(std::string(pb.profile) + ": " + std::to_string(accepted) + " of " + std::to_string(pb.bindings.size()) +
			    " bindings accepted");
		}
	}

	// All candidates at once; if the runtime refuses, one at a time: each call suggests the accepted
	// ones plus one candidate, which is dropped when the call fails (a successful call replaces the
	// profile's previous suggestion). Returns the number accepted.
	std::size_t suggestProfile(XrPath profile, const std::vector<XrActionSuggestedBinding>& cands) {
		if (cands.empty()) return 0;
		const auto suggest = [&](const std::vector<XrActionSuggestedBinding>& v, bool log) {
			XrInteractionProfileSuggestedBinding sb{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
			sb.interactionProfile = profile;
			sb.countSuggestedBindings = static_cast<uint32_t>(v.size());
			sb.suggestedBindings = v.data();
			return XR_SUCCEEDED(call("xrSuggestInteractionProfileBindings",
			                         [&] { return xrSuggestInteractionProfileBindings(inst_, &sb); }, log));
		};
		if (suggest(cands, true)) return cands.size();
		std::vector<XrActionSuggestedBinding> ok;
		bool lastOk = true;
		for (const XrActionSuggestedBinding& c : cands) {
			ok.push_back(c);
			lastOk = suggest(ok, false);
			if (!lastOk) ok.pop_back();
		}
		// The last call failed: suggest the accepted set once more, so it is what the runtime keeps.
		if (!lastOk && !ok.empty() && !suggest(ok, false)) return 0;
		return ok.size();
	}

	XrSpace createActionSpace(Act a, int h) {
		XrActionSpaceCreateInfo ci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
		ci.action = actions_[actIndex(a)];
		ci.subactionPath = handPath_[h];
		ci.poseInActionSpace.orientation.w = 1.0f;
		XrSpace s = XR_NULL_HANDLE;
		if (XR_FAILED(call("xrCreateActionSpace", [&] { return xrCreateActionSpace(sess_, &ci, &s); })))
			return XR_NULL_HANDLE;
		return s;
	}

	// Per FOCUSED frame, after xrBeginFrame (hands_ were reset to inactive by beginFrame). Input
	// failures are logged once per session; a failed call leaves the hand (or the value) inactive.
	void updateHands(XrTime t) {
		if (!inputReady_) return;
		XrActiveActionSet active{actionSet_, XR_NULL_PATH};
		XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
		si.countActiveActionSets = 1;
		si.activeActionSets = &active;
		const XrResult r = callInput("xrSyncActions", [&] { return xrSyncActions(sess_, &si); }, !inputWarned_);
		if (!inputOk(r)) return;
		if (r == XR_SESSION_NOT_FOCUSED) return;   // a success code: focus just went away, hands stay inactive
		for (int h = 0; h < 2; ++h) hands_[h] = readHand(h, t);
	}

	bool inputOk(XrResult r) {
		if (XR_SUCCEEDED(r)) return true;
		inputWarned_ = true;
		return false;
	}

	XrActionStateGetInfo getInfo(Act a, int h) const {
		XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
		gi.action = actions_[actIndex(a)];
		gi.subactionPath = handPath_[h];
		return gi;
	}

	// active = the grip pose action is active (a controller is bound to this hand).
	HandState readHand(int h, XrTime t) {
		HandState s;
		XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE};
		const XrActionStateGetInfo pi = getInfo(Act::GripPose, h);
		if (!inputOk(callInput("xrGetActionStatePose", [&] { return xrGetActionStatePose(sess_, &pi, &pose); },
		                       !inputWarned_)) ||
		    !pose.isActive)
			return HandState{};
		s.active = true;
		Pose grip = s.grip, aim = s.aim;
		if (locateHand(gripSpace_[h], t, grip) && locateHand(aimSpace_[h], t, aim)) {
			s.poseValid = true;
			s.grip = grip;
			s.aim = aim;
		}
		s.trigger = clampAxis(getFloat(Act::Trigger, h), 0.0f, 1.0f);
		s.squeeze = clampAxis(getFloat(Act::Squeeze, h), 0.0f, 1.0f);
		const XrVector2f stick = getVec2(Act::Stick, h);
		s.stick = glm::vec2(clampAxis(stick.x, -1.0f, 1.0f), clampAxis(stick.y, -1.0f, 1.0f));
		s.stickClick = getBool(Act::StickClick, h);
		s.lower = getBool(Act::Lower, h);
		s.upper = getBool(Act::Upper, h);
		s.menu = getBool(Act::Menu, h);
		return s;
	}

	// True with `out` set only if the space was located with valid orientation and position.
	bool locateHand(XrSpace sp, XrTime t, Pose& out) {
		if (sp == XR_NULL_HANDLE) return false;
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		if (!inputOk(callInput("xrLocateSpace(hand)", [&] { return xrLocateSpace(sp, appSpace_, t, &loc); },
		                       !inputWarned_)) ||
		    !poseFlagsValid(loc.locationFlags))
			return false;
		out = toPose(loc.pose);
		return true;
	}

	// Unbound / inactive actions read as 0 / false.
	float getFloat(Act a, int h) {
		XrActionStateFloat st{XR_TYPE_ACTION_STATE_FLOAT};
		const XrActionStateGetInfo gi = getInfo(a, h);
		if (!inputOk(callInput("xrGetActionStateFloat", [&] { return xrGetActionStateFloat(sess_, &gi, &st); },
		                       !inputWarned_)) ||
		    !st.isActive)
			return 0.0f;
		return st.currentState;
	}

	XrVector2f getVec2(Act a, int h) {
		XrActionStateVector2f st{XR_TYPE_ACTION_STATE_VECTOR2F};
		const XrActionStateGetInfo gi = getInfo(a, h);
		if (!inputOk(callInput("xrGetActionStateVector2f", [&] { return xrGetActionStateVector2f(sess_, &gi, &st); },
		                       !inputWarned_)) ||
		    !st.isActive)
			return XrVector2f{0.0f, 0.0f};
		return st.currentState;
	}

	bool getBool(Act a, int h) {
		XrActionStateBoolean st{XR_TYPE_ACTION_STATE_BOOLEAN};
		const XrActionStateGetInfo gi = getInfo(a, h);
		if (!inputOk(callInput("xrGetActionStateBoolean", [&] { return xrGetActionStateBoolean(sess_, &gi, &st); },
		                       !inputWarned_)) ||
		    !st.isActive)
			return false;
		return st.currentState == XR_TRUE;
	}

	// XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: re-reads both hands' profiles; prints
	// "<left|right> hand: <profile|none>" for each hand whose profile changed.
	void updateProfiles() {
		if (!inputReady_) return;
		for (int h = 0; h < 2; ++h) {
			XrInteractionProfileState ps{XR_TYPE_INTERACTION_PROFILE_STATE};
			std::string name;
			if (XR_SUCCEEDED(call("xrGetCurrentInteractionProfile",
			                      [&] { return xrGetCurrentInteractionProfile(sess_, handPath_[h], &ps); })) &&
			    ps.interactionProfile != XR_NULL_PATH)
				name = pathStr(ps.interactionProfile);
			if (name == profile_[h]) continue;
			profile_[h] = name;
			if (!stopping_) say(std::string(kHandNames[h]) + " hand: " + (name.empty() ? "none" : name));
		}
	}

	std::string pathStr(XrPath p) {
		char buf[XR_MAX_PATH_LENGTH] = {};
		uint32_t n = 0;
		if (XR_FAILED(call("xrPathToString", [&] { return xrPathToString(inst_, p, XR_MAX_PATH_LENGTH, &n, buf); })))
			return "unknown";
		buf[XR_MAX_PATH_LENGTH - 1] = '\0';
		return buf;
	}

	// Action spaces, then the action set (its actions with it); before the session and instance go.
	void destroyInput() {
		for (int h = 0; h < 2; ++h) {
			for (XrSpace* sp : {&gripSpace_[h], &aimSpace_[h]}) {
				if (*sp != XR_NULL_HANDLE) call("xrDestroySpace", [&] { return xrDestroySpace(*sp); });
				*sp = XR_NULL_HANDLE;
			}
		}
		if (actionSet_ != XR_NULL_HANDLE) call("xrDestroyActionSet", [&] { return xrDestroyActionSet(actionSet_); });
		actionSet_ = XR_NULL_HANDLE;
		for (XrAction& a : actions_) a = XR_NULL_HANDLE;
		handPath_[0] = handPath_[1] = XR_NULL_PATH;
		inputReady_ = inputWarned_ = false;
		hands_[0] = hands_[1] = HandState{};
		profile_[0].clear();
		profile_[1].clear();
	}

	// --- frame ----------------------------------------------------------------------------

	void pollEvents() {
		for (int guard = 0; guard < 64; ++guard) {
			XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
			const XrResult r = call("xrPollEvent", [&] { return xrPollEvent(inst_, &ev); });
			if (r == XR_EVENT_UNAVAILABLE) return;
			if (XR_FAILED(r)) {
				markLost("xrPollEvent failed");
				return;
			}
			switch (ev.type) {
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				const auto* e = reinterpret_cast<const XrEventDataSessionStateChanged*>(&ev);
				if (e->session == sess_) onState(e->state);
				break;
			}
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
				markLost("OpenXR runtime is shutting down (instance loss pending)");
				break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
				const auto* e = reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(&ev);
				if (e->session == sess_) {
					say(std::string("OpenXR reference space change pending (") +
					    (e->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE   ? "STAGE"
					     : e->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL ? "LOCAL"
					                                                              : "other") +
					    "): the tracking origin moved");
					// the app space is based on STAGE or LOCAL: recenter once the change is in effect
					if (e->referenceSpaceType == appSpaceType_) requestRecenter(e->changeTime);
				}
				break;
			}
			case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
				const auto* e = reinterpret_cast<const XrEventDataInteractionProfileChanged*>(&ev);
				if (e->session == sess_) updateProfiles();
				break;
			}
			case XR_TYPE_EVENT_DATA_EVENTS_LOST:
				say("OpenXR events lost");
				break;
			default:
				break;
			}
			if (lost_) return;
		}
	}

	void onState(XrSessionState s) {
		state_ = s;
		if (!stopping_) say(std::string("OpenXR session ") + stateName(s));
		const SessionStep st = onSessionState(s);
		if (st.beginSession && !running_) {
			XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
			bi.primaryViewConfigurationType = kViewConfig;
			if (XR_SUCCEEDED(callGl("xrBeginSession", [&] { return xrBeginSession(sess_, &bi); }))) {
				running_ = true;
				// READY again after STOPPING (headset off/on): the player may stand elsewhere now.
				// The first begin needs no request: the glue recenters on the first rendered frame.
				if (sessionEnded_) requestRecenter(0);
			} else {
				markLost("xrBeginSession failed");
			}
		}
		if (st.endSession && running_) {
			callGl("xrEndSession", [&] { return xrEndSession(sess_); });
			running_ = false;
			sessionEnded_ = true;
		}
		if (st.lost) markLost(s == XR_SESSION_STATE_EXITING ? "OpenXR session exiting" : "OpenXR session lost");
	}

	// Locates both views and the head in the app space. True only if every pose is valid
	// (orientation and position); then the poses/FOVs of this frame are stored.
	bool locate(XrTime t) {
		XrViewLocateInfo li{XR_TYPE_VIEW_LOCATE_INFO};
		li.viewConfigurationType = kViewConfig;
		li.displayTime = t;
		li.space = appSpace_;
		XrViewState vs{XR_TYPE_VIEW_STATE};
		XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
		uint32_t n = 0;
		XrResult r = call("xrLocateViews", [&] { return xrLocateViews(sess_, &li, &vs, 2, &n, views); });
		bool valid = XR_SUCCEEDED(r) && n == 2 && poseFlagsValid(vs.viewStateFlags);
		XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
		if (valid) {
			r = call("xrLocateSpace", [&] { return xrLocateSpace(viewSpace_, appSpace_, t, &loc); });
			valid = XR_SUCCEEDED(r) && poseFlagsValid(loc.locationFlags);
		}
		if (!valid) {
			if (!trackingWarned_) say("headset tracking not valid; not rendering to the headset");
			trackingWarned_ = true;
			return false;
		}
		if (trackingWarned_) say("headset tracking valid");
		trackingWarned_ = false;
		for (int i = 0; i < 2; ++i) {
			views_[i] = views[i];
			eyePose_[i] = toPose(views[i].pose);
			fov_[i] = toFov(views[i].fov);
		}
		head_ = toPose(loc.pose);
		return true;
	}

	// --- stop -----------------------------------------------------------------------------

	// Ends a running session: xrRequestExitSession, then keeps an empty frame loop going until the
	// runtime reports STOPPING (xrEndSession) -- bounded by kExitWait.
	void shutdownSession() {
		if (sess_ == XR_NULL_HANDLE) return;
		stopping_ = true;
		if (frameBegun_) endFrame();
		if (running_ && !lost_ && contextOk() &&
		    XR_SUCCEEDED(call("xrRequestExitSession", [&] { return xrRequestExitSession(sess_); }))) {
			const auto deadline = std::chrono::steady_clock::now() + kExitWait;
			while (running_ && !lost_ && std::chrono::steady_clock::now() < deadline) {
				pollEvents();   // STOPPING -> xrEndSession, running_ = false
				if (!running_ || lost_) break;
				// running_: READY (begun, not yet synchronized) also needs frames to progress
				if (state_ == XR_SESSION_STATE_READY || state_ == XR_SESSION_STATE_SYNCHRONIZED ||
				    state_ == XR_SESSION_STATE_VISIBLE || state_ == XR_SESSION_STATE_FOCUSED) {
					if (!emptyFrame()) break;
				} else {
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
			}
		}
		if (running_ && !lost_) {   // STOPPING never came: end anyway (destroy follows)
			if (contextOk()) callGl("xrEndSession", [&] { return xrEndSession(sess_); });
		}
		running_ = false;
	}

	bool emptyFrame() {
		XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
		XrFrameState fs{XR_TYPE_FRAME_STATE};
		if (XR_FAILED(callGl("xrWaitFrame", [&] { return xrWaitFrame(sess_, &wi, &fs); }))) return false;
		XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
		if (XR_FAILED(callGl("xrBeginFrame", [&] { return xrBeginFrame(sess_, &bi); }))) return false;
		XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
		ei.displayTime = fs.predictedDisplayTime;
		ei.environmentBlendMode = blend_;
		return XR_SUCCEEDED(callGl("xrEndFrame", [&] { return xrEndFrame(sess_, &ei); }));
	}

	// Destroys every handle (instance last). Safe on a partial start. If another GL context is
	// current, none is made current while the runtime frees its GL objects, so it cannot delete
	// names in someone else's context; the caller's context is restored afterwards.
	void destroyAll() {
		const HGLRC curRc = wglGetCurrentContext();
		const HDC curDc = wglGetCurrentDC();
		const bool foreign = curRc && curRc != rc_;
		if (foreign) wglMakeCurrent(nullptr, nullptr);
		for (Eye& e : eyes_) {
			if (e.sc != XR_NULL_HANDLE) callGl("xrDestroySwapchain", [&] { return xrDestroySwapchain(e.sc); });
			e = Eye{};
		}
		destroyInput();
		if (viewSpace_ != XR_NULL_HANDLE) call("xrDestroySpace", [&] { return xrDestroySpace(viewSpace_); });
		if (appSpace_ != XR_NULL_HANDLE) call("xrDestroySpace", [&] { return xrDestroySpace(appSpace_); });
		if (sess_ != XR_NULL_HANDLE) callGl("xrDestroySession", [&] { return xrDestroySession(sess_); });
		if (messenger_ != XR_NULL_HANDLE) {
			PFN_xrDestroyDebugUtilsMessengerEXT destroy = nullptr;
			if (XR_SUCCEEDED(call("xrGetInstanceProcAddr(xrDestroyDebugUtilsMessengerEXT)", [&] {
				    return xrGetInstanceProcAddr(inst_, "xrDestroyDebugUtilsMessengerEXT",
				                                 reinterpret_cast<PFN_xrVoidFunction*>(&destroy));
			    })) && destroy)
				call("xrDestroyDebugUtilsMessengerEXT", [&] { return destroy(messenger_); });
		}
		if (inst_ != XR_NULL_HANDLE) call("xrDestroyInstance", [&] { return xrDestroyInstance(inst_); });
		if (foreign) wglMakeCurrent(curDc, curRc);
		viewSpace_ = appSpace_ = XR_NULL_HANDLE;
		sess_ = XR_NULL_HANDLE;
		messenger_ = XR_NULL_HANDLE;
		inst_ = XR_NULL_HANDLE;
		sys_ = XR_NULL_SYSTEM_ID;
		state_ = XR_SESSION_STATE_UNKNOWN;
		running_ = lost_ = stopping_ = frameBegun_ = renderFrame_ = false;
		sessionEnded_ = recenterPending_ = false;
		recenterAt_ = 0;
		appSpaceType_ = XR_REFERENCE_SPACE_TYPE_STAGE;
		eyeDone_[0] = eyeDone_[1] = false;
		trackingWarned_ = endFrameWarned_ = false;
		runtime_.clear();
		format_.clear();
		{
			std::lock_guard<std::mutex> lock(debug_.m);
			debug_.msgs.clear();
			debug_.dropped = 0;
		}
	}

	const GlExt* gl_;
	void (*print_)(const char*);
	StartParams params_{1.0f, 1.65f, false, 0.0f};
	HDC dc_ = nullptr;
	HGLRC rc_ = nullptr;

	XrInstance inst_ = XR_NULL_HANDLE;
	XrDebugUtilsMessengerEXT messenger_ = XR_NULL_HANDLE;
	XrSystemId sys_ = XR_NULL_SYSTEM_ID;
	XrSession sess_ = XR_NULL_HANDLE;
	XrSpace appSpace_ = XR_NULL_HANDLE;
	XrSpace viewSpace_ = XR_NULL_HANDLE;
	XrViewConfigurationView viewConf_[2] = {{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}};
	XrEnvironmentBlendMode blend_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	uint32_t maxSwapchainW_ = 0, maxSwapchainH_ = 0;
	Eye eyes_[2];
	glm::ivec2 size_{0, 0};

	XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
	bool running_ = false;      // xrBeginSession succeeded, xrEndSession not yet called
	bool lost_ = false;         // session unusable (sticky until stop)
	bool stopping_ = false;     // inside stop(): state/lost messages are not printed
	bool frameBegun_ = false;   // xrBeginFrame succeeded, xrEndFrame pending
	bool sessionEnded_ = false;     // xrEndSession was called (a later xrBeginSession is a restart)
	bool recenterPending_ = false;  // FrameInfo::recenter to report once displayTime_ >= recenterAt_
	XrTime recenterAt_ = 0;
	XrReferenceSpaceType appSpaceType_ = XR_REFERENCE_SPACE_TYPE_STAGE;   // what appSpace_ is based on
	bool renderFrame_ = false;  // FrameInfo::render of the current frame
	bool eyeDone_[2] = {false, false};
	std::string runtime_, format_;   // for info(): runtime name + version, swapchain format
	bool trackingWarned_ = false;
	bool endFrameWarned_ = false;
	XrTime displayTime_ = 0;
	XrView views_[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
	Pose head_{glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
	Pose eyePose_[2] = {head_, head_};
	Fov fov_[2] = {{-0.785f, 0.785f, 0.785f, -0.785f}, {-0.785f, 0.785f, 0.785f, -0.785f}};
	DebugQueue debug_;

	// Controllers (setupInput / updateHands). inputReady_: the action set is attached.
	XrActionSet actionSet_ = XR_NULL_HANDLE;
	XrAction actions_[kActCount] = {};   // indexed by Act
	XrPath handPath_[2] = {XR_NULL_PATH, XR_NULL_PATH};
	XrSpace gripSpace_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
	XrSpace aimSpace_[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
	bool inputReady_ = false;
	bool inputWarned_ = false;   // an input call failed this session (logged once)
	HandState hands_[2];         // of the last beginFrame
	std::string profile_[2];     // current interaction profile per hand; empty = none
};

}  // namespace

std::unique_ptr<Backend> makeOpenXrBackend(const GlExt* gl, void (*print)(const char*)) {
	return std::make_unique<OpenXrBackend>(gl, print);
}

}  // namespace hxvr
