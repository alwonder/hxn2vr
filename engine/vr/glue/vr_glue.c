/*
 * vr_glue.c -- engine-side glue for the hxvr VR module.
 * C, compiled into the engine; includes engine headers and vr_api.h only
 * (never the module's C++ headers).
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"

cvar_t	vr_enabled = {"vr_enabled", "0", CVAR_ARCHIVE};
cvar_t	vr_backend = {"vr_backend", "openxr", CVAR_ARCHIVE};	/* openxr / mock */
cvar_t	vr_render_scale = {"vr_render_scale", "1.0", CVAR_ARCHIVE};
cvar_t	vr_world_scale = {"vr_world_scale", "1.2", CVAR_ARCHIVE};
cvar_t	vr_eye_height = {"vr_eye_height", "1.65", CVAR_ARCHIVE};	/* metres */
cvar_t	vr_nearclip = {"vr_nearclip", "2", CVAR_ARCHIVE};		/* units */
cvar_t	vr_mirror = {"vr_mirror", "1", CVAR_ARCHIVE};		/* 0 none, 1 left eye, 2 both */
cvar_t	vr_panel_distance = {"vr_panel_distance", "1.5", CVAR_ARCHIVE};	/* metres */
cvar_t	vr_panel_width = {"vr_panel_width", "1.2", CVAR_ARCHIVE};	/* metres */
cvar_t	vr_panel_pitch = {"vr_panel_pitch", "15", CVAR_ARCHIVE};	/* degrees below eye level (in-game panel) */
cvar_t	vr_mock_motion = {"vr_mock_motion", "0", CVAR_NONE};
cvar_t	vr_mock_hz = {"vr_mock_hz", "90", CVAR_NONE};
cvar_t	vr_debug_ticks = {"vr_debug_ticks", "0", CVAR_NONE};

static void VRG_Recenter_f (void);
static void VRG_Status_f (void);

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
	Cvar_RegisterVariable (&vr_panel_pitch);
	Cvar_RegisterVariable (&vr_mock_motion);
	Cvar_RegisterVariable (&vr_mock_hz);
	Cvar_RegisterVariable (&vr_debug_ticks);
	Cmd_AddCommand ("vr_recenter", VRG_Recenter_f);
	Cmd_AddCommand ("vr_status", VRG_Status_f);
	VRG_InputInit ();	/* vr_mainhand, vr_bind_defaults, vr_mock_hand, vr_mock_head */
	VRG_BodyInit ();	/* vr_seated, vr_height_offset, vr_body_*, vr_calibrate, vr_body */
}

/* ---- VR session state ---- */

static vr_simclock_t	vrg_simclock;
vr_frame_t		vrg_frame;		/* shared: vr_glue_int.h */
qboolean		vrg_active;		/* shared: vr_glue_int.h */
static qboolean		vrg_frame_pending;	/* VR_BeginFrame done, VR_EndFrame not yet */

/* body yaw (shared: see vr_glue_int.h) */
float			vrg_body_yaw;
qboolean		vrg_body_valid;
static qboolean		vrg_recenter_pending;	/* auto-recenter on the first rendered frame after VR starts
						 * or after the backend reported vr_frame_t.recenter */

/* current eye pass (VRG_UpdateScreen) */
static qboolean		vrg_eye_active;
static vr_fov_t		vrg_eye_fov;
static int		vrg_eye_w, vrg_eye_h;

/* 2D panel placement (Quake world coordinates). In key_game with a world the panel follows the
 * body (recomputed every frame); otherwise it is fixed at an anchor taken from the head pose when
 * the key_dest leaves key_game, when there is no world (or the world changed), and on vr_recenter. */
static qboolean		vrg_panel_valid;	/* vrg_panel_pos/vrg_panel_yaw hold a fixed anchor */
static vec3_t		vrg_panel_pos;
static float		vrg_panel_yaw;
static keydest_t	vrg_panel_dest;		/* key_dest of the previous frame */
static qboolean		vrg_panel_world;	/* the previous frame had a world */
static const void	*vrg_panel_model;	/* ... and this world model */
static qboolean		vrg_panel_reanchor;	/* vr_recenter: take a new anchor */
static qboolean		vrg_panel_open;		/* VR_BeginPanel done, VR_EndPanel not yet */
static qboolean		vrg_in_update;		/* inside VRG_UpdateScreen (Con_Printf can re-enter) */

