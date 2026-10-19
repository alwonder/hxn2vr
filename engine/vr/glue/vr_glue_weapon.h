/* Internal weapon glue; include after quakedef.h. */
#ifndef HXVR_VR_GLUE_WEAPON_H
#define HXVR_VR_GLUE_WEAPON_H

extern cvar_t vr_compat;
extern cvar_t vr_gunangle;
void VRG_WeaponInit (void);
qboolean VRG_WeaponTuning (void);
void VRG_WeaponTuneFrame (float dt);
void VRG_WeaponTuneReset (void);
void VRG_WeaponTuneChord (void);
void VRG_WeaponChordFrame (void);
void VRG_WeaponPose (float muzzle_world[3], float aim_angles[3], qboolean *valid);
/* Shared sent-view selection: weapon aim when tracked, otherwise body plus head. */
void VRG_AimAngles (float angles[3]);
/* Cache once after room lean is updated, then draw marker/dot in each eye's world matrices. */
void VRG_WeaponIndicatorsFrame (qboolean world);
void VRG_DrawWeaponIndicators (const float right[3], const float up[3]);

#endif /* HXVR_VR_GLUE_WEAPON_H */
