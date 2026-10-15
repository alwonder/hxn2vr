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

#endif /* HXVR_VR_GLUE_H */