/* desktop window swap interval, saved while VR runs (the headset paces frames, not the monitor) */
typedef BOOL (WINAPI *vrg_swapinterval_f) (int);
typedef int (WINAPI *vrg_getswapinterval_f) (void);
static qboolean		vrg_swap_saved;
static int		vrg_swap_interval;
static int		vrg_dbg_frames, vrg_dbg_ticks;
static double		vrg_dbg_start;

/* server ticks per second over the last full one-second window (vr_status) */
static double		vrg_tick_start, vrg_tick_last;
static int		vrg_tick_count;
static float		vrg_ticks_per_s;

/* VR was running when the video mode was being changed (vid_restart): start it again afterwards */
static qboolean		vrg_restart_wanted;

/* vr_eye_height (metres) and vr_world_scale in use (VRG_CheckScaleCvars): the last valid values */
float			vrg_eye_height = 1.65f, vrg_world_scale = 1.2f;	/* shared: vr_glue_int.h */
static qboolean		vrg_eye_height_warned, vrg_world_scale_warned;

/* vr_panel_pitch in use (VRG_CheckPanelPitch): the last valid value, clamped to [-30, 80] */
#define VRG_PANEL_PITCH_MIN	(-30.0f)
#define VRG_PANEL_PITCH_MAX	80.0f
static float		vrg_panel_pitch = 15.0f;
static qboolean		vrg_panel_pitch_warned;

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

/* An invalid vr_eye_height / vr_world_scale (<= 0, NaN, infinite or absurdly large) would give a
 * zero, mirrored or non-finite units-per-metre: it is ignored, the last valid value stays in use and
 * one warning is printed (again only after a valid value was set in between). Safe print: runs
 * mid-frame. */
static void VRG_CheckScaleCvar (cvar_t *var, float *inuse, qboolean *warned)
{
	const float	v = var->value;

	if (v > 0.0f && v <= 1000.0f)	/* false for NaN */
	{
		*inuse = v;
		*warned = false;
	}
	else if (!*warned)
	{
		Con_SafePrintf ("VR: invalid %s \"%s\" ignored, using %g\n", var->name, var->string, *inuse);
		*warned = true;
	}
}

void VRG_CheckScaleCvars (void)
{
	VRG_CheckScaleCvar (&vr_eye_height, &vrg_eye_height, &vrg_eye_height_warned);
	VRG_CheckScaleCvar (&vr_world_scale, &vrg_world_scale, &vrg_world_scale_warned);
}

/* A non-finite vr_panel_pitch is ignored (last valid value stays, one warning); a finite one is
 * clamped to [-30, 80] degrees. */
static void VRG_CheckPanelPitch (void)
{
	const float	v = vr_panel_pitch.value;

	if (v - v == 0.0f)	/* false for NaN and infinities */
	{
		vrg_panel_pitch = (v < VRG_PANEL_PITCH_MIN) ? VRG_PANEL_PITCH_MIN :
				  (v > VRG_PANEL_PITCH_MAX) ? VRG_PANEL_PITCH_MAX : v;
		vrg_panel_pitch_warned = false;
	}
	else if (!vrg_panel_pitch_warned)
	{
		Con_SafePrintf ("VR: invalid %s \"%s\" ignored, using %g\n", vr_panel_pitch.name,
				vr_panel_pitch.string, vrg_panel_pitch);
		vrg_panel_pitch_warned = true;
	}
}

