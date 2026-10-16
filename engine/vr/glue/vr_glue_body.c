/*
 * vr_glue_body.c -- engine-side glue for the hxvr VR module: the player's body model (metres), the tracked
 * poses with the height offset applied, the seated mode cvars and the vr_calibrate command.
 * C, compiled into the engine; includes engine headers and vr_api.h only.
 *
 * The body is in metres of tracking space: nothing here uses vr_world_scale.
 */

#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"

cvar_t	vr_seated = {"vr_seated", "0", CVAR_ARCHIVE};			/* 1: no physical crouch, height from vr_height_offset */
cvar_t	vr_height_offset = {"vr_height_offset", "0", CVAR_ARCHIVE};	/* metres, added to the y of head and hands */
cvar_t	vr_body_arm_length = {"vr_body_arm_length", "0", CVAR_ARCHIVE};	/* metres, 0 = from the proportions */
cvar_t	vr_body_shoulder_width = {"vr_body_shoulder_width", "0", CVAR_ARCHIVE};	/* metres, 0 = from the proportions */
cvar_t	vr_crouch_threshold = {"vr_crouch_threshold", "0.65", CVAR_ARCHIVE};	/* fraction of vr_eye_height */

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
}
