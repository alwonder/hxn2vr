// backend.hpp -- abstract VR runtime backend (OpenXR or the mock "fake headset").
// Module-internal C++ header; the engine only ever sees vr_api.h.
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>

namespace hxvr {

struct GlExt;   // GL entry points (gl_ext.hpp). Backends keep the pointer; it must outlive them.

struct Pose { glm::vec3 pos; glm::quat rot; };   // metres, OpenXR axes, relative to the floor
struct Fov { float left, right, up, down; };      // radians, left/down < 0
// Raw controller state of one hand, as of the last beginFrame. Metres, tracking space. The module
// (vr_input.cpp) derives the VR_BTN_TRIGGER / VR_BTN_GRIP bits from trigger / squeeze with hysteresis.
struct HandState {
	bool active = false;      // a controller is bound and its pose action is active
	bool poseValid = false;   // grip and aim located this frame (orientation and position valid)
	Pose grip{glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
	Pose aim{glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f)};
	float trigger = 0, squeeze = 0;   // 0..1
	glm::vec2 stick{0.0f};            // x right, y up, -1..1, raw
	bool lower = false, upper = false, stickClick = false, menu = false;
};
// recenter: the tracking origin moved since the previous beginFrame (vr_frame_t::recenter).
struct FrameInfo { bool render; bool focused; bool lost; bool recenter = false; };
struct StartParams { float renderScale; float eyeHeightM; bool mockMotion; float mockHz; };
// Human-readable facts about the running backend, for vr_status. Plain strings; empty = not applicable.
struct BackendInfo {
	std::string name;              // "mock" / "openxr"
	std::string state;             // session state, lower case: idle, ready, synchronized, visible, focused, stopping, lost
	std::string runtime;           // runtime name and version (OpenXR); empty for the mock
	std::string swapchainFormat;   // colour format of the eye images
	// Interaction profile per hand (0 = left, 1 = right). OpenXR: the runtime's current profile path,
	// empty while no controller is bound; mock: always "mock".
	std::string profile[2];
	// The tracking space is floor-level (OpenXR STAGE; the mock is always). false = LOCAL shifted down by
	// StartParams::eyeHeightM: the head height then is that value at session start, not the real one.
	bool stageSpace = true;
};

// Contract (the caller, vr_module.cpp, guarantees it; backends may rely on it):
//  - Per frame: beginFrame -> [acquireEye/releaseEye for each eye, only if FrameInfo::render]
//    -> endFrame. endFrame is called after EVERY beginFrame, also when not rendering (OpenXR
//    must still xrEndFrame with zero layers); a backend whose beginFrame did not begin a
//    runtime frame treats that endFrame as a no-op. An acquired eye is always released before
//    endFrame.
//  - stop() is idempotent. A failed start() is never followed by stop(): the destructor must
//    clean up whatever a partial start() created.
//  - GL work (eye textures, swapchains) happens on the thread with the engine's GL context
//    current, in start/stop/acquire/release/endFrame and the destructor.
//  - The GlExt passed to the factory outlives the backend (vr_module.cpp owns it).
// Guarantees of every backend to the caller:
//  - FrameInfo::render == true => headPose(), eyePose() and eyeFov() are this frame's located,
//    valid poses (OpenXR: views and head located with ORIENTATION_VALID and POSITION_VALID; a frame
//    whose poses are not valid is reported as render = false and submits no layer). The glue
//    auto-recenters on the first rendered frame and relies on this. Without a rendered frame yet,
//    headPose() is identity at (0, eyeHeightM, 0); otherwise the last valid pose.
//  - FrameInfo::lost == true means the session is unusable: the caller stops the backend (it does not
//    throttle: beginFrame keeps reporting lost, without sleeping, until stop()).
//  - eyeSize() is fixed between start() and stop(). acquireEye() may return a different texture
//    name every frame (OpenXR swapchains rotate 2-3 images per eye).
class Backend {
public:
	virtual ~Backend() = default;
	virtual bool start(const StartParams&, std::string& err) = 0;
	virtual void stop() = 0;
	// `now` is the host clock in seconds; the mock uses it as the clock of its head motion.
	virtual FrameInfo beginFrame(double now) = 0;
	virtual Pose headPose() const = 0;
	virtual Pose eyePose(int eye) const = 0;   // 0 = left, 1 = right
	virtual Fov eyeFov(int eye) const = 0;
	virtual glm::ivec2 eyeSize() const = 0;
	virtual unsigned acquireEye(int eye) = 0;   // GL texture name for this frame; 0 if none
	virtual void releaseEye(int eye) = 0;
	virtual void endFrame() = 0;
	// Additive (not part of the frame contract): facts for vr_status. May be called any time between
	// start() and stop(); never touches GL or the runtime.
	virtual BackendInfo info() const { return {}; }
	// Controllers (hand: 0 = left, 1 = right). hand() is the state of the last beginFrame; a backend
	// without controllers reports an inactive hand.
	virtual HandState hand(int) const { return {}; }
	virtual void haptic(int /*hand*/, float /*amplitude*/, float /*seconds*/) {}
	virtual void mockLastHaptic(int /*hand*/, float& amplitude, float& seconds) const {
		amplitude = seconds = 0.0f;
	}
	// Mock only (default: ignored). mockSetHand replaces a hand's raw state; mockSetHeadOffset moves
	// the head offset linearly to `target` over `seconds` of the beginFrame clock (0 = at once), starting
	// at the `now` of the last beginFrame.
	virtual void mockSetHand(int, const HandState&) {}
	virtual void mockSetHeadOffset(glm::vec3 /*target*/, float /*seconds*/) {}
};

// Fake headset. `GlExt` may be null (tests): the mock then never touches GL and acquireEye returns 0.
// StartParams::mockHz <= 0 disables frame pacing in beginFrame (so tests never sleep).
std::unique_ptr<Backend> makeMockBackend(const GlExt*);
// Real OpenXR runtime (backend_openxr.cpp, XR_KHR_opengl_enable on the GL context current at
// start()). `print` (may be null) receives status lines ("VR: ..."); it is only called on the
// engine thread. GlExt must be non-null, or start() fails. FrameInfo: render = shouldRender &&
// session running && poses valid; focused = session FOCUSED; lost = LOSS_PENDING / EXITING /
// instance loss / failed frame calls / the current GL context is not the one the session was
// created with (then no XR or GL call is made; stop() and a new start() are needed); recenter = on
// the first begun frame displayed at/after a REFERENCE_SPACE_CHANGE_PENDING of the app space's type
// (STAGE / LOCAL), and on the first begun frame after the session began again after STOPPING
// (headset off/on). The mock never reports recenter. While the
// session is not running (IDLE) beginFrame sleeps ~10 ms instead of calling xrWaitFrame.
// hand(): from the "gameplay" OpenXR action set (suggested bindings: xr_bindings.hpp), synced and
// located at the predicted display time of each begun FOCUSED frame; inactive on every other frame,
// when xrSyncActions fails, and when the actions could not be set up at start (which does not fail
// start()).
std::unique_ptr<Backend> makeOpenXrBackend(const GlExt*, void (*print)(const char*));

}  // namespace hxvr
