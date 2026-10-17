#include "vr_api.h"
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Override { std::string name; vr_weapon_entry_t entry; };
std::vector<Override> overrides;
bool valid(const vr_weapon_entry_t *e) {
	if (!e || !std::isfinite(e->scale) || e->scale <= 0) return false;
	for (int i = 0; i < 3; ++i)
		if (!std::isfinite(e->offset[i]) || !std::isfinite(e->angles[i])) return false;
	return true;
}
glm::mat3 rotation(const float a[3]) {
	glm::mat4 r(1);
	r = glm::rotate(r, glm::radians(a[1]), glm::vec3(0,0,1));
	r = glm::rotate(r, glm::radians(a[0]), glm::vec3(0,1,0));
	r = glm::rotate(r, glm::radians(a[2]), glm::vec3(1,0,0));
	return glm::mat3(r);
}
void store(const glm::vec3& v, float out[3]) {
	for (int i = 0; i < 3; ++i) out[i] = v[i];
}
}

extern "C" {
void VR_WeaponGet(const char *model, const float mins[3], const float maxs[3], vr_weapon_entry_t *out) {
	if (!out) return;
	for (const auto& o : overrides) if (model && o.name == model) { *out = o.entry; return; }
	*out = {};
	for (int i = 0; i < 3; ++i) out->offset[i] = -(mins[i] + maxs[i]) * 0.5f;
	out->scale = 1;
}
int VR_WeaponSet(const char *model, const vr_weapon_entry_t *e) {
	if (!model || !*model || std::strchr(model, '"') || !valid(e)) return 0;
	for (auto& o : overrides) if (o.name == model) { o.entry = *e; return 1; }
	overrides.push_back({model, *e});
	return 1;
}
int VR_WeaponCount(void) { return static_cast<int>(overrides.size()); }
void VR_WeaponClear(void) { overrides.clear(); }
const char *VR_WeaponName(int i) {
	return i >= 0 && i < VR_WeaponCount() ? overrides[i].name.c_str() : nullptr;
}
int VR_WeaponFormat(const char *model, const vr_weapon_entry_t *e, char *buf, int cap) {
	if (!model || !*model || std::strchr(model, '"') || !valid(e) || !buf || cap <= 0) return 0;
	const int n = std::snprintf(buf, static_cast<size_t>(cap),
		"vr_weapon_offset \"%s\" %g %g %g %g %g %g %g\n", model,
		e->offset[0], e->offset[1], e->offset[2], e->angles[0], e->angles[1], e->angles[2], e->scale);
	return n > 0 && n < cap ? n : 0;
}
void VR_WeaponTransform(const float hand_org[3], const float hand_ang[3], const vr_weapon_entry_t *e,
                        const float mins[3], const float maxs[3], float scale_units,
                        float model_org[3], float model_ang[3], float muzzle[3], float aim_ang[3]) {
	const glm::mat3 hand = rotation(hand_ang), model = hand * rotation(e->angles);
	const float s = e->scale * scale_units;
	const glm::vec3 org = glm::vec3(hand_org[0],hand_org[1],hand_org[2])
		+ hand * (s * glm::vec3(e->offset[0],e->offset[1],e->offset[2]));
	store(org, model_org);
	store(org + model * (s * glm::vec3(maxs[0], (mins[1]+maxs[1])*0.5f,
	                                                (mins[2]+maxs[2])*0.5f)), muzzle);
	const float horiz = std::hypot(model[0].x, model[0].y);
	aim_ang[0] = glm::degrees(std::atan2(-model[0].z, horiz));
	if (horiz > 1e-6f) {
		aim_ang[1] = glm::degrees(std::atan2(model[0].y, model[0].x));
		aim_ang[2] = glm::degrees(std::atan2(model[1].z, model[2].z));
	} else {
		aim_ang[1] = glm::degrees(std::atan2(-model[1].x, model[1].y));
		aim_ang[2] = 0;
	}
	model_ang[0] = -aim_ang[0];
	model_ang[1] = aim_ang[1];
	model_ang[2] = -aim_ang[2];
}
}
