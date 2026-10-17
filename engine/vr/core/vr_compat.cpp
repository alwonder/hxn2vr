#include "vr_api.h"

#include <cmath>

void VR_CompatOrigin(const float muzzle[3], const float proj_ofs[3], float out[3]) {
	for (int i = 0; i < 3; ++i)
		out[i] = muzzle[i] - proj_ofs[i];
}

void VR_CompatClampMuzzle(const float start[3], const float muzzle[3], const float hit[3],
                          int blocked, float clearance, float out[3]) {
	if (!blocked) {
		for (int i = 0; i < 3; ++i) out[i] = muzzle[i];
		return;
	}
	const float delta[3] = {hit[0] - start[0], hit[1] - start[1], hit[2] - start[2]};
	const float length = std::sqrt(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);
	float scale = length > 0.0f ? 1.0f - clearance / length : 0.0f;
	if (scale < 0.0f) scale = 0.0f;
	for (int i = 0; i < 3; ++i)
		out[i] = start[i] + delta[i] * scale;
}

void VR_CompatRestore(const float real[3], const float swapped[3], const float after[3], float out[3]) {
	for (int i = 0; i < 3; ++i)
		out[i] = real[i] + (after[i] - swapped[i]);
}

int VR_CompatApplies(int enabled, double block_age, float health, int deadflag, int movetype,
                     int intermission, int camera) {
	// Hexen II server.h: WALK = 3, FLY = 5, NOCLIP = 8, SWIM = 14.
	return enabled && block_age >= 0.0 && block_age <= 0.1 && health > 0.0f && !deadflag &&
	       (movetype == 3 || movetype == 5 || movetype == 8 || movetype == 14) &&
	       !intermission && !camera;
}
