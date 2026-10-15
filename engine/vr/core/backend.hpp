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
struct FrameInfo { bool render; bool focused; bool lost; };
struct StartParams { float renderScale; float eyeHeightM; bool mockMotion; float mockHz; };

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
};

// Fake headset. `GlExt` may be null (tests): the mock then never touches GL and acquireEye returns 0.
// StartParams::mockHz <= 0 disables frame pacing in beginFrame (so tests never sleep).
std::unique_ptr<Backend> makeMockBackend(const GlExt*);
// Real OpenXR runtime. Declared here; defined by the OpenXR backend task (not yet linked).
std::unique_ptr<Backend> makeOpenXrBackend(const GlExt*, void (*print)(const char*));

}  // namespace hxvr
