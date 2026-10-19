/* Client weapon pose, shared by input and the VR move block. Engine-side C glue. */
#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"
#include "vr_glue_weapon.h"
#include <stdlib.h>
#include <stdio.h>

#define VRG_TUNE_PROMPT "Weapon tuning: hold the off-hand grip to move the weapon,@off-hand stick up/down to scale, hold B+Y to save@(or the console command vr_weapon_save)"

cvar_t vr_compat = {"vr_compat", "1", CVAR_ARCHIVE};
cvar_t vr_gunangle = {"vr_gunangle", "0", CVAR_ARCHIVE};
cvar_t vr_viewmodel = {"vr_viewmodel", "1", CVAR_ARCHIVE};
static cvar_t vr_aimdot = {"vr_aimdot", "1", CVAR_ARCHIVE};
static cvar_t vr_offhand_marker = {"vr_offhand_marker", "1", CVAR_ARCHIVE};
static cvar_t vr_haptics = {"vr_haptics", "1", CVAR_ARCHIVE};
static cvar_t vr_haptics_strength = {"vr_haptics_strength", "1.0", CVAR_ARCHIVE};
/* Hexen II progs sound channel: CHAN_WEAPON = 1 (no shared engine C constant). */
#define VRG_CHAN_WEAPON 1
static qboolean vrg_marker_valid, vrg_dot_valid;
static vec3_t vrg_marker_org, vrg_dot_org;
static float vrg_dot_size;
static float vrg_viewmodel_scale = 1;
static qboolean vrg_tuning, vrg_grabbed, vrg_grip_down;
static int vrg_tune_hand;
static qmodel_t *vrg_tune_model;
static vr_weapon_entry_t vrg_tune_entry, vrg_grab_relative;
static double vrg_last_weapon_pulse = -1;

qboolean VRG_WeaponTuning (void) { return vrg_tuning && VR_IsRunning (); }

qboolean VRG_HapticsEnabled (void)
{
	return VR_IsRunning () && !cls.demoplayback && vr_haptics.integer;
}

static void VRG_Haptic (int hand, float amplitude, float seconds, const char *source)
{
	float strength;

	if (!VRG_HapticsEnabled ()) return;
	strength = vr_haptics_strength.value;
	if (!(strength > 0)) return; /* Also rejects NaN. */
	if (strength > 1) strength = 1;
	amplitude *= strength;
	VR_Haptic (hand, amplitude, seconds);
	if (vr_debug_fire.value)
		Con_SafePrintf ("VR: haptic %d %g %g %s\n", hand, amplitude, seconds, source);
}

/* Sound packets and local-server spawns share one monotonic host clock/debounce.
 * Damage pulses are independent of the weapon debounce. */
static void VRG_WeaponPulse (const char *source)
{
	if (!VRG_HapticsEnabled () || !(vr_haptics_strength.value > 0)) return;
	if (realtime >= vrg_last_weapon_pulse && realtime - vrg_last_weapon_pulse < 0.05)
		return;
	vrg_last_weapon_pulse = realtime;
	VRG_Haptic (VRG_MainHand (), 0.6f, 0.04f, source);
}

void VRG_OnWeaponFire (void) { VRG_WeaponPulse ("spawn"); }

void VRG_OnSound (int ent, int channel)
{
	if (!cls.demoplayback && ent == cl.viewentity && channel == VRG_CHAN_WEAPON)
		VRG_WeaponPulse ("sound");
}

void VRG_OnDamage (float count)
{
	float amplitude;

	if (cls.demoplayback || !(count >= 0)) return;
	amplitude = count / 40.0f;
	if (amplitude < 0.3f) amplitude = 0.3f;
	if (amplitude > 1) amplitude = 1;
	VRG_Haptic (VR_HAND_LEFT, amplitude, 0.12f, "damage");
	VRG_Haptic (VR_HAND_RIGHT, amplitude, 0.12f, "damage");
}

void VRG_WeaponTuneReset (void)
{
	vrg_tuning = vrg_grabbed = vrg_grip_down = false;
	vrg_tune_model = NULL;
}

static void VRG_WeaponSave_f (void);

static void VRG_PulseBoth (void)
{
	VRG_Haptic (VR_HAND_LEFT, 0.5f, 0.06f, "tune");
	VRG_Haptic (VR_HAND_RIGHT, 0.5f, 0.06f, "tune");
}

static void VRG_WeaponTuneEnter (void)
{
	vrg_tuning = true;
	vrg_tune_hand = VRG_MainHand ();
	SCR_CenterPrint (VRG_TUNE_PROMPT);
}

