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
static qboolean		vrg_frame_pending;	/* VR_BeginFrame done, VR_EndFrame not yet */

/* current eye pass (VRG_UpdateScreen) */
static qboolean		vrg_eye_active;
static vr_fov_t		vrg_eye_fov;
static int		vrg_eye_w, vrg_eye_h;

/* desktop window swap interval, saved while VR runs (the headset paces frames, not the monitor) */
typedef BOOL (WINAPI *vrg_swapinterval_f) (int);
typedef int (WINAPI *vrg_getswapinterval_f) (void);
static qboolean		vrg_swap_saved;
static int		vrg_swap_interval;
static int		vrg_dbg_frames, vrg_dbg_ticks;
static double		vrg_dbg_start;

/* Module messages can arrive mid-frame (e.g. from VR_BeginEye): Con_Printf may re-enter
 * SCR_UpdateScreen while cls.signon != SIGNONS, so only safe prints here. */
static void VRG_HostPrint (const char *msg)
{
	Con_SafePrintf ("%s\n", msg);
}

/* GL loader for the module: wglGetProcAddress (extensions, GL > 1.1), falling back to the
 * opengl32.dll exports (GL 1.1 core functions, which wglGetProcAddress does not return). */
static void *VRG_GetProc (const char *name)
{
	static HMODULE	gl32;
	static PROC (WINAPI *getproc) (LPCSTR);
	void		*p = NULL;

	if (!gl32)
	{
		gl32 = GetModuleHandleA ("opengl32.dll");
		if (gl32)
			getproc = (PROC (WINAPI *)(LPCSTR)) GetProcAddress (gl32, "wglGetProcAddress");
	}
	if (getproc)
	{
		p = (void *) getproc (name);
		/* some drivers return small integers or -1 instead of NULL */
		if (p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1)
			p = NULL;
	}
	if (!p && gl32)
		p = (void *) GetProcAddress (gl32, name);
	return p;
}

static void VRG_SaveSwapInterval (void)
{
	vrg_getswapinterval_f	get = (vrg_getswapinterval_f) VRG_GetProc ("wglGetSwapIntervalEXT");
	vrg_swapinterval_f	set = (vrg_swapinterval_f) VRG_GetProc ("wglSwapIntervalEXT");

	vrg_swap_saved = false;
	if (!get || !set)
		return;		/* no WGL_EXT_swap_control: leave the window as it is */
	vrg_swap_interval = get ();
	if (set (0))
		vrg_swap_saved = true;
}

static void VRG_RestoreSwapInterval (void)
{
	vrg_swapinterval_f	set;

	if (!vrg_swap_saved)
		return;
	vrg_swap_saved = false;
	set = (vrg_swapinterval_f) VRG_GetProc ("wglSwapIntervalEXT");
	if (set)
		set (vrg_swap_interval);
}

static void VRG_EndFrame (void)
{
	vrg_eye_active = false;
	if (vrg_frame_pending)
		VR_EndFrame ();
	vrg_frame_pending = false;
}

