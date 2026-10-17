/* Client weapon pose, shared by input and the VR move block. Engine-side C glue. */
#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"
#include "vr_glue_weapon.h"
#include <stdlib.h>

cvar_t vr_compat = {"vr_compat", "1", CVAR_ARCHIVE};
cvar_t vr_gunangle = {"vr_gunangle", "0", CVAR_ARCHIVE};
cvar_t vr_viewmodel = {"vr_viewmodel", "1", CVAR_ARCHIVE};
static float vrg_viewmodel_scale = 1;

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
	Cmd_AddCommand ("vr_weapon_offset", VRG_WeaponOffset_f);
	Cmd_AddCommand ("vr_weapon_list", VRG_WeaponList_f);
}

static qmodel_t *VRG_WeaponModel (void)
{
	int index = cl.stats[STAT_WEAPON];
	return (index > 0 && index < MAX_MODELS) ? cl.model_precache[index] : NULL;
}

static void VRG_HandWeaponPose (float muzzle_world[3], float aim_angles[3], qboolean *valid)
{
	vr_pose_t head;
	vr_hand_t hands[2];
	const vr_hand_t *hand;
	float angles[3], upm;
	vec3_t feet;

	*valid = false;
	VectorClear (muzzle_world);
	VectorClear (aim_angles);
	if (!VR_IsRunning ())
		return;
	VRG_TrackedPoses (&head, NULL, hands);
	hand = &hands[VRG_MainHand ()];
	if (!hand->active || !hand->pose_valid)
		return;
	VectorCopy (cl_entities[cl.viewentity].origin, feet);
	upm = VR_UnitsPerMetre (vrg_eye_height, vrg_world_scale);
	VR_EyeOrigin (feet, vrg_body_yaw, vrg_room.lean, &head.pos, &hand->aim.pos,
			upm, VRG_HeightScale (), muzzle_world);
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
	VR_WeaponTransform (hand_org, hand_ang, &entry, model->mins, model->maxs, 1,
			model_org, model_ang, muzzle, aim);
	return entry.scale;
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
	e->model = model;
	vrg_viewmodel_scale = VRG_TransformWeapon (model, hand_org, hand_ang,
			e->origin, e->angles, muzzle, aim);
	return true;
}

float VRG_ViewModelScale (void)
{
	return vrg_viewmodel_scale;
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