/* Leaves tuning: an in-progress grab is committed as on a grip release. */
static void VRG_WeaponTuneLeave (void)
{
	if (vrg_grabbed && vrg_tune_model)
		VR_WeaponSet (vrg_tune_model->name, &vrg_tune_entry);
	VRG_WeaponTuneReset ();
	SCR_CenterPrint ("");
}

static void VRG_WeaponTune_f (void)
{
	if (vrg_tuning)
	{
		VRG_WeaponTuneLeave ();
		return;
	}
	if (!VR_IsRunning ()) return;
	VRG_WeaponTuneEnter ();
}

/* The B+Y chord (vr_glue_input.c): on enters tuning; off saves, then a few frames later takes a mirror
 * screenshot and pulses twice. */
#define VRG_SHOT_FRAMES 3
#define VRG_PULSE_GAP 0.15
static int vrg_shot_frames;
static double vrg_pulse_due;

void VRG_WeaponTuneChord (void)
{
	if (!VR_IsRunning ()) return;
	if (!vrg_tuning)
	{
		VRG_WeaponTuneEnter ();
		VRG_PulseBoth ();
		return;
	}
	VRG_WeaponTuneLeave ();
	VRG_WeaponSave_f ();
	vrg_shot_frames = VRG_SHOT_FRAMES;
	VRG_PulseBoth ();
	vrg_pulse_due = Sys_DoubleTime () + VRG_PULSE_GAP;
}

/* The slot "screenshot" will use (the same scan as SCR_ScreenShot_f). */
#if !defined(H2W)
static const char vrg_shotbase[] = "shots/hexen00.tga";
#define VRG_SHOTNUM_POS 11
#else
static const char vrg_shotbase[] = "shots/hw00.tga";
#define VRG_SHOTNUM_POS 8
#endif

static void VRG_TuneShot (void)
{
	char name[80], path[MAX_OSPATH];
	int i;

	if (vr_mirror.integer != 1 && vr_mirror.integer != 2)
	{
		Con_SafePrintf ("VR: weapon tuning screenshot skipped (vr_mirror 0)\n");
		return;
	}
	q_strlcpy (name, vrg_shotbase, sizeof(name));
	for (i = 0; i <= 99; i++)
	{
		name[VRG_SHOTNUM_POS + 0] = i / 10 + '0';
		name[VRG_SHOTNUM_POS + 1] = i % 10 + '0';
		FS_MakePath_BUF (FS_USERDIR, NULL, path, sizeof(path), name);
		if (Sys_FileType (path) == FS_ENT_NONE)
			break;
	}
	Cmd_ExecuteString ("screenshot", src_command);
	if (i < 100 && Sys_FileType (path) != FS_ENT_NONE)
		Con_SafePrintf ("VR: weapon tuning screenshot %s\n", name);
	else
		Con_SafePrintf ("VR: weapon tuning screenshot failed\n");
}

/* Once per input frame: the deferred screenshot and the second exit pulse. */
void VRG_WeaponChordFrame (void)
{
	if (vrg_pulse_due > 0 && Sys_DoubleTime () >= vrg_pulse_due)
	{
		vrg_pulse_due = 0;
		VRG_PulseBoth ();
	}
	if (vrg_shot_frames > 0 && --vrg_shot_frames == 0)
		VRG_TuneShot ();
}

static void VRG_WeaponSave_f (void)
{
	FILE *file;
	int i, n = 0, failed = 0;
	const char *name;
	const float bounds[3] = {0, 0, 0};
	vr_weapon_entry_t entry;
	char line[MAX_OSPATH + 256];

	if (vrg_grabbed && vrg_tune_model)
		VR_WeaponSet (vrg_tune_model->name, &vrg_tune_entry);
	file = fopen (FS_MakePath (FS_USERDIR, NULL, "vr_weapons.cfg"), "w");
	if (!file) { Con_SafePrintf ("VR: could not write vr_weapons.cfg\n"); return; }
	for (i = 0; i < VR_WeaponCount (); i++)
	{
		name = VR_WeaponName (i);
		VR_WeaponGet (name, bounds, bounds, &entry);
		if (!VR_WeaponFormat (name, &entry, line, sizeof(line)) || fputs (line, file) == EOF)
		{ failed = 1; break; }
		n++;
	}
	if (fclose (file)) failed = 1;
	if (failed) Con_SafePrintf ("VR: could not write vr_weapons.cfg\n");
	else Con_SafePrintf ("VR: saved %d weapon offsets to vr_weapons.cfg\n", n);
}

