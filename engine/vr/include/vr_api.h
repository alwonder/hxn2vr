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
 * last full one-second window (0 when no frame came for 2 s).
 * hand_profile[VR_HAND_*]: the hand's current interaction profile (OpenXR: e.g.
 * "/interaction_profiles/oculus/touch_controller", empty when no controller is bound; mock: always
 * "mock"); hand_active: VR_GetHand(...).active of the last frame.
 * stage_space: 1 if tracking poses are floor-relative from a real floor-level (STAGE) space (the mock: always);
 * 0 if the runtime has only a LOCAL space, where the module shifts the origin down by the start
 * eye height: the head height then starts at that value (vr_eye_height) and says nothing about the user. */
typedef struct {
	int   running;
	char  backend[16];
	char  state[24];
	char  runtime[96];
	char  format[48];
	int   eye_w, eye_h;
	float frames_per_s, rendered_per_s;
	char  hand_profile[2][64];
	int   hand_active[2];
	int   stage_space;
} vr_status_t;

int  VR_Start(const vr_host_t *host, const vr_start_params_t *p, char *err, int errlen);
void VR_Stop(void);
int  VR_IsRunning(void);
void VR_GetStatus(vr_status_t *out);   /* out may be NULL (no-op) */
int  VR_BeginFrame(vr_frame_t *out);
void VR_GetHeadPose(vr_pose_t *out);
void VR_GetEye(int eye, vr_pose_t *pose, vr_fov_t *fov);
void VR_GetEyeSize(int *w, int *h);

/* Controllers. Poses are in metres, tracking space (no vr_height_offset). */
enum { VR_HAND_LEFT = 0, VR_HAND_RIGHT = 1 };
#define VR_BTN_TRIGGER 0x01u   /* from trigger value: press >= 0.55, release <= 0.35 */
#define VR_BTN_GRIP    0x02u   /* from squeeze value, same hysteresis */
#define VR_BTN_LOWER   0x04u   /* A (right) / X (left) */
#define VR_BTN_UPPER   0x08u   /* B / Y */
#define VR_BTN_STICK   0x10u   /* thumbstick click */
#define VR_BTN_MENU    0x20u
typedef struct {
	int active;            /* a controller is bound and its pose action is active */
	int pose_valid;        /* grip and aim located this frame (orientation and position valid) */
	vr_pose_t grip, aim;   /* metres, tracking space (no vr_height_offset) */
	float trigger, squeeze;   /* 0..1 */
	float stick[2];        /* x right, y up, -1..1, raw (no deadzone) */
	unsigned buttons;      /* VR_BTN_* */
} vr_hand_t;
/* State of the last VR_BeginFrame; all zero when not running or when the hand is inactive. */
void VR_GetHand(int hand, vr_hand_t *out);
/* Mock backend only (ignored by OpenXR): replaces a hand's raw state (buttons: only LOWER/UPPER/STICK/MENU
 * are taken; TRIGGER/GRIP come from trigger/squeeze) / moves the head offset linearly to `target` over
 * `seconds` (0 = at once) of host time. The move starts at the host time of the last VR_BeginFrame. */
void VR_MockSetHand(int hand, const vr_hand_t *in);
void VR_MockSetHeadOffset(const vr_vec3_t *target, float seconds);

enum { VR_DIR_NONE = 0, VR_DIR_UP, VR_DIR_DOWN, VR_DIR_LEFT, VR_DIR_RIGHT };
typedef struct { int dir; float held, next; } vr_repeat_t;
/* Menu navigation from a stick: a direction starts when its dominant axis reaches 0.6 (emitted at once),
 * stays while that axis keeps the same sign at >= 0.3, repeats after 0.45 s held and then every 0.12 s;
 * returns the direction emitted this call (VR_DIR_NONE most calls).
 * dt (seconds since the previous call) counts as at most 0.1 s; NaN/negative dt counts as 0, NaN axes
 * as 0. A held direction is kept until its own axis drops below 0.3 (no switch on a diagonal); |x| == |y|
 * starts the vertical direction. */
int VR_StickRepeat(vr_repeat_t *r, const float stick[2], float dt);

/* Stick locomotion and turning (pure functions). */
/* Radial deadzone: |in| <= deadzone gives (0,0), above it the length is rescaled so that deadzone..1 maps
 * to 0..1 (|out| <= 1, direction kept). Non-finite input gives (0,0); a non-finite or out-of-range deadzone
 * counts as 0 / 0.99. out may be in. */
