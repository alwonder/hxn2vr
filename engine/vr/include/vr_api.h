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

typedef struct { int render; int focused; int lost; } vr_frame_t;
typedef struct { double acc; } vr_simclock_t;

int  VR_Start(const vr_host_t *host, const vr_start_params_t *p, char *err, int errlen);
void VR_Stop(void);
int  VR_IsRunning(void);
int  VR_BeginFrame(vr_frame_t *out);
void VR_GetHeadPose(vr_pose_t *out);
void VR_GetEye(int eye, vr_pose_t *pose, vr_fov_t *fov);
void VR_GetEyeSize(int *w, int *h);
void VR_BeginEye(int eye);
void VR_EndEye(int eye, int mirror_mode, int win_w, int win_h);
void VR_EndFrame(void);
void VR_BeginPanel(int w, int h);
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
