/*
 * vr_glue.h -- engine-side glue for the hxvr VR module (C, part of the engine).
 * Include after quakedef.h. The engine talks to the module only through
 * engine/vr/include/vr_api.h; this header is what engine code calls.
 */
#ifndef HXVR_VR_GLUE_H
#define HXVR_VR_GLUE_H

/* VR cvars, registered by VRG_Init (all prefixed vr_). */
extern	cvar_t	vr_enabled;
extern	cvar_t	vr_backend;
extern	cvar_t	vr_render_scale;
extern	cvar_t	vr_world_scale;
extern	cvar_t	vr_eye_height;
extern	cvar_t	vr_nearclip;
extern	cvar_t	vr_mirror;
extern	cvar_t	vr_panel_distance;
extern	cvar_t	vr_panel_width;
extern	cvar_t	vr_mock_motion;
extern	cvar_t	vr_mock_hz;
extern	cvar_t	vr_debug_ticks;
extern	cvar_t	vr_mainhand;		/* vr_glue_input.c: 0 left, 1 right (= VR_HAND_*) */
extern	cvar_t	vr_movement_mode;	/* 0 head, 1 aim of the off hand */
extern	cvar_t	vr_turn_mode;		/* 0 snap, 1 smooth */
extern	cvar_t	vr_snap_angle;
extern	cvar_t	vr_turn_speed;
extern	cvar_t	vr_stick_deadzone;
extern	cvar_t	vr_vignette;		/* 1: comfort vignette while the sticks move / smooth-turn */
extern	cvar_t	vr_vignette_strength;	/* 0..1 */
extern	cvar_t	vr_seated;		/* vr_glue_body.c: 1 = seated mode */
extern	cvar_t	vr_height_offset;	/* metres, added to the y of head, eyes and hands (VRG_TrackedPoses) */
extern	cvar_t	vr_body_arm_length;	/* metres, 0 = from the proportions */
extern	cvar_t	vr_body_shoulder_width;
extern	cvar_t	vr_crouch_threshold;	/* fraction of vr_eye_height */
extern	cvar_t	vr_crouch_physical;	/* 1: crouching for real crouches in the game */
extern	cvar_t	vr_head_collision;	/* 1: head kept under the ceiling, fade when it is in a wall */

void VRG_Init (void);		/* Host_Init, before config.cfg/hexen.rc are executed: registers cvars */
void VRG_Shutdown (void);	/* Host_Shutdown: stops the module if running */
void VRG_BeforeVideoRestart (void);	/* VID_ChangeVideoMode, before the GL context is deleted: stops VR (remembers it ran) */
void VRG_AfterVideoRestart (void);	/* ... after the new context and textures are ready: starts VR again if it ran */
qboolean VRG_Active (void);	/* true while VR rendering is active: running and last frame had render != 0 */
qboolean VRG_SimActive (void);	/* true while the VR session is running (fixed-tick server; also when not rendering) */

/* Start of _Host_Frame (after setjmp): starts/stops VR from vr_enabled, calls VR_BeginFrame,
 * saves the frame flags. Returns VRG_Active(). */
qboolean VRG_HostFrameBegin (void);
/* Number of fixed 1/72 s server ticks to run for a render frame of dt seconds (0 when the
 * session is not rendering/focused: the server does not advance). */
int VRG_ServerTicks (double dt);
/* SCR_ModalMessage's wait loop, each iteration: while a VR session runs, begins the next VR frame
 * (VRG_HostFrameBegin) with the controllers in the dialog mapping (main trigger, A, X = 'y'; B, Y,
 * Menu = 'n', set as key_lastpress). Returns true if a VR frame was begun: the caller then draws it
 * with the dialog (SCR_UpdateScreen). False (and nothing done) while VR is not running. */
qboolean VRG_ModalFrame (void);

/* SCR_UpdateScreen, after SCR_SetUpToDrawConsole: the VR path. If this host frame's VR frame is
 * rendering, renders both eyes, mirrors them into the window (vr_mirror), ends the VR frame and
 * presents the window; returns true (the flat path is skipped). Later calls in the same host frame
 * (the VR frame is already submitted and the window presented) also return true and draw nothing.
 * Otherwise (VR off, or this host frame's VR frame is not rendering) ends a pending non-rendering
 * frame and returns false: the flat path draws the window. */
qboolean VRG_UpdateScreen (void);
qboolean VRG_EyeActive (void);	/* true while an eye's R_RenderView runs */
void VRG_SetupEyeGL (void);	/* R_SetupGL in an eye pass: eye viewport + eye projection */
void VRG_EyeFrustum (float normals[4][3]);	/* R_SetFrustum in an eye pass: left, right, bottom, top */

/* View angles (CL_SendCmd / cl_parse): all no-ops while VR is not running. */
void VRG_PreInput (void);	/* before CL_BaseMove: cl.viewangles = {0, body_yaw, 0} */
void VRG_StickMove (usercmd_t *cmd);	/* CL_BaseMove, after the keyboard terms: the off hand's stick adds to the move */
void VRG_PostInput (void);	/* after IN_Move: body_yaw = cl.viewangles[YAW]; cl.viewangles = body + head */
void VRG_OnSetAngle (void);	/* after svc_setangle(_interpolate): body_yaw = server yaw - head yaw */

/* VR move block (clc_hxvr: u8 len, len bytes; vr_api.h VR_WriteMoveBlock). Sent only to the local server. */
/* CL_SendMove, after the light level: while VR runs, a local server is active, the client is connected and
 * no demo plays, appends the block with the room-scale move walked since the last block (vrg_room_excess,
 * taken: the block consumes it; dropped instead when the server would not advance the player). */
void VRG_WriteMoveBlock (sizebuf_t *buf);
/* CL_SendMove: true while the player crouches for real (head below the body's crouch height, with
 * hysteresis) in the running game (signed on, no demo, no intermission; any key_dest) with vr_crouch_physical
 * on and vr_seated off: the crouch bit is sent. */
qboolean VRG_CrouchHeld (void);
/* SV_ReadClientMessage, case clc_hxvr: reads the block; its room-scale move adds to cl->hxvr_move
 * (vr_glue_server.c; sv_phys.c applies the sum on the next physics tick). */
void VRG_ServerReadMoveBlock (client_t *cl);
extern cvar_t vr_compat;
extern cvar_t vr_debug_fire;
void VRG_ServerFireBegin (void);
void VRG_ServerFireEnd (edict_t *player, const float muzzle[3], const float body[3]);
void VRG_FireInfo_f (void);

/* Implemented by the video driver (gl_vidnt.c): blocks sound while the window is unfocused, except
 * while a VR session runs. The glue calls it when VR starts / stops. */
void VID_VRSoundFocus (void);
void VID_VRSoundInit (void);	/* once, right after S_Init: block sound if unfocused and no VR */

#endif /* HXVR_VR_GLUE_H */