void  VR_StickDeadzone(const float in[2], float deadzone, float out[2]);
/* World yaw (degrees, not normalized) to move along: mode 0 = body_yaw + head_yaw; mode 1 = body_yaw + yaw
 * of the hand's aim forward projected on the floor, falling back to the head when !hand_valid, hand_aim
 * is NULL / not a rotation or the projection is shorter than 0.3 (a pose pointing up or down). */
float VR_MoveYaw(int mode, const vr_quat_t *hand_aim, int hand_valid, float head_yaw, float body_yaw);
/* stick (after deadzone: x right, y forward) along move_yaw, expressed in the frame of view_yaw (Quake yaw,
 * degrees): out[0] forwardmove, out[1] sidemove (> 0 = right), both scaled by speed. */
void  VR_StickToMove(const float stick[2], float move_yaw, float view_yaw, float speed, float out[2]);
typedef struct { int fired; } vr_turn_t;
/* yaw change in degrees (Quake yaw: positive = left, so a stick to the right turns negative). Snap: fires
 * once (snap_deg) at |x| >= 0.7, re-armed when |x| < 0.3. Smooth: -deadzoned(x) * speed_dps * dt (dt < 0
 * counts as 0). 0 for non-finite input. */
float VR_TurnStep(vr_turn_t *t, float stick_x, float dt, int smooth, float snap_deg, float speed_dps, float deadzone);

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

/* Body model and calibration (pure functions). Everything is in metres of tracking space with the floor at 0:
 * none of it takes vr_world_scale (the body does not scale with the world). */
typedef struct { float eye_height, arm_length, shoulder_width, crouch_fraction; } vr_body_params_t;   /* 0 = derive */
typedef struct {
	float eye_height, stature, shoulder_height, shoulder_width, arm_length,
	      chest_height, waist_height, crouch_height;
} vr_body_t;
/* stature = eye / 0.936; shoulder_height 0.818 S; shoulder_width 0.259 S; arm_length 0.40 S; chest_height
 * 0.72 S; waist_height 0.60 S; crouch_height = crouch_fraction (<= 0 or non-finite: 0.65) * eye. An
 * override (arm_length, shoulder_width) <= 0 or non-finite is derived. Returns 0 and leaves *out untouched
 * unless p and out are set and 0.5 <= eye_height <= 2.5 (finite). */
int VR_BodyFromParams(const vr_body_params_t *p, vr_body_t *out);
/* 1 and *out = head_y if 0.5 <= head_y <= 2.5 (finite), else 0 (*out untouched). */
int VR_CalibrateEyeHeight(float head_y, float *out);
/* Arm length from a hand position held straight ahead at shoulder height. The shoulder is
 * (head.x, b->shoulder_height, head.z) +/- b->shoulder_width / 2 along the head's horizontal right axis
 * (hand VR_HAND_RIGHT: +, VR_HAND_LEFT: -); arm = |hand_pos - shoulder|. Returns 1 and *out = arm if
 * 0.4 <= arm <= 1.0 (finite), else 0 (*out untouched). head / hand_pos in the same (corrected) space. */
int VR_CalibrateArm(const vr_body_t *b, const vr_pose_t *head, const vr_vec3_t *hand_pos, int hand, float *out);
/* The distance VR_CalibrateArm measures, without the range check (for the "out of range" message); -1 if
 * it cannot be computed (NULL or non-finite input, head rotation not a rotation). */
float VR_ArmDistance(const vr_body_t *b, const vr_pose_t *head, const vr_vec3_t *hand_pos, int hand);
/* Physical crouch with hysteresis: enter when head_y < b->crouch_height, leave when
 * head_y > b->crouch_height + 0.05 * b->eye_height; otherwise the state stays (also for a non-finite
 * head_y). 0 for a bad body (NULL, eye_height not > 0 or crouch_height not finite). head_y: tracked,
 * metres above the floor (with vr_height_offset). */
int   VR_CrouchUpdate(int crouched, float head_y, const vr_body_t *b);

/* Comfort (pure functions, except VR_DrawFade). */
/* Scale of the camera height above the feet: 1 when physically crouched (the real head is already low);
 * else clamp(viewheight / 50, 0.2, 1) (button crouch, death, polymorph: the game's view height, 50 =
 * standing). 1 for a non-finite viewheight. */