static void VRG_EndFrame (void)
{
	vrg_eye_active = false;
	if (vrg_panel_open)
		VR_EndPanel ();		/* a panel pass cut short (Host_Error longjmp) */
	vrg_panel_open = false;
	vrg_in_update = false;
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
	VRG_CheckScaleCvars ();
	p.eye_height_m = vrg_eye_height;
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
	vrg_body_yaw = 0;
	vrg_body_valid = false;
	VRG_RoomStart ();	/* also forgets the previous session's feet and world */
	vrg_recenter_pending = true;
	vrg_panel_valid = false;
	vrg_panel_reanchor = false;
	vrg_panel_open = false;
	vrg_in_update = false;
	VRG_CalibrateReset ();
	vrg_dbg_frames = vrg_dbg_ticks = 0;
	vrg_dbg_start = Sys_DoubleTime ();
	vrg_tick_start = vrg_tick_last = Sys_DoubleTime ();
	vrg_tick_count = 0;
	vrg_ticks_per_s = 0;
	vrg_frame_pending = false;
	VRG_SaveSwapInterval ();
	VID_VRSoundFocus ();	/* sound plays while VR runs, also with the desktop window unfocused */
	Con_Printf ("VR: started (%s)\n", bname);
	VRG_InputStart ();	/* button state, mock hands; may set (and announce) the default bindings */
}

