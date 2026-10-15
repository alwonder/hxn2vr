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

void VRG_Init (void);		/* end of Host_Init: registers cvars */
void VRG_Shutdown (void);	/* Host_Shutdown: stops the module if running */
qboolean VRG_Active (void);	/* true while VR rendering is active */

#endif /* HXVR_VR_GLUE_H */
