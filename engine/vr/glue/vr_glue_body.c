/*
 * vr_glue_body.c -- engine-side glue for the hxvr VR module: the player's body model (metres), the tracked
 * poses with the height offset applied, the seated mode cvars, the vr_calibrate command, the room-scale
 * lean of the head, the physical crouch and the head's collision with the world (ceiling clamp, fade in walls).
 * C, compiled into the engine; includes engine headers and vr_api.h only.
 *
 * The body is in metres of tracking space: nothing here uses vr_world_scale.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"
#include "vr_glue_weapon.h"

cvar_t	vr_seated = {"vr_seated", "0", CVAR_ARCHIVE};			/* 1: no physical crouch, height from vr_height_offset */
cvar_t	vr_height_offset = {"vr_height_offset", "0", CVAR_ARCHIVE};	/* metres, added to the y of head and hands */
cvar_t	vr_body_arm_length = {"vr_body_arm_length", "0", CVAR_ARCHIVE};	/* metres, 0 = from the proportions */
cvar_t	vr_body_shoulder_width = {"vr_body_shoulder_width", "0", CVAR_ARCHIVE};	/* metres, 0 = from the proportions */
cvar_t	vr_crouch_threshold = {"vr_crouch_threshold", "0.65", CVAR_ARCHIVE};	/* fraction of vr_eye_height */
cvar_t	vr_roomscale = {"vr_roomscale", "1", CVAR_ARCHIVE};			/* room-scale movement of the body */
cvar_t	vr_roomscale_radius = {"vr_roomscale_radius", "0.25", CVAR_ARCHIVE};	/* metres the head leaves the body's centre */
cvar_t	vr_crouch_physical = {"vr_crouch_physical", "1", CVAR_ARCHIVE};	/* crouching for real crouches in the game */
cvar_t	vr_head_collision = {"vr_head_collision", "1", CVAR_ARCHIVE};	/* ceiling clamp and fade in walls */

static void VRG_Calibrate_f (void);
static void VRG_Body_f (void);

/* ---- cvars in use ---- */

/* the last valid values (VRG_BodyCheck): an invalid cvar keeps the old value and warns once */
vr_body_t		vrg_body;
float			vrg_height_offset;

static float		vrg_arm_cvar, vrg_shoulder_cvar, vrg_crouch_cvar = 0.65f;
static qboolean		vrg_arm_warned, vrg_shoulder_warned, vrg_crouch_warned, vrg_offset_warned, vrg_eye_warned;

void VRG_BodyInit (void)
{
	vr_body_params_t	p;

	Cvar_RegisterVariable (&vr_seated);
	Cvar_RegisterVariable (&vr_height_offset);
	Cvar_RegisterVariable (&vr_body_arm_length);
	Cvar_RegisterVariable (&vr_body_shoulder_width);
	Cvar_RegisterVariable (&vr_crouch_threshold);
	Cvar_RegisterVariable (&vr_roomscale);
	Cvar_RegisterVariable (&vr_roomscale_radius);
	Cvar_RegisterVariable (&vr_crouch_physical);
	Cvar_RegisterVariable (&vr_head_collision);
	Cmd_AddCommand ("vr_calibrate", VRG_Calibrate_f);
	Cmd_AddCommand ("vr_body", VRG_Body_f);

	memset (&p, 0, sizeof(p));
	p.eye_height = vrg_eye_height;
	VR_BodyFromParams (&p, &vrg_body);
}

/* A cvar outside lo..hi (or NaN) is ignored: the last valid value stays, one warning (again only after
 * a valid value was set in between). lo_excl: lo itself is invalid too. Safe print: runs mid-frame. */
