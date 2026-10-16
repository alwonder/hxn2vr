// vr_module.cpp -- hxvr C API entry points, implemented on top of hxvr::Backend.
// Entries not yet implemented by later tasks return neutral values. Must not include engine headers.
#include "vr_api.h"

#include "backend.hpp"
#include "gl_ext.hpp"
#include "targets.hpp"
#include "vr_comfort.hpp"
#include "vr_input.hpp"
#include "vr_module_testing.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // wglGetCurrentContext: never delete GL objects of a dead context

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

namespace {

// Declaration order is destruction order in reverse: the backend and the eye targets go before
// the GlExt they point to (GlExt must outlive the backend).
hxvr::GlExt g_gl;
bool g_glLoaded = false;
std::unique_ptr<hxvr::EyeTargets> g_targets;
std::unique_ptr<hxvr::PanelTarget> g_panel;
std::unique_ptr<hxvr::Backend> g_backend;
vr_host_t g_host{};

// Frame bookkeeping: guarantees the Backend contract (endFrame after every beginFrame, every
// acquired eye released) even when the engine longjmps (Host_Error) out of a frame.
bool g_framePending = false;
bool g_frameRender = false;   // FrameInfo::render of the pending frame
bool g_eyeAcquired[2] = {false, false};
bool g_eyeBound[2] = {false, false};
bool g_warnedTargets = false;
bool g_warnedPanel = false;

// GL context current at VR_Start: every GL object the module owns belongs to it. If it is gone (or
// another one is current) when the objects are released, they are leaked instead of deleted.
HGLRC g_ctx = nullptr;

// Frame-rate statistics for vr_status: counts per one-second window, last full window published.
double g_winStart = 0.0;
double g_lastFrame = 0.0;
int g_winFrames = 0;
int g_winRendered = 0;
float g_fps = 0.0f;
float g_renderedFps = 0.0f;
bool g_statsStarted = false;

// Test hook (vr_module_testing.hpp): builds the backend instead of makeMockBackend / makeOpenXrBackend.
std::function<std::unique_ptr<hxvr::Backend>(vr_backend_kind_t)> g_testFactory;

// Copies msg into err, truncating to errlen and always NUL-terminating (when errlen > 0).
void set_error(char *err, int errlen, const std::string &msg) {
	if (!err || errlen <= 0) return;
	const std::size_t n = std::min(msg.size(), static_cast<std::size_t>(errlen) - 1);
	std::memcpy(err, msg.data(), n);
	err[n] = '\0';
}

// VR_Start failures are reported through `err` only: the caller prints it (once).
bool fail(char *err, int errlen, const std::string &msg) {
	set_error(err, errlen, msg);
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

void reset_stats() {
	g_winStart = g_lastFrame = 0.0;
	g_winFrames = g_winRendered = 0;
	g_fps = g_renderedFps = 0.0f;
	g_statsStarted = false;
}

void count_frame(double now, bool rendered) {
	if (!g_statsStarted) {
		g_statsStarted = true;
		g_winStart = now;
	} else if (now - g_winStart >= 1.0) {
		const double span = now - g_winStart;
		g_fps = static_cast<float>(g_winFrames / span);
		g_renderedFps = static_cast<float>(g_winRendered / span);
		g_winStart = now;
		g_winFrames = g_winRendered = 0;
	}
	++g_winFrames;
	if (rendered) ++g_winRendered;
	g_lastFrame = now;
}

void copy_str(char *dst, std::size_t cap, const std::string &s) {
	const std::size_t n = std::min(s.size(), cap - 1);
	std::memcpy(dst, s.data(), n);
	dst[n] = '\0';
}

// Eye and panel targets (and a backend that owns GL objects) belong to the context that was current
// at VR_Start. glDelete* on names of another context would delete someone else's objects: when that
// context is not current any more, the module-owned targets are leaked (the context is gone with them).
bool gl_context_is_ours() { return g_ctx == wglGetCurrentContext(); }

void release_targets() {
	if (!gl_context_is_ours()) {
		(void)g_panel.release();
		(void)g_targets.release();
		return;
	}
	g_panel.reset();
	g_targets.reset();
}

// Static destruction (process exit, e.g. after Sys_Error) must not touch GL if the context is gone.
struct ExitGuard {
	~ExitGuard() {
		if (!gl_context_is_ours()) {
			(void)g_panel.release();
			(void)g_targets.release();
			(void)g_backend.release();
		}
	}
} g_exitGuard;

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
	if (g_panel) g_panel->end();   // a panel pass cut short by a longjmp
	if (g_framePending) g_backend->endFrame();
	g_framePending = false;
	g_frameRender = false;
}

}  // namespace

