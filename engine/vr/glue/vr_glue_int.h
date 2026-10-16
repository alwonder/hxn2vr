/*
 * vr_glue_int.h -- state and functions shared between the glue's own .c files (vr_glue.c,
 * vr_glue_input.c). Not for engine code: the engine includes vr_glue.h only.
 * Include after quakedef.h, vr_api.h and vr_glue.h.
 */
#ifndef HXVR_VR_GLUE_INT_H
#define HXVR_VR_GLUE_INT_H

/* ---- VR session state (vr_glue.c) ---- */

extern vr_frame_t	vrg_frame;		/* flags of the last VR_BeginFrame */
extern qboolean		vrg_active;		/* running && last frame had render != 0 */

/* body yaw (degrees; not normalized: after VRG_PostInput it is in [0, 360) via anglemod, every
 * consumer normalizes): the view yaw is body_yaw + head yaw. Keyboard/mouse turn the body; the head
 * adds its own rotation. Where the head stands is the room-scale lean (vrg_room, vr_glue_body.c). */
extern float		vrg_body_yaw;
extern qboolean		vrg_body_valid;		/* body_yaw taken from cl.viewangles since VR started */

/* vr_eye_height (metres) and vr_world_scale in use (VRG_CheckScaleCvars): the last valid values */
extern float		vrg_eye_height, vrg_world_scale;

/* ---- controller input and mock dev commands (vr_glue_input.c) ---- */

void VRG_InputInit (void);	/* VRG_Init: registers vr_mainhand, vr_bind_defaults, vr_mock_hand, vr_mock_head */
void VRG_InputStart (void);	/* VRG_Start, after VR_Start succeeded: forgets all button state, resets the
				 * mock hands, sets the default bindings if no VR key is bound */
/* VRG_HostFrameBegin, after VR_BeginFrame (VR running): turns button edges of both hands into
 * Key_Event calls routed by key_dest, and stick directions into menu/console arrows. */
void VRG_InputFrame (void);
/* Releases every key a controller button holds down (Key_Event(code, false) with the code it was
 * pressed with). VRG_Stop, and VRG_InputFrame when vr_mainhand changed. */
void VRG_ReleaseAllKeys (void);
/* VRG_PreInput: yaw change (degrees, Quake yaw) from the main hand's stick this frame (snap or smooth
 * turn); 0 outside the running game. */
float VRG_TurnStep (void);
/* true while VRG_ModalFrame runs its frame: VRG_InputFrame then maps the buttons to the dialog's
 * 'y' / 'n' instead of the key_dest routing, and skips the menu stick arrows */
extern qboolean		vrg_input_modal;

/* ---- body model, tracked poses, calibration (vr_glue_body.c) ---- */

extern vr_body_t	vrg_body;		/* current body model (metres), from vr_eye_height and the vr_body_* cvars */
extern float		vrg_height_offset;	/* vr_height_offset in use (metres) */

void VRG_BodyInit (void);		/* VRG_Init: registers the body cvars and vr_calibrate, vr_body */
/* The head's, the eyes' and the hands' poses with vr_height_offset added to their y (and nothing else
 * changed: rotations stay). The ONLY place that applies the offset: renderer, panel, hands, crouch and
 * room-scale take their poses from here. Any argument may be NULL. Poses of inactive hands stay zero. */
void VRG_TrackedPoses (vr_pose_t *head, vr_pose_t eyes[2], vr_hand_t hands[2]);
/* A button of a hand went down (role 0 = main hand, 1 = other): true if vr_calibrate used it, then no
 * key is sent for it. */
qboolean VRG_CalibratePress (int hand, int role, unsigned bit);
qboolean VRG_Calibrating (void);
void VRG_CalibrateFrame (void);		/* every frame while VR runs: keeps the prompt on the screen */
void VRG_CalibrateReset (void);		/* VR starts / stops: drops a calibration in progress */

/* Room-scale on the client: the head's lean from the feet (Quake units, world x/y; the eyes are placed at
 * feet + lean + their offset from the head) and the excess past vr_roomscale_radius that the body should
 * walk (world units): summed over the rendered frames until VRG_WriteMoveBlock sends it to the local
 * server (the block consumes it), dropped whenever the server would not advance the player. */
extern vr_room_t	vrg_room;
extern float		vrg_room_excess[2];
void VRG_RoomReset (void);		/* lean 0, forget the last head, drop the excess not yet sent */
void VRG_RoomStart (void);		/* VRG_Start: VRG_RoomReset and forget the previous session's feet and world */
/* VRG_UpdateScreen, once per rendered frame before the eyes are placed (feet: r_refdef.vieworg -
 * cl.viewheight after V_SetupRefdef; ignored without a world). Steps the lean; resets it on a new
 * cl.worldmodel, when the world comes back (after a frame without one) and when the feet jumped
 * (VR_BodyJumped). Without a world the head moves freely (no radius); over a world the lean stays within
 * vr_roomscale_radius, and the excess is added to vrg_room_excess only while VRG_RoomWalking. */
void VRG_RoomFrame (qboolean world, const vec3_t feet, const vr_vec3_t *head, float upm);
/* true while the local server advances this player for a room-scale move: VR runs, a local server with
 * a world, connected and signed on, no demo, not paused (client or server), not in intermission, key_game,
 * the headset focused and rendering (VRG_ServerTicks runs ticks), vr_roomscale on */
qboolean VRG_RoomWalking (void);
float VRG_HeightScale (void);		/* eye height scale above the feet (game crouch): 1 until Task 2.8 */

/* ---- from vr_glue.c, for vr_glue_body.c ---- */

void VRG_CheckScaleCvars (void);	/* re-reads vr_eye_height / vr_world_scale into vrg_eye_height / vrg_world_scale */
void VRG_Recenter (void);		/* lean 0 (the head is over the feet), panel placed again (prints "VR: recentered") */

#endif /* HXVR_VR_GLUE_INT_H */