static void VRG_CheckRange (cvar_t *var, float lo, float hi, qboolean lo_excl, float *inuse, qboolean *warned)
{
	const float	v = var->value;

	if (v >= lo && v <= hi && !(lo_excl && v == lo))	/* false for NaN */
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

/* Re-reads the body cvars and rebuilds vrg_body when they (or the eye height) are valid. The model is
 * cheap to build, so this runs on every VRG_TrackedPoses. */
static void VRG_BodyCheck (void)
{
	vr_body_params_t	p;
	vr_body_t		b;

	VRG_CheckScaleCvars ();
	VRG_CheckRange (&vr_height_offset, -3.0f, 3.0f, false, &vrg_height_offset, &vrg_offset_warned);
	VRG_CheckRange (&vr_body_arm_length, 0.0f, 3.0f, false, &vrg_arm_cvar, &vrg_arm_warned);
	VRG_CheckRange (&vr_body_shoulder_width, 0.0f, 3.0f, false, &vrg_shoulder_cvar, &vrg_shoulder_warned);
	VRG_CheckRange (&vr_crouch_threshold, 0.0f, 1.0f, true, &vrg_crouch_cvar, &vrg_crouch_warned);

	memset (&p, 0, sizeof(p));
	p.eye_height = vrg_eye_height;
	p.arm_length = vrg_arm_cvar;
	p.shoulder_width = vrg_shoulder_cvar;
	p.crouch_fraction = vrg_crouch_cvar;
	if (VR_BodyFromParams (&p, &b))
	{
		vrg_body = b;
		vrg_eye_warned = false;
	}
	else if (!vrg_eye_warned)
	{
		Con_SafePrintf ("VR: eye height %g m is outside 0.5..2.5 m, keeping the previous body model\n",
				vrg_eye_height);
		vrg_eye_warned = true;
	}
}

/* ---- tracked poses ---- */

/* The one place that applies vr_height_offset (to the y of head, eyes and the poses of active hands);
 * rotations are unchanged. Every consumer of a head, eye or hand pose takes it from here. head, eyes and
 * hands may each be NULL. Eye poses are those of the last VR_BeginFrame (use VR_GetEye for the fov). */
void VRG_TrackedPoses (vr_pose_t *head, vr_pose_t eyes[2], vr_hand_t hands[2])
{
	int	i;

	VRG_BodyCheck ();
	if (head)
	{
		VR_GetHeadPose (head);
		head->pos.y += vrg_height_offset;
	}
	if (eyes)
	{
		for (i = 0; i < 2; i++)
		{
			VR_GetEye (i, &eyes[i], NULL);
			eyes[i].pos.y += vrg_height_offset;
		}
	}
	if (hands)
	{
		for (i = 0; i < 2; i++)
		{
			VR_GetHand (i, &hands[i]);
			if (hands[i].active)
			{
				hands[i].grip.pos.y += vrg_height_offset;
				hands[i].aim.pos.y += vrg_height_offset;
			}
		}
	}
}

/* ---- room-scale on the client ---- */

/* The head leans away from the body's centre (the feet) in world axes: turning the body does not move
 * the head. The lean is stepped once per rendered frame (VRG_RoomFrame); past vr_roomscale_radius the
 * excess is for the body to walk. */
vr_room_t		vrg_room;
float			vrg_room_excess[2];

static float		vrg_room_radius = 0.25f;	/* vr_roomscale_radius in use (metres) */
static qboolean		vrg_room_radius_warned;
static vec3_t		vrg_room_feet;			/* the feet of the previous rendered world frame ... */
static qboolean		vrg_room_feet_valid;		/* ... if there was one */
static const void	*vrg_room_model;		/* cl.worldmodel of the previous frame (NULL: none) */

/* lean 0 and forget the last head: the head is back over the feet (vr_recenter, setangle, teleport,
 * new level, VR start); the excess not yet sent is dropped (the body must not jump on it) */
void VRG_RoomReset (void)
{
	VR_RoomReset (&vrg_room);
	vrg_room_excess[0] = vrg_room_excess[1] = 0;
}

/* VR starts: nothing of a previous session (its last feet and world) is kept */
void VRG_RoomStart (void)
{
	VRG_RoomReset ();
	vrg_room_feet_valid = false;
	vrg_room_model = NULL;
}

qboolean VRG_RoomWalking (void)
{
	return VR_IsRunning () && sv.active && cls.state == ca_connected && cls.signon == SIGNONS &&
		cl.worldmodel && !cls.demoplayback && !cl.paused && !sv.paused && !cl.intermission && !cl.v.cameramode &&
		Key_GetDest () == key_game && vrg_frame.focused && vrg_frame.render && vr_roomscale.integer;
}

/* Once per rendered frame (VRG_UpdateScreen), before the eyes are placed. world: there is a world to
 * draw (feet valid); head: the tracked head (VRG_TrackedPoses). The lean resets on a new world model, when
 * the world comes back after frames without one (the head moved freely meanwhile) and when the feet jumped
 * (teleport, respawn, load). Without a world the head moves freely (no radius: nothing to walk or collide
 * with). Over a world the lean stays within the radius; while the server advances the player
 * (VRG_RoomWalking) the excess past it is summed into vrg_room_excess for the next move block, otherwise
 * it is dropped (and so is any excess not yet sent): the head stops at the radius. */
void VRG_RoomFrame (qboolean world, const vec3_t feet, const vr_vec3_t *head, float upm)
{
	const void	*model = (cls.state == ca_connected) ? (const void *) cl.worldmodel : NULL;
	float		excess[2], radius;

	if (model != vrg_room_model)
	{
		VRG_RoomReset ();
		vrg_room_model = model;
		vrg_room_feet_valid = false;
	}
	else if (world && (!vrg_room_feet_valid || VR_BodyJumped (vrg_room_feet, feet)))
	{
		VRG_RoomReset ();
	}
	if (world)
		VectorCopy (feet, vrg_room_feet);
	vrg_room_feet_valid = world;

	VRG_CheckRange (&vr_roomscale_radius, 0.0f, 3.0f, false, &vrg_room_radius, &vrg_room_radius_warned);
	radius = world ? vrg_room_radius * upm : 1.0e30f;	/* no world: the head moves freely */
	VR_RoomStep (&vrg_room, head, vrg_body_yaw, upm, radius, excess);
	if (world && VRG_RoomWalking ())
	{
		vrg_room_excess[0] += excess[0];
		vrg_room_excess[1] += excess[1];
	}
	else
	{
		vrg_room_excess[0] = vrg_room_excess[1] = 0;
	}
}

/* CL_SendMove: the VR move block for the local server (stock servers never see clc_hxvr). The block takes
 * the excess summed since the last one; if the server would not advance the player now, the excess is
 * dropped instead. Blocks written into a move CL_SendMove does not send (the first two of a level) lose
 * their excess. */
void VRG_WriteMoveBlock (sizebuf_t *buf)
{
	vr_moveblock_t	b;
	unsigned char	blk[VR_MOVEBLOCK_MAX];
	float		muzzle[3];
	qboolean	valid;
	int		n;

	if (!VR_IsRunning () || !sv.active || cls.state != ca_connected || cls.demoplayback)
		return;
	memset (&b, 0, sizeof(b));
	b.version = VR_MOVEBLOCK_VERSION;
	if (VRG_RoomWalking () && (vrg_room_excess[0] != 0 || vrg_room_excess[1] != 0))
	{
		b.flags |= VR_MOVE_ROOMSCALE;
		b.roomscale[0] = vrg_room_excess[0];
		b.roomscale[1] = vrg_room_excess[1];
	}
	vrg_room_excess[0] = vrg_room_excess[1] = 0;
	VRG_WeaponPose (muzzle, b.aim, &valid);
	if (valid && vr_compat.integer)
	{
		b.flags |= VR_MOVE_WEAPON;
		VectorSubtract (muzzle, cl_entities[cl.viewentity].origin, b.muzzle);
	}
	n = VR_WriteMoveBlock (&b, blk, (int) sizeof(blk));
	if (n <= 0 || buf->cursize + 2 + n > buf->maxsize)
		return;
	MSG_WriteByte (buf, clc_hxvr);
	MSG_WriteByte (buf, n);
	SZ_Write (buf, blk, n);
}

/* ---- physical crouch, camera height ---- */

static int		vrg_crouched;		/* VR_CrouchUpdate state (hysteresis) */
static qboolean		vrg_crouch_rising;	/* stood up from a physical crouch, the game's view still rising */
static float		vrg_crouch_view;	/* cl.viewheight at the previous VRG_HeightScale */

/* the physical crouch counts: VR runs, vr_crouch_physical, not seated, in the running game (signed on,
 * no demo, no intermission). Not tied to key_dest: the body stays crouched while a menu or the console is
 * up, as a held crouch button does (a single player game does not run meanwhile anyway). */
static qboolean VRG_CrouchGameplay (void)
{
	return VR_IsRunning () && vr_crouch_physical.integer && !vr_seated.integer && cls.state == ca_connected &&
		cls.signon == SIGNONS && !cls.demoplayback && !cl.intermission;
}

/* CL_SendMove (crouch button bit) and the camera: the head's tracked height (with vr_height_offset) below
 * the body's crouch height crouches in the game (VR_CrouchUpdate, hysteresis). Outside the running game the
 * state is dropped. Idempotent for the same head: may run several times per frame. */
qboolean VRG_CrouchHeld (void)
{
	vr_pose_t	head;

	if (!VRG_CrouchGameplay ())
	{
		vrg_crouched = 0;
		return false;
	}
	VRG_TrackedPoses (&head, NULL, NULL);
	vrg_crouched = VR_CrouchUpdate (vrg_crouched, head.pos.y, &vrg_body);
	return vrg_crouched != 0;
}

/* Scale of the eye height above the feet: the game's view height (cl.viewheight / 50: button crouch, death,
 * polymorph), except while physically crouched (the real head is low already). Standing up from a physical
 * crouch the game raises its view from 24 to 50 over a few frames; until it is up (or it goes down instead,
 * or the crouch button takes over) the real head height still counts, so the camera does not dip. */
float VRG_HeightScale (void)
{
	const qboolean	physical = VRG_CrouchHeld ();
	const float	view = cl.viewheight;

	if (physical)
		vrg_crouch_rising = true;
	else if (!VRG_CrouchGameplay () || (in_crouch.state & 1) || view >= 50.0f || view < vrg_crouch_view)
		vrg_crouch_rising = false;
	vrg_crouch_view = view;
	return VR_CameraHeightScale (view, physical || vrg_crouch_rising);
}

/* ---- head collision with the world ---- */

static float		vrg_fade;		/* 0..1, black over the eyes while the head is in a wall */
static double		vrg_fade_time;		/* host time of the previous VRG_HeadCollide (0: none) */

/* A line through the world's point hull (hull 0), initialized like SV_ClipMoveToEntity */
void VRG_WorldTrace (const vec3_t start, const vec3_t end, trace_t *tr)
{
	hull_t	*hull = &cl.worldmodel->hulls[0];
	vec3_t	p1, p2;

	VectorCopy (start, p1);
	VectorCopy (end, p2);
	memset (tr, 0, sizeof(*tr));
	tr->fraction = 1;
	tr->allsolid = true;
	VectorCopy (end, tr->endpos);
	SV_RecursiveHullCheck (hull, hull->firstclipnode, 0, 1, p1, p2, tr);
}

/* VRG_UpdateScreen, once per rendered frame: the head's centre cam (Quake units) is kept under the ceiling
 * (moved down in place; *shift_z is the move to apply to both eyes, <= 0) and the fade goes toward black
 * while the line from the body's eye to the head crosses a wall. Only with a world, vr_head_collision and
 * not in noclip; otherwise nothing moves and the fade goes back to 0. Returns the fade alpha (0..1). */
float VRG_HeadCollide (qboolean world, const vec3_t feet, vec3_t cam, float *shift_z)
{
	const double	now = Sys_DoubleTime ();
	const float	dt = vrg_fade_time > 0 ? (float) (now - vrg_fade_time) : 0.0f;
	float		target = 0.0f;
	vec3_t		start, end;
	trace_t		tr;

	vrg_fade_time = now;
	*shift_z = 0;
	if (world && cl.worldmodel && vr_head_collision.integer && cl.v.movetype != MOVETYPE_NOCLIP)
	{
		/* the body's eye height, or the head's if it is lower */
		const float	eye_z = feet[2] + q_min (cl.viewheight, cam[2] - feet[2]);

		/* ceiling: from below the head up to 4 units above it */
		start[0] = cam[0];
		start[1] = cam[1];
		start[2] = eye_z;
		VectorCopy (cam, end);
		end[2] += 4;
		VRG_WorldTrace (start, end, &tr);
		if (tr.fraction < 1 && !tr.startsolid)
		{
			const float	z = q_max (start[2], tr.endpos[2] - 4);

			*shift_z = z - cam[2];
			cam[2] = z;
		}

		/* wall: from the body's eye (over the feet) to the head */
		start[0] = feet[0];
		start[1] = feet[1];
		start[2] = eye_z;
		VRG_WorldTrace (start, cam, &tr);
		if (tr.fraction < 1 || tr.startsolid)
			target = 1.0f;
	}
	vrg_fade = VR_Approach (vrg_fade, target, 6.0f, dt);
	return vrg_fade;
}

/* VR starts: no crouch, no fade left from a previous session */
void VRG_CrouchStart (void)
{
	vrg_crouched = 0;
	vrg_crouch_rising = false;
	vrg_fade = 0;
	vrg_fade_time = 0;
}

/* ---- vr_body ---- */

static void VRG_Body_f (void)
{
	VRG_BodyCheck ();
	Con_Printf ("VR: body (m): eye %.3f, stature %.3f, shoulder height %.3f, shoulder width %.3f, arm %.3f\n",
			vrg_body.eye_height, vrg_body.stature, vrg_body.shoulder_height, vrg_body.shoulder_width,
			vrg_body.arm_length);
	Con_Printf ("VR: body (m): chest %.3f, waist %.3f, crouch %.3f, height offset %.3f%s\n",
			vrg_body.chest_height, vrg_body.waist_height, vrg_body.crouch_height, vrg_height_offset,
			vr_seated.integer ? " (seated)" : "");
}

/* ---- vr_calibrate ---- */

/* Hexen II centerprint: '@' breaks a line */
#define VRG_CAL_STAND	"Stand straight, look ahead,@press the trigger"
#define VRG_CAL_SIT	"Sit comfortably, look ahead,@press the trigger"
#define VRG_CAL_ARM	"Stretch your main arm straight ahead@at shoulder height, press the trigger"

enum { VRG_CAL_OFF = 0, VRG_CAL_EYE, VRG_CAL_ARMLEN };
static int		vrg_cal_step;
static double		vrg_cal_print;		/* host time of the last prompt (centerprint times out) */

static void VRG_CalPrompt (void)
{
	SCR_CenterPrint (vrg_cal_step == VRG_CAL_ARMLEN ? VRG_CAL_ARM :
			 vr_seated.integer ? VRG_CAL_SIT : VRG_CAL_STAND);
	vrg_cal_print = Sys_DoubleTime ();
}

static void VRG_CalEnd (qboolean cancelled)
{
	vrg_cal_step = VRG_CAL_OFF;
	SCR_CenterPrint ("");
	if (cancelled)
		Con_SafePrintf ("VR: calibration cancelled\n");
}

static qboolean VRG_PoseSane (const vr_pose_t *p)
{
	const float	n = p->rot.x * p->rot.x + p->rot.y * p->rot.y + p->rot.z * p->rot.z + p->rot.w * p->rot.w;

	return p->pos.x - p->pos.x == 0.0f && p->pos.y - p->pos.y == 0.0f && p->pos.z - p->pos.z == 0.0f &&
		n > 0.5f && n < 1.5f;	/* false for NaN */
}

/* Step 1: the head's raw height (no vr_height_offset). Standing: it is the eye height and the offset 0.
 * Seated: the offset brings the head up to vr_eye_height. Then recenter. */
static void VRG_CalEyeStep (void)
{
	vr_pose_t	raw;
	float		y;

	VR_GetHeadPose (&raw);
	if (!VRG_Active () || !VRG_PoseSane (&raw))
	{
		Con_SafePrintf ("VR: calibration: head not tracked, try again\n");
		return;
	}
	if (!VR_CalibrateEyeHeight (raw.pos.y, &y))
	{
		Con_SafePrintf ("VR: calibration: %s %.2f m out of range, try again\n",
				vr_seated.integer ? "head height" : "eye height", raw.pos.y);
		return;
	}
	VRG_CheckScaleCvars ();
	if (vr_seated.integer)
	{
		Cvar_SetValue ("vr_height_offset", vrg_eye_height - y);
	}
	else
	{
		vr_status_t	st;

		VR_GetStatus (&st);
		if (!st.stage_space)
			Con_SafePrintf ("VR: warning: no floor-level tracking space (LOCAL), so %.2f m is not your real eye height; type vr_eye_height <metres> or use seated mode (vr_seated 1)\n", y);
		else if (y < 1.2f)
			Con_SafePrintf ("VR: that is low for standing; for seated play set vr_seated 1 and calibrate again\n");
		Cvar_SetValue ("vr_eye_height", y);
		Cvar_SetValue ("vr_height_offset", 0);
	}
	VRG_BodyCheck ();	/* vrg_eye_height, vrg_height_offset and vrg_body now follow the new values */
	VRG_Recenter ();
	vrg_cal_step = VRG_CAL_ARMLEN;
	VRG_CalPrompt ();
}

/* Step 2: the arm, from the main hand held out at shoulder height; head and hand in the corrected
 * space (with the offset just set), the body from vr_eye_height. */
static void VRG_CalArmStep (int hand)
{
	vr_pose_t	head;
	vr_hand_t	hands[2];
	float		arm;

	VRG_TrackedPoses (&head, NULL, hands);
	if (!VRG_PoseSane (&head))
	{
		Con_SafePrintf ("VR: calibration: head not tracked, try again\n");
		return;
	}
	if (!hands[hand].active || !hands[hand].pose_valid)
	{
		Con_SafePrintf ("VR: calibration: hand not tracked, try again\n");
		return;
	}
	if (!VR_CalibrateArm (&vrg_body, &head, &hands[hand].grip.pos, hand, &arm))
	{
		Con_SafePrintf ("VR: calibration: arm %.2f m out of range, try again\n",
				VR_ArmDistance (&vrg_body, &head, &hands[hand].grip.pos, hand));
		return;
	}
	Cvar_SetValue ("vr_body_arm_length", arm);
	VRG_BodyCheck ();
	VRG_CalEnd (false);
	Con_SafePrintf ("VR: calibration done: eye height %.2f m, arm %.2f m\n", vrg_eye_height, arm);
}

qboolean VRG_Calibrating (void)
{
	return vrg_cal_step != VRG_CAL_OFF;
}

/* A button went down (VRG_InputFrame; role 0 = main hand, 1 = the other). While calibrating in the game
 * (not in a menu, the console or a modal dialog): the main trigger confirms the step, the other hand's
 * trigger and B / Y cancel. A consumed press sends no key (and so leaves no key to release). */
qboolean VRG_CalibratePress (int hand, int role, unsigned bit)
{
	if (vrg_cal_step == VRG_CAL_OFF || vrg_input_modal || Key_GetDest () != key_game)
		return false;
	if (bit == VR_BTN_UPPER || (bit == VR_BTN_TRIGGER && role == 1))
	{
		VRG_CalEnd (true);
		return true;
	}
	if (bit == VR_BTN_TRIGGER && role == 0)
	{
		if (vrg_cal_step == VRG_CAL_EYE)
			VRG_CalEyeStep ();
		else
			VRG_CalArmStep (hand);
		return true;
	}
	return false;
}

/* every frame: keeps the prompt on the screen (a centerprint fades after scr_centertime) */
void VRG_CalibrateFrame (void)
{
	if (vrg_cal_step != VRG_CAL_OFF && Sys_DoubleTime () - vrg_cal_print >= 1.0)
		VRG_CalPrompt ();
}

/* VR starts or stops: a calibration in progress is dropped without a message */
void VRG_CalibrateReset (void)
{
	if (vrg_cal_step != VRG_CAL_OFF)
		VRG_CalEnd (false);
}

static void VRG_Calibrate_f (void)
{
	if (Cmd_Argc () > 2 || (Cmd_Argc () == 2 && q_strcasecmp (Cmd_Argv (1), "cancel")))
	{
		Con_Printf ("VR: usage: vr_calibrate [cancel]\n");
		return;
	}
	if (Cmd_Argc () == 2)
	{
		if (vrg_cal_step == VRG_CAL_OFF)
			Con_Printf ("VR: not calibrating\n");
		else
			VRG_CalEnd (true);
		return;
	}
	if (!VR_IsRunning ())
	{
		Con_Printf ("VR: not running\n");
		return;
	}
	vrg_cal_step = VRG_CAL_EYE;
	VRG_CalPrompt ();
	/* the command was typed in the console, where the trigger would be Enter: in the running game the
	 * console closes (as Host_Connect_f does), so the trigger can confirm the steps. Without a game (menu,
	 * the console forced up over nothing) there is nothing to close: the calibration waits for one. */
	if (cls.state == ca_connected && cls.signon == SIGNONS && !con_forcedup)
	{
		if (Key_GetDest () == key_console)
			Key_SetDest (key_game);
	}
	else
		Con_Printf ("VR: calibration continues in the game\n");
}