static void VRG_WeaponOffset_f (void)
{
	vr_weapon_entry_t entry;
	char *end;
	double value;
	int i;

	if (Cmd_Argc () != 9)
	{
		Con_SafePrintf ("Usage: vr_weapon_offset <model> <x> <y> <z> <pitch> <yaw> <roll> <scale>\n");
		return;
	}
	for (i = 0; i < 7; i++)
	{
		value = strtod (Cmd_Argv (i + 2), &end);
		if (end == Cmd_Argv (i + 2) || *end)
		{
			Con_SafePrintf ("VR: invalid weapon entry\n");
			return;
		}
		if (i < 3) entry.offset[i] = (float)value;
		else if (i < 6) entry.angles[i - 3] = (float)value;
		else entry.scale = (float)value;
	}
	if (!VR_WeaponSet (Cmd_Argv (1), &entry))
		Con_SafePrintf ("VR: invalid weapon entry\n");
}

static void VRG_WeaponList_f (void)
{
	int i;
	const char *name;
	const float bounds[3] = {0, 0, 0};
	vr_weapon_entry_t entry;
	char line[MAX_OSPATH + 256];

	for (i = 0; i < VR_WeaponCount (); i++)
	{
		name = VR_WeaponName (i);
		VR_WeaponGet (name, bounds, bounds, &entry);
		if (VR_WeaponFormat (name, &entry, line, sizeof(line)))
			Con_SafePrintf ("%s", line);
	}
}

void VRG_WeaponInit (void)
{
	Cvar_RegisterVariable (&vr_compat);
	Cvar_RegisterVariable (&vr_gunangle);
	Cvar_RegisterVariable (&vr_viewmodel);
	Cvar_RegisterVariable (&vr_aimdot);
	Cvar_RegisterVariable (&vr_offhand_marker);
	Cvar_RegisterVariable (&vr_haptics);
	Cvar_RegisterVariable (&vr_haptics_strength);
	Cmd_AddCommand ("vr_weapon_offset", VRG_WeaponOffset_f);
	Cmd_AddCommand ("vr_weapon_list", VRG_WeaponList_f);
	Cmd_AddCommand ("vr_weapon_tune", VRG_WeaponTune_f);
	Cmd_AddCommand ("vr_weapon_save", VRG_WeaponSave_f);
}

static qmodel_t *VRG_WeaponModel (void)
{
	int index = cl.stats[STAT_WEAPON];
	return (index > 0 && index < MAX_MODELS) ? cl.model_precache[index] : NULL;
}

/* The same tracking-to-world path for weapon aim, tuning grip and off-hand marker. */
static void VRG_HandOrigin (const vr_pose_t *head, const vr_pose_t *pose, float org[3])
{
	VR_EyeOrigin (cl_entities[cl.viewentity].origin, vrg_body_yaw, vrg_room.lean,
		&head->pos, &pose->pos, VR_UnitsPerMetre (vrg_eye_height, vrg_world_scale),
		VRG_HeightScale (), org);
}

static void VRG_HandWeaponPose (float muzzle_world[3], float aim_angles[3], qboolean *valid)
{
	vr_pose_t head;
	vr_hand_t hands[2];
	const vr_hand_t *hand;
	float angles[3];

	*valid = false;
	VectorClear (muzzle_world);
	VectorClear (aim_angles);
	if (!VR_IsRunning ())
		return;
	VRG_TrackedPoses (&head, NULL, hands);
	hand = &hands[VRG_MainHand ()];
	if (!hand->active || !hand->pose_valid)
		return;
	VRG_HandOrigin (&head, &hand->aim, muzzle_world);
	VR_TrackingToQuakeAngles (&hand->aim.rot, angles);
	VR_ComposeViewAngles (vrg_body_yaw, angles, aim_angles);
	aim_angles[PITCH] += vr_gunangle.value;
	*valid = true;
}

static float VRG_TransformWeapon (qmodel_t *model, const float hand_org[3], const float hand_ang[3],
		float model_org[3], float model_ang[3], float muzzle[3], float aim[3])
{
	vr_weapon_entry_t entry;

	VR_WeaponGet (model->name, model->mins, model->maxs, &entry);
	if (vrg_grabbed && model == vrg_tune_model) entry = vrg_tune_entry;
	VR_WeaponTransform (hand_org, hand_ang, &entry, model->mins, model->maxs, 1,
			model_org, model_ang, muzzle, aim);
	return entry.scale;
}

