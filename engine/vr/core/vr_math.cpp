// vr_math.cpp -- pure math of the hxvr C API (tracking -> Quake conversion). No engine headers.
#include "vr_api.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace {

// OpenXR (x right, y up, -z forward) -> Quake (x forward, y left, z up).
glm::vec3 quake_from_tracking(const glm::vec3& v) { return glm::vec3(-v.z, -v.x, v.y); }

constexpr float kRadToDeg = 57.295779513082320876798f;

}  // namespace

extern "C" {

// Hexen II eye is 50 units above the origin (feet): the player's real eye height maps to 50 units.
float VR_UnitsPerMetre(float eye_height_m, float world_scale) {
	if (eye_height_m <= 0.0f) return 0.0f;
	return 50.0f / eye_height_m * world_scale;
}

void VR_TrackingToQuakePos(const vr_vec3_t *v, float upm, float out[3]) {
	const glm::vec3 q = quake_from_tracking(glm::vec3(v->x, v->y, v->z)) * upm;
	out[0] = q.x;
	out[1] = q.y;
	out[2] = q.z;
}

// Output [PITCH, YAW, ROLL] in degrees, Quake convention (inverse of AngleVectors in mathlib.c).
void VR_TrackingToQuakeAngles(const vr_quat_t *qin, float out[3]) {
	const glm::quat q = glm::normalize(glm::quat(qin->w, qin->x, qin->y, qin->z));
	const glm::vec3 fwd = quake_from_tracking(q * glm::vec3(0, 0, -1));
	const glm::vec3 right = quake_from_tracking(q * glm::vec3(1, 0, 0));
	const glm::vec3 up = quake_from_tracking(q * glm::vec3(0, 1, 0));

	// forward = (cp*cy, cp*sy, -sp); right = (sr*sp*cy + cr*sy ..., -(sr*sp*sy + cr*cy), -sr*cp)
	const float horiz = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y);
	float pitch = std::atan2(-fwd.z, horiz) * kRadToDeg;
	float yaw, roll;
	if (horiz > 1e-6f) {
		yaw = std::atan2(fwd.y, fwd.x) * kRadToDeg;
		roll = std::atan2(-right.z, up.z) * kRadToDeg;
	} else {
		// Looking straight up/down: yaw and roll are degenerate; fold all into yaw, roll = 0.
		// With roll = 0, right = (sy, -cy, 0).
		yaw = std::atan2(right.x, -right.y) * kRadToDeg;
		roll = 0.0f;
	}
	out[0] = pitch;
	out[1] = yaw;
	out[2] = roll;
}

void VR_EyeOrigin(const float feet[3], float body_yaw, const vr_vec3_t *eye_pos,
                  const vr_vec3_t *anchor, float upm, float out[3]) {
	// Horizontal: (eye - anchor) in x,z -> Quake axes, rotated by the body yaw, scaled.
	const glm::vec3 d = quake_from_tracking(glm::vec3(eye_pos->x - anchor->x, 0.0f, eye_pos->z - anchor->z));
	const float a = body_yaw * (3.14159265358979323846f / 180.0f);
	const float c = std::cos(a), s = std::sin(a);
	out[0] = feet[0] + (d.x * c - d.y * s) * upm;
	out[1] = feet[1] + (d.x * s + d.y * c) * upm;
	// Vertical: absolute eye height above the floor (anchor.y ignored).
	out[2] = feet[2] + eye_pos->y * upm;
}

}  // extern "C"