static void VRG_Start (void)
{
	vr_host_t		host;
	vr_start_params_t	p;
	char			err[256];
	const char		*bname;

	memset (&host, 0, sizeof(host));
	host.print = VRG_HostPrint;
	host.gl_get_proc = VRG_GetProc;
	host.time = Sys_DoubleTime;

	memset (&p, 0, sizeof(p));
	if (!q_strcasecmp(vr_backend.string, "mock"))
	{
		p.backend = VR_BACKEND_MOCK;
		bname = "mock";
	}
	else
	{
		if (q_strcasecmp(vr_backend.string, "openxr"))
			Con_Printf ("VR: unknown vr_backend \"%s\", using openxr\n", vr_backend.string);
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
	vrg_frame_pending = false;
	VRG_SaveSwapInterval ();
	Con_Printf ("VR: started (%s)\n", bname);
}

static void VRG_Stop (void)
{
	VRG_EndFrame ();
	VR_Stop ();
	VRG_RestoreSwapInterval ();
	vrg_active = false;
	memset (&vrg_frame, 0, sizeof(vrg_frame));
	Con_Printf ("VR: stopped\n");
}

void VRG_Shutdown (void)
{
	VRG_EndFrame ();
	if (VR_IsRunning())
		VR_Stop ();
	VRG_RestoreSwapInterval ();
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
	/* a frame left pending by Host_Error (longjmp) or an undrawn screen is ended first */
	VRG_EndFrame ();

	if (vr_enabled.integer && !VR_IsRunning())
		VRG_Start ();
	else if (!vr_enabled.integer && VR_IsRunning())
		VRG_Stop ();

	vrg_active = false;
	if (VR_IsRunning() && VR_BeginFrame(&vrg_frame))
	{
		vrg_frame_pending = true;
		vrg_active = (vrg_frame.render != 0);

		if (vr_debug_ticks.integer)
		{
			const double now = Sys_DoubleTime ();
			vrg_dbg_frames++;
			if (now - vrg_dbg_start >= 1.0)
			{
				Con_SafePrintf ("vr: %d frames, %d server ticks\n", vrg_dbg_frames, vrg_dbg_ticks);	/* frame pending */
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

/* ---- per-eye rendering ---- */

qboolean VRG_EyeActive (void)
{
	return vrg_eye_active;
}

void VRG_SetupEyeGL (void)
{
	float	m[16];
	float	znear = vr_nearclip.value;

	if (!(znear >= 0.1f))
		znear = 0.1f;
	glViewport_fp (0, 0, vrg_eye_w, vrg_eye_h);
	VR_ProjectionMatrix (&vrg_eye_fov, znear, 4096.0f, m);
	glLoadMatrixf_fp (m);	/* matrix mode is GL_PROJECTION here (R_SetupGL) */
}

void VRG_EyeFrustum (float normals[4][3])
{
	VR_FrustumNormals (r_refdef.viewangles, &vrg_eye_fov, normals);
}

/* clears the bound framebuffer's colour (and `extra` buffers) without changing the engine's
 * clear colour */
static void VRG_ClearColor (float r, float g, float b, GLbitfield extra)
{
	float	saved[4];

	glGetFloatv_fp (GL_COLOR_CLEAR_VALUE, saved);
	glClearColor_fp (r, g, b, 1.0f);
	glClear_fp (GL_COLOR_BUFFER_BIT | extra);
	glClearColor_fp (saved[0], saved[1], saved[2], saved[3]);
}

qboolean VRG_UpdateScreen (void)
{
	int		eye, numvis = 0;
	qboolean	world;
	vec3_t		feet, save_org, save_ang, head_ang, org;
	float		body_yaw = 0, upm = 0;
	vr_pose_t	pose;
	vr_vec3_t	anchor = {0, 0, 0};	/* vr_recenter: Task 1.7 */

	if (!vrg_frame_pending)
		return false;	/* VR off, or this host frame's VR frame was already submitted */
	if (!vrg_active)
	{
		VRG_EndFrame ();	/* not rendering: the window takes the flat path */
		return false;
	}

	world = (cls.state == ca_connected && cl.worldmodel && !con_forcedup);
	if (world)
	{
		V_SetupRefdef ();
		VectorCopy (r_refdef.vieworg, save_org);
		VectorCopy (r_refdef.viewangles, save_ang);
		VectorCopy (r_refdef.vieworg, feet);
		feet[2] -= cl.viewheight;
		numvis = cl_numvisedicts;	/* R_DrawWorld appends static entities on every pass */
		body_yaw = cl.viewangles[YAW];	/* the glue's body_yaw from Task 1.7 on */
		upm = VR_UnitsPerMetre (vr_eye_height.value, vr_world_scale.value);
	}

	VR_GetEyeSize (&vrg_eye_w, &vrg_eye_h);
	for (eye = 0; eye < 2; eye++)
	{
		if (VR_BeginEye (eye))
		{
			if (world)
			{
				VR_GetEye (eye, &pose, &vrg_eye_fov);
				VR_EyeOrigin (feet, body_yaw, &pose.pos, &anchor, upm, r_refdef.vieworg);
				VR_TrackingToQuakeAngles (&pose.rot, head_ang);
				VR_ComposeViewAngles (body_yaw, head_ang, r_refdef.viewangles);
				cl_numvisedicts = numvis;
				vrg_eye_active = true;
				R_PushDlights ();	/* per eye: R_SetupFrame advances r_framecount */
				R_RenderView ();
				vrg_eye_active = false;
			}
			else
			{
				VRG_ClearColor (0.05f, 0.05f, 0.05f, GL_DEPTH_BUFFER_BIT);
			}
		}
		VR_EndEye (eye, vr_mirror.integer, glwidth, glheight);
	}

	if (world)
	{
		cl_numvisedicts = numvis;
		VectorCopy (save_org, r_refdef.vieworg);	/* reused next frame while paused */
		VectorCopy (save_ang, r_refdef.viewangles);
		/* the eye passes left r_origin/vpn/vright/vup at the right eye: put them at the head
		 * for the sound listener (S_Update) and anything else reading them after the refresh */
		VR_GetHeadPose (&pose);
		VR_EyeOrigin (feet, body_yaw, &pose.pos, &anchor, upm, org);
		VR_TrackingToQuakeAngles (&pose.rot, head_ang);
		VR_ComposeViewAngles (body_yaw, head_ang, head_ang);
		VectorCopy (org, r_origin);
		AngleVectors (head_ang, vpn, vright, vup);
	}

	VRG_EndFrame ();

	if (vr_mirror.integer != 1 && vr_mirror.integer != 2)
	{
		glViewport_fp (glx, gly, glwidth, glheight);
		VRG_ClearColor (0, 0, 0, 0);	/* no mirror: black window */
	}

	V_UpdatePalette ();	/* cshift decay and gamma, as in the flat path */
	GL_EndRendering ();
	return true;
}
