#include "doctest.h"
#include "vr_api.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <random>

namespace {

struct V3 { float v[3]; };

bool vec_eq(const float a[3], V3 b, float eps = 1e-3f) {
	for (int i = 0; i < 3; ++i)
		if (std::fabs(a[i] - b.v[i]) > eps) return false;
	return true;
}

// Copy of the formula in engine/h2shared/mathlib.c AngleVectors (angles = [PITCH, YAW, ROLL], degrees).
void quake_angle_vectors(const float angles[3], float forward[3], float right[3], float up[3]) {
	const double k = 3.14159265358979323846 * 2.0 / 360.0;
	double angle = angles[1] * k;
	const float sy = (float)std::sin(angle), cy = (float)std::cos(angle);
	angle = angles[0] * k;
	const float sp = (float)std::sin(angle), cp = (float)std::cos(angle);
	angle = angles[2] * k;
	const float sr = (float)std::sin(angle), cr = (float)std::cos(angle);

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	right[0] = -1 * (sr * sp * cy + cr * -sy);
	right[1] = -1 * (sr * sp * sy + cr * cy);
	right[2] = -1 * (sr * cp);
	up[0] = (cr * sp * cy + -sr * -sy);
	up[1] = (cr * sp * sy + -sr * cy);
	up[2] = cr * cp;
}

vr_quat_t quat_axis_deg(glm::vec3 axis, float deg) {
	glm::quat q = glm::angleAxis(glm::radians(deg), glm::normalize(axis));
	return vr_quat_t{q.x, q.y, q.z, q.w};
}

void angles_of(const vr_quat_t& q, float out[3]) { VR_TrackingToQuakeAngles(&q, out); }

bool angles_eq(const float a[3], V3 b, float eps = 1e-3f) { return vec_eq(a, b, eps); }

// q * v (OpenXR axes) converted into Quake axes.
void rotated_to_quake(const vr_quat_t& q, glm::vec3 v, float out[3]) {
	const glm::vec3 r = glm::quat(q.w, q.x, q.y, q.z) * v;
	const vr_vec3_t t{r.x, r.y, r.z};
	VR_TrackingToQuakePos(&t, 1.0f, out);
}

}  // namespace

TEST_CASE("position axes") {
	vr_vec3_t fwd{0, 0, -1}, right{1, 0, 0}, up{0, 1, 0};
	float o[3];
	VR_TrackingToQuakePos(&fwd, 10.f, o);
	CHECK(vec_eq(o, {{10, 0, 0}}));
	VR_TrackingToQuakePos(&right, 10.f, o);
	CHECK(vec_eq(o, {{0, -10, 0}}));
	VR_TrackingToQuakePos(&up, 10.f, o);
	CHECK(vec_eq(o, {{0, 0, 10}}));
}

TEST_CASE("angles: explicit cases") {
	float a[3];
	angles_of(quat_axis_deg({0, 1, 0}, 90), a);  // turn left
	CHECK(angles_eq(a, {{0, 90, 0}}));
	angles_of(quat_axis_deg({1, 0, 0}, 30), a);  // look up
	CHECK(angles_eq(a, {{-30, 0, 0}}));
	angles_of(quat_axis_deg({0, 0, 1}, 20), a);  // tilt left
	CHECK(angles_eq(a, {{0, 0, -20}}));
	angles_of(vr_quat_t{0, 0, 0, 1}, a);  // identity
	CHECK(angles_eq(a, {{0, 0, 0}}));
}

TEST_CASE("angles: property over 200 random rotations") {
	std::mt19937 rng(12345);
	std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
	int checked = 0;
	while (checked < 200) {
		glm::quat q(uni(rng), uni(rng), uni(rng), uni(rng));
		if (glm::length(q) < 0.1f) continue;
		q = glm::normalize(q);
		const vr_quat_t vq{q.x, q.y, q.z, q.w};

		float angles[3], f[3], r[3], u[3], ef[3], er[3], eu[3];
		VR_TrackingToQuakeAngles(&vq, angles);
		quake_angle_vectors(angles, f, r, u);
		rotated_to_quake(vq, {0, 0, -1}, ef);
		rotated_to_quake(vq, {1, 0, 0}, er);
		rotated_to_quake(vq, {0, 1, 0}, eu);

		CHECK(vec_eq(f, {{ef[0], ef[1], ef[2]}}, 1e-4f));
		CHECK(vec_eq(r, {{er[0], er[1], er[2]}}, 1e-4f));
		CHECK(vec_eq(u, {{eu[0], eu[1], eu[2]}}, 1e-4f));
		++checked;
	}
}

TEST_CASE("angles: straight up/down gimbal does not produce NaN") {
	float a[3];
	angles_of(quat_axis_deg({1, 0, 0}, 90), a);  // straight up
	for (int i = 0; i < 3; ++i) CHECK(std::isfinite(a[i]));
	CHECK(a[0] == doctest::Approx(-90.0f).epsilon(1e-3));
	angles_of(quat_axis_deg({1, 0, 0}, -90), a);  // straight down
	for (int i = 0; i < 3; ++i) CHECK(std::isfinite(a[i]));
	CHECK(a[0] == doctest::Approx(90.0f).epsilon(1e-3));
}

TEST_CASE("eye origin") {
	float feet[3] = {100, 200, 0};
	vr_vec3_t eye{0.f, 1.65f, 0.f}, anchor{0, 0, 0};
	float o[3];
	VR_EyeOrigin(feet, 0.f, &eye, &anchor, VR_UnitsPerMetre(1.65f, 1.f), o);
	CHECK(vec_eq(o, {{100, 200, 50}}));
	vr_vec3_t step{0.f, 1.65f, -0.5f};  // step forward 0.5 m
	VR_EyeOrigin(feet, 90.f, &step, &anchor, 30.f, o);  // body turned 90 degrees (faces +Y)
	CHECK(vec_eq(o, {{100, 215, 49.5f}}));
}

TEST_CASE("eye origin: anchor offsets horizontally, ignored vertically") {
	float feet[3] = {0, 0, 0};
	vr_vec3_t eye{0.2f, 1.65f, -0.5f}, anchor{0.2f, 0.7f, -0.5f};
	float o[3];
	VR_EyeOrigin(feet, 0.f, &eye, &anchor, 30.f, o);
	CHECK(vec_eq(o, {{0, 0, 49.5f}}));
}
