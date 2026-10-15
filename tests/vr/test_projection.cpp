#include "doctest.h"
#include "quake_test_math.hpp"
#include "vr_api.h"

#include <cmath>
#include <random>

using doctest::Approx;

namespace {

constexpr double kPi = 3.14159265358979323846;
float rad(float deg) { return (float)(deg * kPi / 180.0); }

vr_fov_t make_fov(float left_deg, float right_deg, float up_deg, float down_deg) {
	return vr_fov_t{rad(left_deg), rad(right_deg), rad(up_deg), rad(down_deg)};
}

float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void normalize3(float v[3]) {
	const float l = std::sqrt(dot3(v, v));
	for (int i = 0; i < 3; ++i) v[i] /= l;
}

// Column-major 4x4 times (x, y, z, 1); returns the xyz part (w ignored: view matrices are affine).
void xform(const float m[16], const float p[3], float out[3]) {
	for (int r = 0; r < 3; ++r) out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

bool near3(const float a[3], const float b[3], float eps = 1e-4f) {
	for (int i = 0; i < 3; ++i)
		if (std::fabs(a[i] - b[i]) > eps) return false;
	return true;
}

// Copy of TurnVector in engine/hexen2/gl_rmain.c (forward and side perpendicular and normalized).
void turn_vector(float out[3], const float forward[3], const float side[3], float angle_deg) {
	const float sf = (float)std::cos(angle_deg * kPi / 180.0);
	const float ss = (float)std::sin(angle_deg * kPi / 180.0);
	for (int i = 0; i < 3; ++i) out[i] = sf * forward[i] + ss * side[i];
}

// Unit direction in Quake axes at horizontal angle h (to the right) and vertical angle v (up), degrees.
void edge_dir(const float fwd[3], const float right[3], const float up[3], float h_deg, float v_deg,
              float out[3]) {
	const double h = h_deg * kPi / 180.0, v = v_deg * kPi / 180.0;
	for (int i = 0; i < 3; ++i)
		out[i] = (float)(std::cos(v) * (std::cos(h) * fwd[i] + std::sin(h) * right[i]) + std::sin(v) * up[i]);
}

}  // namespace

TEST_CASE("symmetric fov matches engine GL_SetFrustum") {
	vr_fov_t f{-0.7854f, 0.7854f, 0.7854f, -0.7854f};
	float m[16];
	VR_ProjectionMatrix(&f, 4.f, 4096.f, m);
	CHECK(m[0] == Approx(1.0f));
	CHECK(m[5] == Approx(1.0f));
	CHECK(m[8] == Approx(0.0f));
	CHECK(m[9] == Approx(0.0f));
	CHECK(m[10] == Approx(-(4096.f + 4.f) / (4096.f - 4.f)));
	CHECK(m[11] == Approx(-1.f));
	CHECK(m[14] == Approx(-2.f * 4096.f * 4.f / (4096.f - 4.f)));
	CHECK(m[15] == Approx(0.0f));
	CHECK(m[1] == Approx(0.0f));
	CHECK(m[2] == Approx(0.0f));
	CHECK(m[3] == Approx(0.0f));
	CHECK(m[4] == Approx(0.0f));
	CHECK(m[6] == Approx(0.0f));
	CHECK(m[7] == Approx(0.0f));
	CHECK(m[12] == Approx(0.0f));
	CHECK(m[13] == Approx(0.0f));
}

TEST_CASE("asymmetric fov shifts center") {  // left -50, right 40, up 45, down -45 (degrees)
	const vr_fov_t f = make_fov(-50.f, 40.f, 45.f, -45.f);
	float m[16];
	VR_ProjectionMatrix(&f, 4.f, 4096.f, m);
	const float tl = std::tan(f.left), tr = std::tan(f.right), tu = std::tan(f.up), td = std::tan(f.down);
	CHECK(m[0] == Approx(2.f / (tr - tl)));
	CHECK(m[5] == Approx(2.f / (tu - td)));
	CHECK(m[8] == Approx((tr + tl) / (tr - tl)));
	CHECK(m[8] != Approx(0.0f));
	CHECK(m[9] == Approx(0.0f).epsilon(1e-5));
	CHECK(m[11] == Approx(-1.f));

	// vertical asymmetry: up 30, down -50
	const vr_fov_t g = make_fov(-45.f, 45.f, 30.f, -50.f);
	VR_ProjectionMatrix(&g, 4.f, 4096.f, m);
	const float gu = std::tan(g.up), gd = std::tan(g.down);
	CHECK(m[9] == Approx((gu + gd) / (gu - gd)));
	CHECK(m[8] == Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("projection: frustum edges map to NDC +-1") {
	const vr_fov_t f = make_fov(-50.f, 40.f, 35.f, -45.f);
	float m[16];
	VR_ProjectionMatrix(&f, 4.f, 4096.f, m);
	// clip = M * (x, y, z, 1); a view-space point on the right edge at depth z=-10 has x = 10*tan(right).
	auto ndc = [&](float x, float y, float z, float out[2]) {
		const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
		const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
		const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
		out[0] = cx / cw;
		out[1] = cy / cw;
	};
	float p[2];
	ndc(10.f * std::tan(f.right), 0.f, -10.f, p);
	CHECK(p[0] == Approx(1.0f));
	ndc(10.f * std::tan(f.left), 0.f, -10.f, p);
	CHECK(p[0] == Approx(-1.0f));
	ndc(0.f, 10.f * std::tan(f.up), -10.f, p);
	CHECK(p[1] == Approx(1.0f));
	ndc(0.f, 10.f * std::tan(f.down), -10.f, p);
	CHECK(p[1] == Approx(-1.0f));
}

TEST_CASE("view matrix: Quake forward maps to GL -Z") {
	const float org[3] = {10, 20, 30};
	const float zero[3] = {0, 0, 0};
	float m[16], e[3];
	VR_QuakeViewMatrix(org, zero, m);

	const float pf[3] = {11, 20, 30}, pl[3] = {10, 21, 30}, pu[3] = {10, 20, 31};
	const float ef[3] = {0, 0, -1}, el[3] = {-1, 0, 0}, eu[3] = {0, 1, 0};
	xform(m, pf, e);
	CHECK(near3(e, ef));
	xform(m, pl, e);
	CHECK(near3(e, el));
	xform(m, pu, e);
	CHECK(near3(e, eu));
	xform(m, org, e);  // the origin is the eye
	const float origin_e[3] = {0, 0, 0};
	CHECK(near3(e, origin_e));

	// yaw 90: facing +Y
	const float yaw90[3] = {0, 90, 0};
	VR_QuakeViewMatrix(org, yaw90, m);
	const float py[3] = {10, 21, 30};
	const float eexp[3] = {0, 0, -1};
	xform(m, py, e);
	CHECK(near3(e, eexp));
}

TEST_CASE("view matrix: eye-space coords equal AngleVectors projections (200 random poses)") {
	std::mt19937 rng(777);
	std::uniform_real_distribution<float> ang(-180.f, 180.f), pit(-89.f, 89.f), pos(-500.f, 500.f);
	for (int n = 0; n < 200; ++n) {
		const float org[3] = {pos(rng), pos(rng), pos(rng)};
		const float angles[3] = {pit(rng), ang(rng), ang(rng)};
		const float p[3] = {pos(rng), pos(rng), pos(rng)};
		float m[16], e[3], f[3], r[3], u[3];
		VR_QuakeViewMatrix(org, angles, m);
		xform(m, p, e);
		quake_test::angle_vectors(angles, f, r, u);
		const float d[3] = {p[0] - org[0], p[1] - org[1], p[2] - org[2]};
		const float expect[3] = {dot3(d, r), dot3(d, u), -dot3(d, f)};
		CHECK(near3(e, expect, 2e-3f));
	}
}

TEST_CASE("frustum normals: symmetric 90 equals engine formula") {
	// Engine (R_SetFrustum, fov_x == 90): vpn+vright, vpn-vright, vpn+vup, vpn-vup -- NOT normalized.
	// Compared as directions: normalize() the engine vectors, and ours are unit length.
	const float angle_sets[3][3] = {{0, 0, 0}, {15, 40, 0}, {-20, 200, 10}};
	for (const auto& angles : angle_sets) {
		float f[3], r[3], u[3];
		quake_test::angle_vectors(angles, f, r, u);
		float expect[4][3];
		for (int i = 0; i < 3; ++i) {
			expect[0][i] = f[i] + r[i];
			expect[1][i] = f[i] - r[i];
			expect[2][i] = f[i] + u[i];
			expect[3][i] = f[i] - u[i];
		}
		const vr_fov_t fov = make_fov(-45.f, 45.f, 45.f, -45.f);
		float n[4][3];
		VR_FrustumNormals(angles, &fov, n);
		for (int k = 0; k < 4; ++k) {
			normalize3(expect[k]);
			CHECK(near3(n[k], expect[k], 1e-4f));
			CHECK(std::sqrt(dot3(n[k], n[k])) == Approx(1.0f));
		}
	}
}

TEST_CASE("frustum normals: symmetric non-90 equals engine TurnVector formula (as a set)") {
	// Engine non-90 path: [0]=Turn(vpn,vright,fx/2-90) [1]=Turn(vpn,vright,90-fx/2)
	// [2]=Turn(vpn,vup,90-fy/2) [3]=Turn(vpn,vup,fy/2-90). Its horizontal pair is the mirror of the
	// fov==90 path (geometrically [0] is the right-hand plane); culling only needs the set, so we
	// compare ours (left,right) against the engine's ([1],[0]); vertical order is identical.
	const float angles[3] = {12, -70, 6};
	float f[3], r[3], u[3];
	quake_test::angle_vectors(angles, f, r, u);
	const float fx = 100.f, fy = 70.f;
	float e0[3], e1[3], e2[3], e3[3];
	turn_vector(e0, f, r, fx / 2 - 90);
	turn_vector(e1, f, r, 90 - fx / 2);
	turn_vector(e2, f, u, 90 - fy / 2);
	turn_vector(e3, f, u, fy / 2 - 90);
	const vr_fov_t fov = make_fov(-fx / 2, fx / 2, fy / 2, -fy / 2);
	float n[4][3];
	VR_FrustumNormals(angles, &fov, n);
	CHECK(near3(n[0], e1, 1e-4f));
	CHECK(near3(n[1], e0, 1e-4f));
	CHECK(near3(n[2], e2, 1e-4f));
	CHECK(near3(n[3], e3, 1e-4f));
}

TEST_CASE("frustum normals: asymmetric edges") {
	struct Case { vr_fov_t fov; float l, r, u, d; };
	const float L1 = -50.f, R1 = 40.f, U1 = 45.f, D1 = -35.f;
	const Case cases[] = {
		{make_fov(L1, R1, U1, D1), L1, R1, U1, D1},
		{make_fov(-30.f, 55.f, 20.f, -60.f), -30.f, 55.f, 20.f, -60.f},
	};
	const float angle_sets[3][3] = {{0, 0, 0}, {25, -130, 0}, {-30, 75, 20}};
	for (const Case& c : cases) {
		for (const auto& angles : angle_sets) {
			float f[3], r[3], u[3], n[4][3];
			quake_test::angle_vectors(angles, f, r, u);
			VR_FrustumNormals(angles, &c.fov, n);
			for (int k = 0; k < 4; ++k) CHECK(std::sqrt(dot3(n[k], n[k])) == Approx(1.0f));

			// Directions 0.5 degree inside every edge: positive against all four planes.
			const float hin_l = c.l + 0.5f, hin_r = c.r - 0.5f, vin_u = c.u - 0.5f, vin_d = c.d + 0.5f;
			const float inside[4][2] = {{hin_l, 0}, {hin_r, 0}, {0, vin_u}, {0, vin_d}};
			for (const auto& h : inside) {
				float d[3];
				edge_dir(f, r, u, h[0], h[1], d);
				for (int k = 0; k < 4; ++k) CHECK(dot3(d, n[k]) > 0.0f);
			}
			// 0.5 degree outside an edge: negative against that edge's plane.
			float d[3];
			edge_dir(f, r, u, c.l - 0.5f, 0, d);  // left
			CHECK(dot3(d, n[0]) < 0.0f);
			edge_dir(f, r, u, c.r + 0.5f, 0, d);  // right
			CHECK(dot3(d, n[1]) < 0.0f);
			edge_dir(f, r, u, 0, c.d - 0.5f, d);  // bottom
			CHECK(dot3(d, n[2]) < 0.0f);
			edge_dir(f, r, u, 0, c.u + 0.5f, d);  // top
			CHECK(dot3(d, n[3]) < 0.0f);
			// Exactly on the edge: ~0.
			edge_dir(f, r, u, c.l, 0, d);
			CHECK(dot3(d, n[0]) == Approx(0.0f).epsilon(1e-4).scale(1.0));
			edge_dir(f, r, u, c.r, 0, d);
			CHECK(dot3(d, n[1]) == Approx(0.0f).epsilon(1e-4).scale(1.0));
			edge_dir(f, r, u, 0, c.d, d);
			CHECK(dot3(d, n[2]) == Approx(0.0f).epsilon(1e-4).scale(1.0));
			edge_dir(f, r, u, 0, c.u, d);
			CHECK(dot3(d, n[3]) == Approx(0.0f).epsilon(1e-4).scale(1.0));
		}
	}
}