static void VRG_Stop (void)
{
	VRG_ReleaseAllKeys ();	/* no +attack / +jump stays held by a controller button */
	VRG_CalibrateReset ();
	VRG_EndFrame ();
	VR_Stop ();
	VRG_RestoreSwapInterval ();
	vrg_active = false;
	memset (&vrg_frame, 0, sizeof(vrg_frame));
	VID_VRSoundFocus ();	/* back to the flat rule: no sound while the window is unfocused */
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
		if (vrg_frame.lost)
		{
			/* the headset session is gone (runtime quit, device removed...): back to the flat view.
			 * The backend reports lost on every frame until stopped, so stop on the first one. */
			Con_Printf ("VR: session lost, continuing in the flat view\n");
			VRG_Stop ();		/* VR_Stop ends the frame VR_BeginFrame left pending */
			Cvar_Set ("vr_enabled", "0");
			return false;
		}
		vrg_frame_pending = true;
		vrg_active = (vrg_frame.render != 0);
		if (vrg_frame.recenter)
			vrg_recenter_pending = true;	/* the runtime moved the tracking origin (headset off/on...) */
		if (vrg_active && vrg_recenter_pending)
		{
			/* the tracking origin is the stage centre, not where the player stands */
			vrg_recenter_pending = false;
			VRG_Recenter ();
		}

		VRG_InputFrame ();	/* controller buttons -> key events (may change key_dest) */
		VRG_CalibrateFrame ();	/* vr_calibrate prompt */

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

qboolean VRG_ModalFrame (void)
{
	if (!VR_IsRunning())
		return false;
	vrg_input_modal = true;
	VRG_HostFrameBegin ();	/* ends the dialog's previous frame; the backend paces (no busy spin) */
	vrg_input_modal = false;
	return vrg_frame_pending;
}

int VRG_ServerTicks (double dt)
{
	/* the server only advances while the headset is rendering and focused */
	const int paused = !(vrg_frame.focused && vrg_frame.render);
	const int n = VR_SimTicks (&vrg_simclock, dt, paused);
	const double now = Sys_DoubleTime ();
	vrg_dbg_ticks += n;
	if (now - vrg_tick_start >= 1.0)
	{
		vrg_ticks_per_s = (float) (vrg_tick_count / (now - vrg_tick_start));
		vrg_tick_start = now;
		vrg_tick_count = 0;
	}
	vrg_tick_count += n;
	vrg_tick_last = now;
	return n;
}

/* ---- body yaw / head-composed view angles ---- */

/* yaw of the head pose in Quake angles; angles[] = pitch, yaw, roll */
static void VRG_HeadAngles (float angles[3])
{
	vr_pose_t	pose;

	VRG_TrackedPoses (&pose, NULL, NULL);
	VR_TrackingToQuakeAngles (&pose.rot, angles);
}

/* Before CL_BaseMove: the keyboard/mouse turn the body, so the move code starts from
 * {0, body_yaw, 0}. */
void VRG_PreInput (void)
{
	float	head[3];

	if (!VR_IsRunning())
		return;
	if (cls.demoplayback)
	{
		/* the demo sets the view: it is the body, the head adds on top; cl.viewangles untouched */
		vrg_body_yaw = cl.viewangles[YAW];
		vrg_body_valid = true;
		return;
	}
	if (!vrg_body_valid)
	{
		/* VR started mid-game: the current view direction becomes the head's direction */
		VRG_HeadAngles (head);
		vrg_body_yaw = VR_BodyYawFromSetAngle (cl.viewangles[YAW], head[YAW]);
		vrg_body_valid = true;
	}
	vrg_body_yaw += VRG_TurnStep ();	/* snap / smooth turn with the main hand's stick */
	cl.viewangles[PITCH] = 0;
	cl.viewangles[YAW] = vrg_body_yaw;
	cl.viewangles[ROLL] = 0;
}

/* After IN_Move: take the turned body yaw and compose it with the head's angles; this is
 * what CL_SendMove sends (movement and shooting follow the head in phase 1). */
void VRG_PostInput (void)
{
	float	head[3], view[3];

	if (!VR_IsRunning() || cls.demoplayback)
		return;
	vrg_body_yaw = cl.viewangles[YAW];
	VRG_HeadAngles (head);
	VR_ComposeViewAngles (vrg_body_yaw, head, view);
	VectorCopy (view, cl.viewangles);
}

/* After the server wrote cl.viewangles (svc_setangle / svc_setangle_interpolate): choose the
 * body yaw so that body + head yaw gives the server's yaw. */
void VRG_OnSetAngle (void)
{
	float	head[3];

	if (!VR_IsRunning())
		return;
	VRG_HeadAngles (head);
	vrg_body_yaw = VR_BodyYawFromSetAngle (cl.viewangles[YAW], head[YAW]);
	vrg_body_valid = true;
	VRG_RoomReset ();	/* teleport, spawn: the head is back over the feet */
}

/* the head is put back over the feet: the lean is reset and wherever the head now is becomes the
 * player's origin (vr_recenter, and automatically on the first rendered frame after VR starts or the
 * runtime moved the tracking origin). Safe print: may run mid-frame. */
void VRG_Recenter (void)
{
	VRG_RoomReset ();
	vrg_panel_reanchor = true;	/* the fixed 2D panel is placed again in front of the head */
	Con_SafePrintf ("VR: recentered\n");
}

static void VRG_Recenter_f (void)
{
	if (!VR_IsRunning())
	{
		Con_Printf ("VR: not running\n");
		return;
	}
	VRG_Recenter ();
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

/* ---- 2D panel ---- */

/* Corners of the panel for this frame (see the placement comment at vrg_panel_valid). feet/body_yaw/
 * upm describe the player (feet is the origin when there is no world); head_org/head_view are the
 * head's position and composed view angles in the same space. */
static void VRG_PanelPlace (qboolean world, const vec3_t feet, float body_yaw, float upm,
				const vec3_t head_org, const vec3_t head_view, float corners[4][3])
{
	const keydest_t	dest = Key_GetDest ();
	const float	dist = vr_panel_distance.value * upm;
	const float	width = vr_panel_width.value * upm;
	const float	aspect = (vid.height > 0) ? (float)vid.width / (float)vid.height : 1.0f;
	const void	*model = world ? (const void *) cl.worldmodel : NULL;
	float		center[3], ang[3], fwd[3], right[3], up[3];
	float		yaw, pitch;

	if (world && dest == key_game)
	{
		/* follows the body: vr_panel_pitch degrees below the nominal eye (the body's eye height over
		 * feet + lean), vr_panel_distance ahead along the body yaw */
		vr_vec3_t	eye;

		eye.x = eye.z = 0;
		eye.y = vrg_body.eye_height;
		VR_EyeOrigin (feet, body_yaw, vrg_room.lean, &eye, &eye, upm, VRG_HeightScale (), center);
		yaw = body_yaw;
		VRG_CheckPanelPitch ();
		pitch = vrg_panel_pitch;
		ang[0] = pitch;
		ang[1] = yaw;
		ang[2] = 0;
		AngleVectors (ang, fwd, right, up);
		VectorMA (center, dist, fwd, center);
	}
	else
	{
		if (!vrg_panel_valid || vrg_panel_reanchor ||
		    world != vrg_panel_world || model != vrg_panel_model ||
		    (vrg_panel_dest == key_game && dest != key_game))
		{
			/* in front of the head, along its heading in the world, at eye level */
			const double	a = head_view[YAW] * (3.14159265358979323846 / 180.0);

			vrg_panel_pos[0] = head_org[0] + dist * (float) cos (a);
			vrg_panel_pos[1] = head_org[1] + dist * (float) sin (a);
			vrg_panel_pos[2] = head_org[2];
			vrg_panel_yaw = head_view[YAW];
			vrg_panel_valid = true;
			vrg_panel_reanchor = false;
		}
		VectorCopy (vrg_panel_pos, center);
		yaw = vrg_panel_yaw;
		pitch = 0;
	}
	vrg_panel_dest = dest;
	vrg_panel_world = world;
	vrg_panel_model = model;
	VR_PanelCorners (center, yaw, pitch, width, aspect, corners);
}

/* Renders the engine's 2D layer into the panel texture. The 2D code works in glx/gly/glwidth/
 * glheight (set by GL_BeginRendering each frame) and vid.width/height; the panel is vid.width x
 * vid.height pixels. Returns true if the panel texture is ready to be drawn. */
static qboolean VRG_RenderPanel (void)
{
	const int	sx = glx, sy = gly, sw = glwidth, sh = glheight;

	if (vid.width <= 0 || vid.height <= 0 || !VR_BeginPanel (vid.width, vid.height))
		return false;
	vrg_panel_open = true;
	glx = gly = 0;
	glwidth = vid.width;
	glheight = vid.height;
	SCR_Draw2DForVR ();
	glx = sx;
	gly = sy;
	glwidth = sw;
	glheight = sh;
	vrg_panel_open = false;
	VR_EndPanel ();
	return true;
}

/* Draws the panel quad into the bound eye with the eye's projection and view matrices. */
static void VRG_DrawPanel (const float corners[4][3], const vec3_t org, const vec3_t ang)
{
	float	proj[16], view[16];
	float	znear = vr_nearclip.value;

	if (!(znear >= 0.1f))
		znear = 0.1f;
	VR_ProjectionMatrix (&vrg_eye_fov, znear, 4096.0f, proj);
	VR_QuakeViewMatrix (org, ang, view);

	glViewport_fp (0, 0, vrg_eye_w, vrg_eye_h);
	glMatrixMode_fp (GL_PROJECTION);
	glPushMatrix_fp ();
	glLoadMatrixf_fp (proj);
	glMatrixMode_fp (GL_MODELVIEW);
	glPushMatrix_fp ();
	glLoadMatrixf_fp (view);
	VR_DrawPanelQuad (corners);
	glPopMatrix_fp ();
	glMatrixMode_fp (GL_PROJECTION);
	glPopMatrix_fp ();
	glMatrixMode_fp (GL_MODELVIEW);
}

qboolean VRG_UpdateScreen (void)
{
	int		eye, numvis = 0;
	qboolean	world, panel;
	vec3_t		feet, save_org, save_ang, head_ang, eye_org, eye_ang, head_org, head_view;
	float		body_yaw, upm, hscale;
	float		corners[4][3];
	vr_pose_t	head, eyes[2];

	if (vrg_in_update)
		return true;	/* re-entered (Con_Printf during signon) while this frame is being drawn */
	if (!vrg_frame_pending)
		return vrg_active;	/* VR off or not rendering (false: flat path); or this host frame's VR
					 * frame was already submitted and the window presented (true: draw nothing) */
	if (!vrg_active)
	{
		VRG_EndFrame ();	/* not rendering: the window takes the flat path */
		return false;
	}
	vrg_in_update = true;

	world = (cls.state == ca_connected && cl.worldmodel && !con_forcedup);
	VectorClear (feet);
	body_yaw = vrg_body_yaw;
	VRG_CheckScaleCvars ();
	upm = VR_UnitsPerMetre (vrg_eye_height, vrg_world_scale);
	if (world)
	{
		V_SetupRefdef ();
		VectorCopy (r_refdef.vieworg, save_org);
		VectorCopy (r_refdef.viewangles, save_ang);
		VectorCopy (r_refdef.vieworg, feet);
		feet[2] -= cl.viewheight;
		numvis = cl_numvisedicts;	/* R_DrawWorld appends static entities on every pass */
	}

	/* the 2D layer (menus, console, HUD, centerprint...) goes into the panel texture first;
	 * without a world the player stands at the origin */
	VRG_TrackedPoses (&head, eyes, NULL);	/* with vr_height_offset */
	VRG_RoomFrame (world, feet, &head.pos, upm);	/* the head's lean from the feet, this frame */
	hscale = VRG_HeightScale ();
	VR_EyeOrigin (feet, body_yaw, vrg_room.lean, &head.pos, &head.pos, upm, hscale, head_org);
	VR_TrackingToQuakeAngles (&head.rot, head_ang);
	VR_ComposeViewAngles (body_yaw, head_ang, head_view);
	VRG_PanelPlace (world, feet, body_yaw, upm, head_org, head_view, corners);
	panel = VRG_RenderPanel ();

	VR_GetEyeSize (&vrg_eye_w, &vrg_eye_h);
	for (eye = 0; eye < 2; eye++)
	{
		if (VR_BeginEye (eye))
		{
			VR_GetEye (eye, NULL, &vrg_eye_fov);
			VR_EyeOrigin (feet, body_yaw, vrg_room.lean, &head.pos, &eyes[eye].pos, upm, hscale, eye_org);
			VR_TrackingToQuakeAngles (&eyes[eye].rot, head_ang);
			VR_ComposeViewAngles (body_yaw, head_ang, eye_ang);
			if (world)
			{
				VectorCopy (eye_org, r_refdef.vieworg);
				VectorCopy (eye_ang, r_refdef.viewangles);
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
			if (panel)
				VRG_DrawPanel ((const float (*)[3]) corners, eye_org, eye_ang);
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
		VectorCopy (head_org, r_origin);
		AngleVectors (head_view, vpn, vright, vup);
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

/* ---- vid_restart, vr_status ---- */

/* VID_ChangeVideoMode deletes the GL context: every GL object of the module (eye and panel
 * targets, mock eye textures, the OpenXR session bound to the context) dies with it. Stop VR while
 * the old context is still current so the module deletes its objects properly. */
void VRG_BeforeVideoRestart (void)
{
	vrg_restart_wanted = (VR_IsRunning() != 0);
	if (vrg_restart_wanted)
		VRG_Stop ();
}

/* The new context is current and the engine's textures are reloaded: start VR again through the
 * normal path (which also re-saves and re-applies the window's swap interval). */
void VRG_AfterVideoRestart (void)
{
	if (!vrg_restart_wanted)
		return;
	vrg_restart_wanted = false;
	if (vr_enabled.integer && !VR_IsRunning())
		VRG_Start ();
}

static void VRG_Status_f (void)
{
	vr_status_t	st;
	const double	now = Sys_DoubleTime ();

	VR_GetStatus (&st);
	if (!st.running)
	{
		Con_Printf ("VR: stopped (vr_enabled %d, vr_backend %s)\n", vr_enabled.integer, vr_backend.string);
		return;
	}
	Con_Printf ("VR: backend %s, session %s\n", st.backend, st.state);
	if (st.runtime[0])
		Con_Printf ("VR: runtime %s\n", st.runtime);
	Con_Printf ("VR: eye %dx%d, swapchain format %s\n", st.eye_w, st.eye_h, st.format[0] ? st.format : "n/a");
	Con_Printf ("VR: %.1f frames/s (%.1f rendered), %.1f server ticks/s\n", st.frames_per_s, st.rendered_per_s,
			(now - vrg_tick_last < 2.0) ? vrg_ticks_per_s : 0.0f);
	Con_Printf ("VR: hands left %s (%s), right %s (%s)\n",
			st.hand_profile[VR_HAND_LEFT][0] ? st.hand_profile[VR_HAND_LEFT] : "none",
			st.hand_active[VR_HAND_LEFT] ? "active" : "inactive",
			st.hand_profile[VR_HAND_RIGHT][0] ? st.hand_profile[VR_HAND_RIGHT] : "none",
			st.hand_active[VR_HAND_RIGHT] ? "active" : "inactive");
}