float VR_CameraHeightScale(float viewheight, int physical_crouch);
/* Moves cur toward target by at most rate_per_s * dt, no overshoot. cur unchanged for a non-finite or
 * non-positive step (dt NaN / <= 0, rate <= 0) or a non-finite target; target for a non-finite cur. */
float VR_Approach(float cur, float target, float rate_per_s, float dt);
/* Black over the whole bound eye at alpha (clamped to 0..1; nothing drawn at <= 0 or NaN), alpha-blended,
 * depth test and writes off; every GL state it touches (enables, blend, depth mask, colour, matrix mode
 * and both matrices) is restored. Call it between VR_BeginEye and VR_EndEye (no-op otherwise). */
void  VR_DrawFade(float alpha);

/* Comfort vignette. NDC radius of the clear area: 1.5 - 0.9 * clamp(strength) * clamp(amount) (a non-finite
 * argument counts as 0); >= 1.45 means nothing to draw. */
float VR_VignetteInner(float strength, float amount);
/* NDC of the view direction in an eye with this (asymmetric) FOV: x = -(tan r + tan l) / (tan r - tan l),
 * the same for y with up / down; (0, 0) for a NULL or degenerate fov. */
void  VR_VignetteCenter(const vr_fov_t *fov, float out[2]);
/* Black ring around the eye's view centre (VR_VignetteCenter of that eye's FOV): alpha 0 at `inner`, 1 at
 * inner + 0.35, solid black out to radius 3; 48 segments, immediate mode, GL state restored as in
 * VR_DrawFade. Nothing for inner >= 1.45. Call it between VR_BeginEye and VR_EndEye (no-op otherwise);
 * after the world, before VR_DrawFade and the panel. */
void  VR_DrawVignette(int eye, float inner);

/* Room-scale on the client (pure functions). The head leans away from the body's centre (the feet) in world
 * axes, so turning the body does not move the head; past the radius the body should walk the excess. */
typedef struct { float lean[2]; vr_vec3_t last_head; int has_last; } vr_room_t;   /* lean: Quake units, world x/y */
void VR_RoomReset(vr_room_t *r);   /* lean 0, forget the last head */
/* Integrates the head's horizontal motion since the previous call (tracking metres, rotated by body_yaw,
 * * upm) into the lean; a step longer than 0.5 m is not a step (tracking jump). Past radius_units the
 * excess goes to out_excess[2] (world units; the body should walk it) and the lean keeps radius_units.
 * out_excess is set on every call ((0,0) when there is none). The first call after VR_RoomReset (and any
 * call with a non-finite head) only takes the head; a non-finite head is not kept. A non-finite body_yaw
 * or upm, or upm <= 0, is no step (the head is kept); a negative or non-finite radius counts as 0. */
void VR_RoomStep(vr_room_t *r, const vr_vec3_t *head, float body_yaw, float upm, float radius_units, float out_excess[2]);

/* VR move block: the client's VR data for its local server, sent after the move in its own message
 * (clc_hxvr, u8 len, len bytes of the block); the protocol number does not change. */
#define VR_MOVEBLOCK_VERSION 2
#define VR_MOVEBLOCK_MAX     64        /* bytes on the wire, length-prefixed */
#define VR_MOVE_ROOMSCALE    0x01u
#define VR_MOVE_WEAPON       0x02u    /* muzzle and aim valid (main hand tracked) */
typedef struct {
	int version; unsigned flags; float roomscale[2];
	float muzzle[3];   /* world units relative to the player's feet */
	float aim[3];      /* Quake pitch, yaw, roll in degrees */
} vr_moveblock_t;
/* v1 layout: u8 version, u8 flags, f32 LE roomscale x, f32 LE roomscale y (10 bytes). Returns bytes written,
 * v2 appends muzzle and aim (six f32 LE), for 34 bytes. Write: 0 if capacity is insufficient.
 * Read: 1 = ok; 0 if len < 10, version 0, or a non-finite float in the recognized prefix.
 * Version 1 or len 10..33 clears WEAPON and zeros muzzle/aim. Version >= 2 with len >= 34 reads
 * the v2 prefix; trailing bytes are ignored and the version is reported as sent.
 * Also 0 for NULL arguments; Write: 0 for a version outside 1..255 (flags: the low 8 bits are sent);
 * Read: *out is untouched unless it returns 1. */