/* Called once per input frame, never per eye or server tick. The grab offset has
 * scale 1 so scaling the model cannot scale its rigid displacement from the grip. */
void VRG_WeaponTuneFrame (float dt)
{
	vr_pose_t head;
	vr_hand_t hands[2], *other;
	qmodel_t *model;
	qboolean valid, down;
	float hand_org[3], hand_ang[3], other_org[3], other_ang[3], angles[3];
	float org[3], ang[3], muzzle[3], aim[3], stick[2], scale;

	if (!VRG_WeaponTuning ()) return;
	SCR_CenterPrint (VRG_TUNE_PROMPT);
	model = VRG_WeaponModel ();
	if (vrg_tune_hand != VRG_MainHand ()) { VRG_WeaponTuneReset (); return; }
	if (!model || cl.v.health <= 0 ||
		cl.intermission || cl.v.cameramode || cls.signon != SIGNONS)
	{ vrg_grabbed = false; vrg_grip_down = true; vrg_tune_model = NULL; return; }
	if (vrg_tune_model != model) { vrg_grabbed = false; vrg_grip_down = true; }
	vrg_tune_model = model;
	VRG_TrackedPoses (&head, NULL, hands);
	other = &hands[1 - VRG_MainHand ()];
	VRG_HandWeaponPose (hand_org, hand_ang, &valid);
	if (!valid || !other->active || !other->pose_valid || Key_GetDest () != key_game)
	{ vrg_grabbed = false; vrg_grip_down = true; return; }
	down = (other->buttons & VR_BTN_GRIP) != 0;
	VRG_HandOrigin (&head, &other->grip, other_org);
	VR_TrackingToQuakeAngles (&other->grip.rot, angles);
	VR_ComposeViewAngles (vrg_body_yaw, angles, other_ang);
	if (!vrg_grabbed) VR_WeaponGet (model->name, model->mins, model->maxs, &vrg_tune_entry);
	if (down && !vrg_grip_down)
	{
		VR_WeaponTransform (hand_org, hand_ang, &vrg_tune_entry, model->mins, model->maxs,
			1, org, ang, muzzle, aim);
		VR_WeaponFromPose (other_org, other_ang, org, ang, 1, &vrg_grab_relative);
		vrg_grabbed = true;
	}
	VR_StickDeadzone (other->stick, vr_stick_deadzone.value, stick);
	scale = vrg_tune_entry.scale * (1 + 0.5f * stick[1] * (dt > 0 ? q_min (dt, 0.1f) : 0));
	scale = q_min (q_max (scale, 0.25f), 4.0f);
	if (vrg_grabbed)
	{
		VR_WeaponTransform (other_org, other_ang, &vrg_grab_relative, model->mins, model->maxs,
			1, org, ang, muzzle, aim);
		VR_WeaponFromPose (hand_org, hand_ang, org, ang, scale, &vrg_tune_entry);
		if (!down) { VR_WeaponSet (model->name, &vrg_tune_entry); vrg_grabbed = false; }
	}
	else if (scale != vrg_tune_entry.scale)
	{
		vrg_tune_entry.scale = scale;
		VR_WeaponSet (model->name, &vrg_tune_entry);
	}
	vrg_grip_down = down;
}

void VRG_WeaponPose (float muzzle_world[3], float aim_angles[3], qboolean *valid)
{
	float hand_org[3], hand_ang[3], model_org[3], model_ang[3];
	qmodel_t *model;

	VRG_HandWeaponPose (hand_org, hand_ang, valid);
	VectorCopy (hand_org, muzzle_world);
	VectorCopy (hand_ang, aim_angles);
	if (!*valid)
		return;
	model = VRG_WeaponModel ();
	if (!model)
		return; /* No model: retain the aim-pose position and angles. */
	VRG_TransformWeapon (model, hand_org, hand_ang,
			model_org, model_ang, muzzle_world, aim_angles);
}

qboolean VRG_ViewModelPose (entity_t *e)
{
	float hand_org[3], hand_ang[3], muzzle[3], aim[3];
	qmodel_t *model;
	qboolean valid;

	if (!e || !VR_IsRunning () || !vr_viewmodel.integer || cl.v.health <= 0 ||
		chase_active.integer || cl.intermission || cl.v.cameramode)
		return false;
	model = VRG_WeaponModel ();
	if (!model || !e->model)
		return false;
	VRG_HandWeaponPose (hand_org, hand_ang, &valid);
	if (!valid)
		return false;
	hand_org[0] += vrg_camera_feet[0] - cl_entities[cl.viewentity].origin[0];
	hand_org[1] += vrg_camera_feet[1] - cl_entities[cl.viewentity].origin[1];
	hand_org[2] += vrg_camera_feet[2] - cl_entities[cl.viewentity].origin[2];
	e->model = model;
	vrg_viewmodel_scale = VRG_TransformWeapon (model, hand_org, hand_ang,
			e->origin, e->angles, muzzle, aim);
	return true;
}

