// vr_module.cpp -- hxvr C API entry points, implemented on top of hxvr::Backend.
// Entries not yet implemented by later tasks return neutral values. Must not include engine headers.
#include "vr_api.h"

#include "backend.hpp"
#include "gl_ext.hpp"
#include "targets.hpp"
#include "vr_module_testing.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>

namespace {

// Declaration order is destruction order in reverse: the backend and the eye targets go before
// the GlExt they point to (GlExt must outlive the backend).
hxvr::GlExt g_gl;
bool g_glLoaded = false;
std::unique_ptr<hxvr::EyeTargets> g_targets;
std::unique_ptr<hxvr::Backend> g_backend;
vr_host_t g_host{};

// Frame bookkeeping: guarantees the Backend contract (endFrame after every beginFrame, every
// acquired eye released) even when the engine longjmps (Host_Error) out of a frame.
bool g_framePending = false;
bool g_frameRender = false;   // FrameInfo::render of the pending frame
bool g_eyeAcquired[2] = {false, false};
bool g_eyeBound[2] = {false, false};
bool g_warnedTargets = false;

void zero3(float out[3]) { out[0] = out[1] = out[2] = 0.0f; }

// Copies msg into err, truncating to errlen and always NUL-terminating (when errlen > 0).
void set_error(char *err, int errlen, const std::string &msg) {
	if (!err || errlen <= 0) return;
	const std::size_t n = std::min(msg.size(), static_cast<std::size_t>(errlen) - 1);
	std::memcpy(err, msg.data(), n);
	err[n] = '\0';
}

bool fail(char *err, int errlen, const std::string &msg) {
	set_error(err, errlen, msg);
	if (g_host.print) g_host.print(msg.c_str());
	return false;
}

double host_time() {
	if (g_host.time) return g_host.time();
	using clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

vr_pose_t to_c(const hxvr::Pose &p) {
	return vr_pose_t{{p.pos.x, p.pos.y, p.pos.z}, {p.rot.x, p.rot.y, p.rot.z, p.rot.w}};
}

void print(const std::string &msg) {
	if (g_host.print) g_host.print(msg.c_str());
}

// Releases any acquired eye (unbinding its FBO first) and ends the pending frame.
void finish_frame() {
	if (!g_backend) {
		g_framePending = false;
		return;
	}
	for (int eye = 0; eye < 2; ++eye) {
		if (g_eyeBound[eye] && g_targets) g_targets->unbind();
		g_eyeBound[eye] = false;
		if (g_eyeAcquired[eye]) g_backend->releaseEye(eye);
		g_eyeAcquired[eye] = false;
	}
	if (g_framePending) g_backend->endFrame();
	g_framePending = false;
	g_frameRender = false;
}

}  // namespace

extern "C" {

int  VR_Start(const vr_host_t *host, const vr_start_params_t *p, char *err, int errlen) {
	if (err && errlen > 0) err[0] = '\0';
	VR_Stop();
	g_host = host ? *host : vr_host_t{};
	if (!p) { fail(err, errlen, "VR_Start: no parameters"); return 0; }

	// GL entry points (only with a host GL loader; unit tests run without GL).
	g_glLoaded = false;
	if (g_host.gl_get_proc) {
		g_gl = hxvr::GlExt{};
		std::string glerr;
		if (!g_gl.load(g_host.gl_get_proc, glerr)) {
			fail(err, errlen, glerr);
			return 0;
		}
		g_glLoaded = true;
	}
	const hxvr::GlExt *gl = g_glLoaded ? &g_gl : nullptr;

	const hxvr::StartParams sp{p->render_scale, p->eye_height_m, p->mock_motion != 0, p->mock_hz};
	std::unique_ptr<hxvr::Backend> backend;
	switch (p->backend) {
	case VR_BACKEND_MOCK:
		backend = hxvr::makeMockBackend(gl);
		break;
	case VR_BACKEND_OPENXR:
		fail(err, errlen, "OpenXR backend not available yet");
		return 0;
	default:
		fail(err, errlen, "VR_Start: unknown backend");
		return 0;
	}

	std::string msg;
	if (!backend->start(sp, msg)) {
		fail(err, errlen, msg.empty() ? std::string("backend failed to start") : msg);
		return 0;
	}
	g_backend = std::move(backend);
	if (gl) g_targets = std::make_unique<hxvr::EyeTargets>(gl);
	g_framePending = false;
	g_warnedTargets = false;
	return 1;
}
void VR_Stop(void) {
	if (g_backend) {
		finish_frame();
		g_backend->stop();
		g_backend.reset();
	}
	g_targets.reset();   // GL context is still current (VR stops before the window goes away)
	g_framePending = false;
	g_frameRender = false;
}
int  VR_IsRunning(void) { return g_backend ? 1 : 0; }
int  VR_BeginFrame(vr_frame_t *out) {
	if (out) { out->render = 0; out->focused = 0; out->lost = 0; }
	if (!g_backend) return 0;
	finish_frame();   // a frame left pending (longjmp, or the screen was not drawn) ends first
	const hxvr::FrameInfo fi = g_backend->beginFrame(host_time());
	g_framePending = true;
	g_frameRender = fi.render;
	if (out) { out->render = fi.render; out->focused = fi.focused; out->lost = fi.lost; }
	return 1;
}
void VR_GetHeadPose(vr_pose_t *out) {
	if (!out) return;
	*out = g_backend ? to_c(g_backend->headPose()) : vr_pose_t{{0, 0, 0}, {0, 0, 0, 1}};
}
void VR_GetEye(int eye, vr_pose_t *pose, vr_fov_t *fov) {
	if (pose) *pose = g_backend ? to_c(g_backend->eyePose(eye)) : vr_pose_t{{0, 0, 0}, {0, 0, 0, 1}};
	if (fov) {
		const hxvr::Fov f = g_backend ? g_backend->eyeFov(eye) : hxvr::Fov{0, 0, 0, 0};
		*fov = vr_fov_t{f.left, f.right, f.up, f.down};
	}
}
void VR_GetEyeSize(int *w, int *h) {
	const glm::ivec2 s = g_backend ? g_backend->eyeSize() : glm::ivec2(0);
	if (w) *w = s.x;
	if (h) *h = s.y;
}
int  VR_BeginEye(int eye) {
	// Contract: eyes are acquired only in a frame that renders.
	if (!g_backend || !g_framePending || !g_frameRender || eye < 0 || eye > 1 || g_eyeAcquired[eye]) return 0;
	const unsigned tex = g_backend->acquireEye(eye);
	g_eyeAcquired[eye] = true;
	if (!tex || !g_targets) return 0;
	const glm::ivec2 size = g_backend->eyeSize();
	if (!g_targets->ensure(size) || !g_targets->bind(tex, eye)) {
		if (!g_warnedTargets) print("VR: eye render target unavailable (incomplete framebuffer)");
		g_warnedTargets = true;
		return 0;
	}
	g_eyeBound[eye] = true;
	g_gl.glViewport(0, 0, size.x, size.y);
	return 1;
}
void VR_EndEye(int eye, int mirror_mode, int win_w, int win_h) {
	if (!g_backend || eye < 0 || eye > 1 || !g_eyeAcquired[eye]) return;
	if (g_eyeBound[eye] && g_targets) {
		g_targets->blitToWindow(eye, mirror_mode, win_w, win_h);   // before releaseEye
		g_targets->unbind();
	}
	g_eyeBound[eye] = false;
	g_backend->releaseEye(eye);
	g_eyeAcquired[eye] = false;
}
void VR_EndFrame(void) { finish_frame(); }
void VR_BeginPanel(int, int) {}
void VR_EndPanel(void) {}
void VR_DrawPanelQuad(const float[4][3]) {}

void VR_PanelCorners(const float[3], float, float, float, float, float out[4][3]) {
	for (int i = 0; i < 4; ++i) zero3(out[i]);
}

}  // extern "C"

void VR_TestInjectBackend(std::unique_ptr<hxvr::Backend> backend) {
	VR_Stop();
	g_host = vr_host_t{};
	g_glLoaded = false;
	g_backend = std::move(backend);
	g_framePending = false;
	g_frameRender = false;
}
