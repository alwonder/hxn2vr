// vr_module.cpp -- hxvr C API entry points, implemented on top of hxvr::Backend.
// Entries not yet implemented by later tasks return neutral values. Must not include engine headers.
#include "vr_api.h"

#include "backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>

namespace {

std::unique_ptr<hxvr::Backend> g_backend;
vr_host_t g_host{};

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

}  // namespace

extern "C" {

int  VR_Start(const vr_host_t *host, const vr_start_params_t *p, char *err, int errlen) {
	if (err && errlen > 0) err[0] = '\0';
	VR_Stop();
	g_host = host ? *host : vr_host_t{};
	if (!p) { fail(err, errlen, "VR_Start: no parameters"); return 0; }

	const hxvr::StartParams sp{p->render_scale, p->eye_height_m, p->mock_motion != 0, p->mock_hz};
	std::unique_ptr<hxvr::Backend> backend;
	switch (p->backend) {
	case VR_BACKEND_MOCK:
		backend = hxvr::makeMockBackend(nullptr);
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
	return 1;
}
void VR_Stop(void) {
	if (g_backend) {
		g_backend->stop();
		g_backend.reset();
	}
}
int  VR_IsRunning(void) { return g_backend ? 1 : 0; }
int  VR_BeginFrame(vr_frame_t *out) {
	if (out) { out->render = 0; out->focused = 0; out->lost = 0; }
	if (!g_backend) return 0;
	const hxvr::FrameInfo fi = g_backend->beginFrame(host_time());
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
void VR_BeginEye(int) {}
void VR_EndEye(int, int, int, int) {}
void VR_EndFrame(void) {}
void VR_BeginPanel(int, int) {}
void VR_EndPanel(void) {}
void VR_DrawPanelQuad(const float[4][3]) {}

int  VR_SimTicks(vr_simclock_t *, double, int) { return 0; }
void VR_ComposeViewAngles(float, const float[3], float out[3]) { zero3(out); }
float VR_BodyYawFromSetAngle(float, float) { return 0.0f; }
void VR_PanelCorners(const float[3], float, float, float, float, float out[4][3]) {
	for (int i = 0; i < 4; ++i) zero3(out[i]);
}

}  // extern "C"
