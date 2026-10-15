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

void VRG_Init (void);		/* Host_Init, before config.cfg/hexen.rc are executed: registers cvars */
void VRG_Shutdown (void);	/* Host_Shutdown: stops the module if running */
qboolean VRG_Active (void);	/* true while VR rendering is active: running and last frame had render != 0 */
qboolean VRG_SimActive (void);	/* true while the VR session is running (fixed-tick server; also when not rendering) */

/* Start of _Host_Frame (after setjmp): starts/stops VR from vr_enabled, calls VR_BeginFrame,
 * saves the frame flags. Returns VRG_Active(). */
qboolean VRG_HostFrameBegin (void);
/* Number of fixed 1/72 s server ticks to run for a render frame of dt seconds (0 when the
 * session is not rendering/focused: the server does not advance). */
int VRG_ServerTicks (double dt);

/* SCR_UpdateScreen, after SCR_SetUpToDrawConsole: the VR path. If this host frame's VR frame is
 * rendering, renders both eyes, mirrors them into the window (vr_mirror), ends the VR frame and
 * presents the window; returns true (the flat path is skipped). Otherwise (VR off, frame not
 * rendering, or this frame's VR frame already submitted) ends a pending non-rendering frame and
 * returns false: the flat path draws the window. */
qboolean VRG_UpdateScreen (void);
qboolean VRG_EyeActive (void);	/* true while an eye's R_RenderView runs */
void VRG_SetupEyeGL (void);	/* R_SetupGL in an eye pass: eye viewport + eye projection */
void VRG_EyeFrustum (float normals[4][3]);	/* R_SetFrustum in an eye pass: left, right, bottom, top */

/* View angles (CL_SendCmd / cl_parse): all no-ops while VR is not running. */
void VRG_PreInput (void);	/* before CL_BaseMove: cl.viewangles = {0, body_yaw, 0} */
void VRG_PostInput (void);	/* after IN_Move: body_yaw = cl.viewangles[YAW]; cl.viewangles = body + head */
void VRG_OnSetAngle (void);	/* after svc_setangle(_interpolate): body_yaw = server yaw - head yaw */

#endif /* HXVR_VR_GLUE_H */