float VRG_ViewModelScale (void)
{
	return vrg_viewmodel_scale;
}

/* Once per rendered frame before either eye: both eyes share one trace result. */
void VRG_WeaponIndicatorsFrame (qboolean world)
{
	vr_pose_t head;
	vr_hand_t hands[2], *other;
	qboolean valid;
	vec3_t muzzle, angles, forward, right, up, end, delta, start, clamped;
	int i;
	trace_t tr;
	edict_t *player;

	vrg_marker_valid = vrg_dot_valid = false;
	if (!VR_IsRunning () || !world || Key_GetDest () != key_game ||
		cl.intermission || cls.signon != SIGNONS || cl.v.cameramode || cl.v.health <= 0)
		return;
	if (vr_offhand_marker.integer)
	{
		VRG_TrackedPoses (&head, NULL, hands);
		other = &hands[1 - VRG_MainHand ()];
		if (other->active && other->pose_valid)
		{
			VRG_HandOrigin (&head, &other->grip, vrg_marker_org);
			for (i = 0; i < 3; i++)
				vrg_marker_org[i] += vrg_camera_feet[i] - cl_entities[cl.viewentity].origin[i];
			vrg_marker_valid = true;
		}
	}
	if (cls.demoplayback || !vr_aimdot.integer || !VRG_WeaponModel () || cl.v.health <= 0 || cl.v.cameramode)
		return;
	VRG_WeaponPose (muzzle, angles, &valid);
	if (!valid) return;
	AngleVectors (angles, forward, right, up);
	player = sv.active ? ((cl.viewentity > 0 && cl.viewentity < sv.num_edicts) ?
		EDICT_NUM(cl.viewentity) : svs.clients[0].edict) : NULL;
	if (player) {
		/* The move block carries an offset from client feet, applied at server feet. */
		for (i = 0; i < 3; i++)
			muzzle[i] += player->v.origin[i] - cl_entities[cl.viewentity].origin[i];
		VectorAdd (player->v.origin, player->v.view_ofs, start);
	}
	else {
		VectorCopy (cl_entities[cl.viewentity].origin, start);
		start[2] += cl.viewheight;
	}
	VRG_ClampMuzzle (player, start, muzzle, angles, clamped);
	VectorCopy (clamped, muzzle);
	VectorMA (muzzle, 4096.0f, forward, end);
	if (sv.active)
	{
		player = (cl.viewentity > 0 && cl.viewentity < sv.num_edicts) ?
			EDICT_NUM(cl.viewentity) : svs.clients[0].edict;
		tr = SV_Move (muzzle, vec3_origin, vec3_origin, end, MOVE_NORMAL, player);
	}
	else
		VRG_WorldTrace (muzzle, end, &tr);
	if (tr.fraction == 1) return;
	VectorSubtract (tr.endpos, muzzle, delta);
	vrg_dot_size = VR_AimDotSize ((float)VectorLength (delta));
	VectorMA (tr.endpos, -1.0f, forward, vrg_dot_org);
	vrg_dot_valid = true;
}

void VRG_DrawWeaponIndicators (const float right[3], const float up[3])
{
	static const float marker_color[4] = {0.8f, 0.8f, 1.0f, 0.8f};
	static const float dot_color[4] = {1.0f, 0.3f, 0.2f, 0.9f};

	if (vrg_marker_valid) VR_DrawMarker (vrg_marker_org, 1.5f, marker_color);
	if (vrg_dot_valid) VR_DrawAimDot (vrg_dot_org, right, up, vrg_dot_size, dot_color);
}

void VRG_AimAngles (float angles[3])
{
	float muzzle[3], head_angles[3];
	vr_pose_t head;
	qboolean valid;

	VRG_WeaponPose (muzzle, angles, &valid);
	if (!valid)
	{
		VRG_TrackedPoses (&head, NULL, NULL);
		VR_TrackingToQuakeAngles (&head.rot, head_angles);
		VR_ComposeViewAngles (vrg_body_yaw, head_angles, angles);
	}
}
