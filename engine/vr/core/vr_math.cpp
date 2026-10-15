// vr_math.cpp -- pure math of the hxvr C API (tracking -> Quake conversion). No engine headers.
#include "vr_api.h"

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>

namespace {

// OpenXR (x right, y up, -z forward) -> Quake (x forward, y left, z up).
glm::vec3 quake_from_tracking(const glm::vec3& v) { return glm::vec3(-v.z, -v.x, v.y); }

constexpr float kRadToDeg = 57.295779513082320876798f;

// Same as TurnVector in engine/hexen2/gl_rmain.c: forward rotated towards side by angle (degrees).
glm::vec3 turn_vector(const glm::vec3& forward, const glm::vec3& side, float angle_deg) {
	const double a = angle_deg * (3.14159265358979323846 / 180.0);
	return static_cast<float>(std::cos(a)) * forward + static_cast<float>(std::sin(a)) * side;
}

// Quake AngleVectors (engine/h2shared/mathlib.c); angles = [PITCH, YAW, ROLL] degrees.
void quake_angle_vectors(const float angles[3], glm::vec3& forward, glm::vec3& right, glm::vec3& up) {
	const double k = 3.14159265358979323846 * 2.0 / 360.0;
	double angle = angles[1] * k;
	const float sy = static_cast<float>(std::sin(angle)), cy = static_cast<float>(std::cos(angle));
	angle = angles[0] * k;
	const float sp = static_cast<float>(std::sin(angle)), cp = static_cast<float>(std::cos(angle));
	angle = angles[2] * k;
	const float sr = static_cast<float>(std::sin(angle)), cr = static_cast<float>(std::cos(angle));

	forward = glm::vec3(cp * cy, cp * sy, -sp);
	right = glm::vec3(-1 * (sr * sp * cy + cr * -sy), -1 * (sr * sp * sy + cr * cy), -1 * (sr * cp));
	up = glm::vec3(cr * sp * cy + -sr * -sy, cr * sp * sy + -sr * cy, cr * cp);
}

// Angle in degrees wrapped to (-180, 180].
float normalize180(float a) {
	float r = std::fmod(a, 360.0f);
	if (r > 180.0f) r -= 360.0f;
	else if (r <= -180.0f) r += 360.0f;
	return r;
}

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

// Column-major, equivalent to glFrustum(tan(l)*n, tan(r)*n, tan(d)*n, tan(u)*n, n, f).
void VR_ProjectionMatrix(const vr_fov_t *fov, float znear, float zfar, float out[16]) {
	const float l = std::tan(fov->left) * znear, r = std::tan(fov->right) * znear;
	const float b = std::tan(fov->down) * znear, t = std::tan(fov->up) * znear;
	const glm::mat4 m = glm::frustumRH_NO(l, r, b, t, znear, zfar);
	std::memcpy(out, glm::value_ptr(m), sizeof(float) * 16);
}

// Equivalent to the R_SetupGL modelview sequence in engine/hexen2/gl_rmain.c.
void VR_QuakeViewMatrix(const float org[3], const float angles[3], float out[16]) {
	const glm::vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);
	glm::mat4 m(1.0f);
	m = glm::rotate(m, glm::radians(-90.0f), X);  // put Z going up
	m = glm::rotate(m, glm::radians(90.0f), Z);
	m = glm::rotate(m, glm::radians(-angles[2]), X);  // roll
	m = glm::rotate(m, glm::radians(-angles[0]), Y);  // pitch
	m = glm::rotate(m, glm::radians(-angles[1]), Z);  // yaw
	m = glm::translate(m, glm::vec3(-org[0], -org[1], -org[2]));
	std::memcpy(out, glm::value_ptr(m), sizeof(float) * 16);
}

// out = left, right, bottom, top; inward-pointing unit normals (same order as frustum[0..3] in
// R_SetFrustum, fov == 90 path). Each edge turns the view direction by (90 - edge half-angle)
// towards the inside of the frustum.
void VR_FrustumNormals(const float angles[3], const vr_fov_t *fov, float out[4][3]) {
	glm::vec3 fwd, right, up;
	quake_angle_vectors(angles, fwd, right, up);
	const float l = fov->left * kRadToDeg, r = fov->right * kRadToDeg;
	const float u = fov->up * kRadToDeg, d = fov->down * kRadToDeg;
	const glm::vec3 n[4] = {
		turn_vector(fwd, right, 90.0f + l),  // left edge: turn towards +right
		turn_vector(fwd, right, r - 90.0f),  // right edge: turn towards -right
		turn_vector(fwd, up, 90.0f + d),     // bottom edge: turn towards +up
		turn_vector(fwd, up, u - 90.0f),     // top edge: turn towards -up
	};
	for (int i = 0; i < 4; ++i) {
		out[i][0] = n[i].x;
		out[i][1] = n[i].y;
		out[i][2] = n[i].z;
	}
}

// View angles of an eye: head pitch and roll; yaw = body yaw + head yaw, wrapped to (-180, 180].
void VR_ComposeViewAngles(float body_yaw, const float head_angles[3], float out[3]) {
	const float pitch = head_angles[0], yaw = head_angles[1], roll = head_angles[2];
	out[0] = pitch;
	out[1] = normalize180(body_yaw + yaw);
	out[2] = roll;
}

// Fixed 72 Hz server clock. Accumulates the render dt and returns how many 1/72 s ticks are due,
// at most kMaxSimTicks; whatever is left over beyond that is discarded (no catch-up burst).
// While paused nothing is accumulated and no ticks are due.
int VR_SimTicks(vr_simclock_t *c, double dt, int paused) {
	constexpr double kTick = 1.0 / 72.0;
	constexpr double kEps = 1e-9;  // a frame of exactly one tick must not lose it to rounding
	constexpr int kMaxSimTicks = 4;
	if (!c) return 0;
	if (paused || !std::isfinite(dt) || !(dt > 0.0)) return 0;
	c->acc += dt;
	// O(1) whatever dt is (host_framerate can bypass the 0.05 s clamp): whole ticks due, capped,
	// remainder (< one tick) kept.
	const double ticks = c->acc / kTick;
	const double whole = std::floor(ticks + kEps / kTick);
	const int n = whole >= kMaxSimTicks ? kMaxSimTicks : static_cast<int>(whole);
	double rest = (ticks - whole) * kTick;
	if (!(rest >= 0.0)) rest = 0.0;
	if (rest >= kTick) rest = 0.0;
	c->acc = rest;
	return n;
}

}  // extern "C"