int VR_WriteMoveBlock(const vr_moveblock_t *b, unsigned char *buf, int cap);
int VR_ReadMoveBlock(const unsigned char *buf, int len, vr_moveblock_t *out);
/* server sanity: finite, |muzzle| <= 96 world units (0 for NULL) */
int VR_MuzzleOffsetOk(const float muzzle[3]);
/* server sanity: finite, and |accumulated move| <= 64 units (0 for NULL) */
int VR_RoomscaleMoveOk(const float move[2]);

/* Stock-progs compatibility: origin + proj_ofs must equal the muzzle. */
void VR_CompatOrigin(const float muzzle[3], const float proj_ofs[3], float out[3]);
/* No obstruction: muzzle unchanged. Otherwise hit is the first wall on the body/muzzle/aim probe;
 * place the muzzle clearance units before hit along start->hit, never behind start.
 * The caller traces start->muzzle, muzzle->muzzle+aim*clearance, and validates start->hit. */
void VR_CompatClampMuzzle(const float start[3], const float muzzle[3], const float hit[3],
                          int blocked, float clearance, float out[3]);
/* Restore real + (after - swapped), preserving movement made by progs. */
void VR_CompatRestore(const float real[3], const float swapped[3], const float after[3], float out[3]);
/* Fresh age in [0, 0.1], alive, WALK/FLY/NOCLIP/SWIM, outside intermission/camera. */
int VR_CompatApplies(int enabled, double block_age, float health, int deadflag, int movetype,
                     int intermission, int camera);

/* Weapon entries: offsets and view-convention angles are in the hand frame.
 * hand_ang, entry.angles and aim_ang use Quake VIEW convention (degrees):
 * R(a) = Rz(yaw) Ry(pitch) Rx(+roll). s = entry.scale * scale_units.
 * model_org = hand_org + R(hand_ang) * (s * offset).
 * R_model = R(hand_ang) * R(entry.angles); scale is about the model origin.
 * muzzle = model_org + R_model * (s * (maxs.x, mid_y, mid_z)).
 * model_ang uses Quake ENTITY convention: (-aim_pitch, aim_yaw, -aim_roll),
 * rendered as Rz(model_yaw) Ry(-model_pitch) Rx(-model_roll).
 * Models are already world units: pass scale_units = 1.
 * Get defaults to -bbox centre, zero angles, scale 1; enumeration is overrides only.
 * Name pointers remain valid until the next Set or Clear. Clear removes all overrides.
 * Set and Format reject empty names or names containing a double quote, and
 * non-finite entries or scale <= 0. Format returns length, or 0 on failure. */
typedef struct { float offset[3]; float angles[3]; float scale; } vr_weapon_entry_t;
/* Inverse placement for scale_units = 1; model_ang is ENTITY convention.
 * scale is the entry scale (positive); output angles use VIEW convention. */
void VR_WeaponFromPose(const float hand_org[3], const float hand_ang[3], const float model_org[3],
                       const float model_ang[3], float scale, vr_weapon_entry_t *out);
void VR_WeaponGet(const char *model, const float mins[3], const float maxs[3], vr_weapon_entry_t *out);
int VR_WeaponSet(const char *model, const vr_weapon_entry_t *e);
int VR_WeaponCount(void);
void VR_WeaponClear(void);
const char *VR_WeaponName(int i);
int VR_WeaponFormat(const char *model, const vr_weapon_entry_t *e, char *buf, int cap);
void VR_WeaponTransform(const float hand_org[3], const float hand_ang[3], const vr_weapon_entry_t *e,
                        const float mins[3], const float maxs[3], float scale_units,
                        float model_org[3], float model_ang[3], float muzzle[3], float aim_ang[3]);

/* pure math */
float VR_UnitsPerMetre(float eye_height_m, float world_scale);
void  VR_TrackingToQuakePos(const vr_vec3_t *v, float upm, float out[3]);
void  VR_TrackingToQuakeAngles(const vr_quat_t *q, float out[3]);
/* eye = feet + lean + rot(body_yaw, quake(eye_pos - head_pos).xy * upm); z = feet.z + eye_pos.y * upm * height_scale
 * (head_pos.y is ignored: the height is absolute, above the floor) */
void  VR_EyeOrigin(const float feet[3], float body_yaw, const float lean[2], const vr_vec3_t *head_pos,
                   const vr_vec3_t *eye_pos, float upm, float height_scale, float out[3]);
/* 1 if the feet moved more than 64 units horizontally since the previous frame (teleport, respawn, load);
 * also 1 for a non-finite input */
int   VR_BodyJumped(const float prev_feet[3], const float feet[3]);
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