hxvr::Backend* hxvr::activeBackend() { return g_backend.get(); }

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
	if (g_testFactory) {
		backend = g_testFactory(p->backend);
	} else switch (p->backend) {
	case VR_BACKEND_MOCK:
		backend = hxvr::makeMockBackend(gl);
		break;
	case VR_BACKEND_OPENXR:
		backend = hxvr::makeOpenXrBackend(gl, g_host.print);
		break;
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
	hxvr::inputReset();
	g_ctx = wglGetCurrentContext();
	if (gl) {
		g_targets = std::make_unique<hxvr::EyeTargets>(gl);
		g_panel = std::make_unique<hxvr::PanelTarget>(gl);
	}
	g_framePending = false;
	g_warnedTargets = false;
	g_warnedPanel = false;
	reset_stats();
	return 1;
}
void VR_Stop(void) {
	if (g_backend) {
		finish_frame();
		g_backend->stop();
		g_backend.reset();
	}
	hxvr::inputReset();
	release_targets();   // the GL context is still current: VR stops before the window / context goes away
	g_framePending = false;
	g_frameRender = false;
	for (int eye = 0; eye < 2; ++eye) g_eyeAcquired[eye] = g_eyeBound[eye] = false;
	g_warnedTargets = false;
	g_warnedPanel = false;
	g_ctx = nullptr;
	reset_stats();
}
int  VR_IsRunning(void) { return g_backend ? 1 : 0; }
void VR_GetStatus(vr_status_t *out) {
	if (!out) return;
	*out = vr_status_t{};
	copy_str(out->state, sizeof out->state, "stopped");
	if (!g_backend) return;
	const hxvr::BackendInfo info = g_backend->info();
	const glm::ivec2 size = g_backend->eyeSize();
	out->running = 1;
	copy_str(out->backend, sizeof out->backend, info.name);
	copy_str(out->state, sizeof out->state, info.state);
	copy_str(out->runtime, sizeof out->runtime, info.runtime);
	copy_str(out->format, sizeof out->format, info.swapchainFormat);
	out->eye_w = size.x;
	out->eye_h = size.y;
	for (int h = 0; h < 2; ++h) {
		copy_str(out->hand_profile[h], sizeof out->hand_profile[h], info.profile[h]);
		vr_hand_t hand;
		VR_GetHand(h, &hand);
		out->hand_active[h] = hand.active ? 1 : 0;
	}
	if (host_time() - g_lastFrame < 2.0) {
		out->frames_per_s = g_fps;
		out->rendered_per_s = g_renderedFps;
	}
}
int  VR_BeginFrame(vr_frame_t *out) {
	if (out) { out->render = 0; out->focused = 0; out->lost = 0; out->recenter = 0; }
	if (!g_backend) return 0;
	finish_frame();   // a frame left pending (longjmp, or the screen was not drawn) ends first
	const double now = host_time();
	const hxvr::FrameInfo fi = g_backend->beginFrame(now);
	hxvr::inputUpdate(*g_backend);
	count_frame(now, fi.render);
	g_framePending = true;
	g_frameRender = fi.render;
	if (out) {
		out->render = fi.render;
		out->focused = fi.focused;
		out->lost = fi.lost;
		out->recenter = fi.recenter;
	}
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
int  VR_BeginPanel(int w, int h) {
	if (!g_backend || !g_panel) return 0;
	if (g_panel->begin(glm::ivec2(w, h))) return 1;
	if (!g_panel->active() && !g_warnedPanel) {
		print("VR: 2D panel target unavailable");
		g_warnedPanel = true;
	}
	return 0;
}
void VR_EndPanel(void) {
	if (g_panel) g_panel->end();
}
void VR_DrawPanelQuad(const float corners[4][3]) {
	if (!g_backend || !g_panel || !corners) return;
	g_panel->drawQuad(corners);
}
void VR_DrawFade(float alpha) {
	// only into a bound eye target (between VR_BeginEye and VR_EndEye), with the GL entry points loaded
	if (!g_backend || !g_glLoaded || !(g_eyeBound[0] || g_eyeBound[1])) return;
	hxvr::drawFade(g_gl, alpha);
}

}  // extern "C"

void VR_TestInjectBackend(std::unique_ptr<hxvr::Backend> backend) {
	VR_Stop();
	g_host = vr_host_t{};
	g_glLoaded = false;
	g_backend = std::move(backend);
	hxvr::inputReset();
	g_framePending = false;
	g_frameRender = false;
	g_ctx = wglGetCurrentContext();
	reset_stats();
}

void VR_TestSetBackendFactory(std::function<std::unique_ptr<hxvr::Backend>(vr_backend_kind_t)> factory) {
	g_testFactory = std::move(factory);
}
