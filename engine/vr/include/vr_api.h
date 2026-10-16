/*
 * vr_api.h -- C API of the hxvr module (6DoF OpenXR support for hxn2vr).
 *
 * This is the ONLY bridge between the C engine and the C++ module: valid C and C++.
 * Units: metres, OpenXR axes (x right, y up, -z forward), relative to the floor,
 * unless a function says otherwise.
 */
#ifndef HXVR_VR_API_H
#define HXVR_VR_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { float x, y, z; } vr_vec3_t;
typedef struct { float x, y, z, w; } vr_quat_t;
typedef struct { vr_vec3_t pos; vr_quat_t rot; } vr_pose_t;   /* metres, OpenXR axes, relative to the floor */
typedef struct { float left, right, up, down; } vr_fov_t;      /* radians, left/down < 0 */

typedef struct {
	void   (*print)(const char *msg);
	void  *(*gl_get_proc)(const char *name);
	double (*time)(void);
} vr_host_t;

typedef enum { VR_BACKEND_OPENXR = 0, VR_BACKEND_MOCK = 1 } vr_backend_kind_t;

typedef struct {
	vr_backend_kind_t backend;
	float render_scale;
	float eye_height_m;
	int mock_motion;
	float mock_hz;
} vr_start_params_t;

/* recenter: the runtime moved the tracking origin (OpenXR: reference space change, or the session
 * began again after it stopped -- headset off/on) since the previous VR_BeginFrame; the caller re-arms
 * its automatic recenter (done on the next frame with render != 0). Never set by the mock. */
typedef struct { int render; int focused; int lost; int recenter; } vr_frame_t;
typedef struct { double acc; } vr_simclock_t;

/* Snapshot for the vr_status command. All strings are NUL-terminated and empty when not applicable.
 * stopped: running 0, state "stopped", everything else zero. state is the backend's session state
 * (mock: "focused"; OpenXR: idle, ready, synchronized, visible, focused, stopping, lost).
 * frames_per_s / rendered_per_s: VR_BeginFrame calls / frames with render != 0 per second over the
 * last full one-second window (0 when no frame came for 2 s). */
typedef struct {
	int   running;
	char  backend[16];
	char  state[24];
	char  runtime[96];
	char  format[48];
	int   eye_w, eye_h;
	float frames_per_s, rendered_per_s;
} vr_status_t;

int  VR_Start(const vr_host_t *host, const vr_start_params_t *p, char *err, int errlen);
void VR_Stop(void);
int  VR_IsRunning(void);
void VR_GetStatus(vr_status_t *out);   /* out may be NULL (no-op) */
int  VR_BeginFrame(vr_frame_t *out);
void VR_GetHeadPose(vr_pose_t *out);
void VR_GetEye(int eye, vr_pose_t *pose, vr_fov_t *fov);
void VR_GetEyeSize(int *w, int *h);
/* Per frame, after VR_BeginFrame returned render != 0, for eye 0 then 1:
 * VR_BeginEye acquires the eye image, binds its framebuffer and sets the viewport; returns 1 if
 * the eye target is bound (draw into it), 0 if not (do not draw). VR_EndEye mirrors the eye into
 * the window framebuffer (mirror_mode 0 none, 1 eye 0 fills the window, 2 both side by side;
 * win_w x win_h pixels), rebinds the window framebuffer and releases the eye.
 * VR_EndFrame ends the frame; it must follow every VR_BeginFrame (also when not rendering). A
 * frame left pending is ended by the next VR_BeginFrame / VR_Stop. */
int  VR_BeginEye(int eye);
void VR_EndEye(int eye, int mirror_mode, int win_w, int win_h);
void VR_EndFrame(void);
/* The engine's 2D layer is drawn into an offscreen w x h RGBA8 panel texture (any frame; the panel
 * is independent of the eyes): VR_BeginPanel binds the panel framebuffer, clears it to (0,0,0,0)
 * and returns 1 (0 = unavailable: do not draw); VR_EndPanel rebinds whatever framebuffer and
 * viewport were bound before (no-op without a panel pass; a frame ended by VR_EndFrame /
 * VR_BeginFrame / VR_Stop also ends it). VR_DrawPanelQuad draws the panel texture as an
 * alpha-blended quad (corners: top-left, top-right, bottom-right, bottom-left, e.g. from
 * VR_PanelCorners) in the currently bound framebuffer with the currently loaded matrices; depth
 * test and writes are off (the panel is on top of the world) and every GL state it touches is
 * restored. Call it between VR_BeginEye and VR_EndEye. */
int  VR_BeginPanel(int w, int h);
void VR_EndPanel(void);
void VR_DrawPanelQuad(const float corners[4][3]);

/* pure math */
float VR_UnitsPerMetre(float eye_height_m, float world_scale);
void  VR_TrackingToQuakePos(const vr_vec3_t *v, float upm, float out[3]);
void  VR_TrackingToQuakeAngles(const vr_quat_t *q, float out[3]);
void  VR_EyeOrigin(const float feet[3], float body_yaw, const vr_vec3_t *eye_pos,
                   const vr_vec3_t *anchor, float upm, float out[3]);
void  VR_ProjectionMatrix(const vr_fov_t *fov, float znear, float zfar, float out[16]);
void  VR_QuakeViewMatrix(const float org[3], const float angles[3], float out[16]);
void  VR_FrustumNormals(const float angles[3], const vr_fov_t *fov, float out[4][3]);
int   VR_SimTicks(vr_simclock_t *c, double dt, int paused);
void  VR_ComposeViewAngles(float body_yaw, const float head_angles[3], float out[3]);
float VR_BodyYawFromSetAngle(float new_yaw, float head_yaw);
void  VR_PanelCorners(const float center[3], float yaw, float pitch, float width_units,
                      float aspect, float out[4][3]);

#ifdef __cplusplus
}
#endif

#endif /* HXVR_VR_API_H */
