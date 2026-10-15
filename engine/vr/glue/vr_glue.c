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

/* ---- VR session state ---- */

static vr_simclock_t	vrg_simclock;
static vr_frame_t	vrg_frame;		/* flags of the last VR_BeginFrame */
static qboolean		vrg_active;		/* running && last frame had render != 0 */
static int		vrg_dbg_frames, vrg_dbg_ticks;
static double		vrg_dbg_start;

static void VRG_HostPrint (const char *msg)
{
	Con_Printf ("%s\n", msg);
}

static void VRG_Start (void)
{
	vr_host_t		host;
	vr_start_params_t	p;
	char			err[256];
	const char		*bname;

	memset (&host, 0, sizeof(host));
	host.print = VRG_HostPrint;
	host.gl_get_proc = NULL;		/* Task 1.6 fills this in */
	host.time = Sys_DoubleTime;

	memset (&p, 0, sizeof(p));
	if (!q_strcasecmp(vr_backend.string, "mock"))
	{
		p.backend = VR_BACKEND_MOCK;
		bname = "mock";
	}
	else
	{
		p.backend = VR_BACKEND_OPENXR;
		bname = "openxr";
	}
	p.render_scale = vr_render_scale.value;
	p.eye_height_m = vr_eye_height.value;
	p.mock_motion = vr_mock_motion.integer;
	p.mock_hz = vr_mock_hz.value;

	err[0] = '\0';
	if (!VR_Start(&host, &p, err, (int)sizeof(err)))
	{
		Con_Printf ("VR: %s\n", err);
		Cvar_Set ("vr_enabled", "0");
		return;
	}

	memset (&vrg_simclock, 0, sizeof(vrg_simclock));
	memset (&vrg_frame, 0, sizeof(vrg_frame));
	vrg_dbg_frames = vrg_dbg_ticks = 0;
	vrg_dbg_start = Sys_DoubleTime ();
	Con_Printf ("VR: started (%s)\n", bname);
}

static void VRG_Stop (void)
{
	VR_Stop ();
	vrg_active = false;
	memset (&vrg_frame, 0, sizeof(vrg_frame));
	Con_Printf ("VR: stopped\n");
}

void VRG_Shutdown (void)
{
	if (VR_IsRunning())
		VR_Stop ();
	vrg_active = false;
}

qboolean VRG_Active (void)
{
	return vrg_active;
}

qboolean VRG_SimActive (void)
{
	return VR_IsRunning() != 0;
}

qboolean VRG_HostFrameBegin (void)
{
	if (vr_enabled.integer && !VR_IsRunning())
		VRG_Start ();
	else if (!vr_enabled.integer && VR_IsRunning())
		VRG_Stop ();

	vrg_active = false;
	if (VR_IsRunning() && VR_BeginFrame(&vrg_frame))
	{
		vrg_active = (vrg_frame.render != 0);

		if (vr_debug_ticks.integer)
		{
			const double now = Sys_DoubleTime ();
			vrg_dbg_frames++;
			if (now - vrg_dbg_start >= 1.0)
			{
				Con_Printf ("vr: %d frames, %d server ticks\n", vrg_dbg_frames, vrg_dbg_ticks);
				vrg_dbg_frames = vrg_dbg_ticks = 0;
				vrg_dbg_start = now;
			}
		}
	}
	return vrg_active;
}

int VRG_ServerTicks (double dt)
{
	/* the server only advances while the headset is rendering and focused */
	const int paused = !(vrg_frame.focused && vrg_frame.render);
	const int n = VR_SimTicks (&vrg_simclock, dt, paused);
	vrg_dbg_ticks += n;
	return n;
}
