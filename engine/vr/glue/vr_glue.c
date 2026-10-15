/*
 * vr_glue.c -- engine-side glue for the hxvr VR module.
 * C, compiled into the engine; includes engine headers and vr_api.h only
 * (never the module's C++ headers).
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"

cvar_t	vr_enabled = {"vr_enabled", "0", CVAR_ARCHIVE};
cvar_t	vr_backend = {"vr_backend", "openxr", CVAR_ARCHIVE};	/* openxr / mock */
cvar_t	vr_render_scale = {"vr_render_scale", "1.0", CVAR_ARCHIVE};
cvar_t	vr_world_scale = {"vr_world_scale", "1.0", CVAR_ARCHIVE};
cvar_t	vr_eye_height = {"vr_eye_height", "1.65", CVAR_ARCHIVE};	/* metres */
cvar_t	vr_nearclip = {"vr_nearclip", "2", CVAR_ARCHIVE};		/* units */
cvar_t	vr_mirror = {"vr_mirror", "1", CVAR_ARCHIVE};		/* 0 none, 1 left eye, 2 both */
cvar_t	vr_panel_distance = {"vr_panel_distance", "1.5", CVAR_ARCHIVE};	/* metres */
cvar_t	vr_panel_width = {"vr_panel_width", "1.2", CVAR_ARCHIVE};	/* metres */
cvar_t	vr_mock_motion = {"vr_mock_motion", "0", CVAR_NONE};
cvar_t	vr_mock_hz = {"vr_mock_hz", "90", CVAR_NONE};
cvar_t	vr_debug_ticks = {"vr_debug_ticks", "0", CVAR_NONE};

void VRG_Init (void)
{
	Cvar_RegisterVariable (&vr_enabled);
	Cvar_RegisterVariable (&vr_backend);
	Cvar_RegisterVariable (&vr_render_scale);
	Cvar_RegisterVariable (&vr_world_scale);
	Cvar_RegisterVariable (&vr_eye_height);
	Cvar_RegisterVariable (&vr_nearclip);
	Cvar_RegisterVariable (&vr_mirror);
	Cvar_RegisterVariable (&vr_panel_distance);
	Cvar_RegisterVariable (&vr_panel_width);
	Cvar_RegisterVariable (&vr_mock_motion);
	Cvar_RegisterVariable (&vr_mock_hz);
	Cvar_RegisterVariable (&vr_debug_ticks);
}

void VRG_Shutdown (void)
{
	if (VR_IsRunning())
		VR_Stop ();
}

qboolean VRG_Active (void)
{
	return false;
}
