// vr_module.cpp -- hxvr C API entry points. Stubs return neutral values until implemented
// by later tasks. Must not include engine headers.
#include "vr_api.h"

#include <cstring>

namespace {
void set_identity(float out[16]) {
	std::memset(out, 0, sizeof(float) * 16);
	out[0] = out[5] = out[10] = out[15] = 1.0f;
}
void zero3(float out[3]) { out[0] = out[1] = out[2] = 0.0f; }
}  // namespace

extern "C" {

int  VR_Start(const vr_host_t *, const vr_start_params_t *, char *err, int errlen) {
	if (err && errlen > 0) err[0] = '\0';
	return 0;
}
void VR_Stop(void) {}
int  VR_IsRunning(void) { return 0; }
int  VR_BeginFrame(vr_frame_t *out) {
	if (out) { out->render = 0; out->focused = 0; out->lost = 0; }
	return 0;
}
void VR_GetHeadPose(vr_pose_t *out) {
	if (out) *out = vr_pose_t{{0, 0, 0}, {0, 0, 0, 1}};
}
void VR_GetEye(int, vr_pose_t *pose, vr_fov_t *fov) {
	if (pose) *pose = vr_pose_t{{0, 0, 0}, {0, 0, 0, 1}};
	if (fov) *fov = vr_fov_t{0, 0, 0, 0};
}
void VR_GetEyeSize(int *w, int *h) {
	if (w) *w = 0;
	if (h) *h = 0;
}
void VR_BeginEye(int) {}
void VR_EndEye(int, int, int, int) {}
void VR_EndFrame(void) {}
void VR_BeginPanel(int, int) {}
void VR_EndPanel(void) {}
void VR_DrawPanelQuad(const float[4][3]) {}

void VR_ProjectionMatrix(const vr_fov_t *, float, float, float out[16]) { set_identity(out); }
void VR_QuakeViewMatrix(const float[3], const float[3], float out[16]) { set_identity(out); }
void VR_FrustumNormals(const float[3], const vr_fov_t *, float out[4][3]) {
	for (int i = 0; i < 4; ++i) zero3(out[i]);
}
int  VR_SimTicks(vr_simclock_t *, double, int) { return 0; }
void VR_ComposeViewAngles(float, const float[3], float out[3]) { zero3(out); }
float VR_BodyYawFromSetAngle(float, float) { return 0.0f; }
void VR_PanelCorners(const float[3], float, float, float, float, float out[4][3]) {
	for (int i = 0; i < 4; ++i) zero3(out[i]);
}

}  // extern "C"
