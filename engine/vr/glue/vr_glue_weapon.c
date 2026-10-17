/* Client weapon pose, shared by input and the VR move block. Engine-side C glue. */
#include "quakedef.h"
#include "vr_api.h"
#include "vr_glue.h"
#include "vr_glue_int.h"
#include "vr_glue_weapon.h"

cvar_t vr_compat = {"vr_compat", "1", CVAR_ARCHIVE};
cvar_t vr_gunangle = {"vr_gunangle", "0", CVAR_ARCHIVE};

void VRG_WeaponInit (void)
{
	Cvar_RegisterVariable (&vr_compat);
	Cvar_RegisterVariable (&vr_gunangle);
}

void VRG_WeaponPose (float muzzle_world[3], float aim_angles[3], qboolean *valid)
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
	hand = &hands[vr_mainhand.integer == VR_HAND_LEFT ? VR_HAND_LEFT : VR_HAND_RIGHT];
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
