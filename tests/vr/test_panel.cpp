#include "doctest.h"
#include "vr_api.h"

#include <cmath>

namespace {

void sub(const float a[3], const float b[3], float out[3]) {
	for (int i = 0; i < 3; ++i) out[i] = a[i] - b[i];
}
void cross(const float a[3], const float b[3], float out[3]) {
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}
float dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void check_corner(const float c[3], float x, float y, float z) {
	CHECK(c[0] == doctest::Approx(x).epsilon(1e-4));
	CHECK(c[1] == doctest::Approx(y).epsilon(1e-4));
	CHECK(c[2] == doctest::Approx(z).epsilon(1e-4));
}

}  // namespace

TEST_CASE("panel corners: yaw 0 pitch 0, 4:3, order TL TR BR BL") {
	const float center[3] = {100.0f, 0.0f, 50.0f};
	float c[4][3];
	VR_PanelCorners(center, 0.0f, 0.0f, 40.0f, 4.0f / 3.0f, c);
	check_corner(c[0], 100.0f, 20.0f, 65.0f);    // top-left (Quake +y is the viewer's left)
	check_corner(c[1], 100.0f, -20.0f, 65.0f);   // top-right
	check_corner(c[2], 100.0f, -20.0f, 35.0f);   // bottom-right
	check_corner(c[3], 100.0f, 20.0f, 35.0f);    // bottom-left
}

TEST_CASE("panel corners: yaw 90 lies on the plane y = const") {
	const float center[3] = {100.0f, 0.0f, 50.0f};
	float c[4][3];
	VR_PanelCorners(center, 90.0f, 0.0f, 40.0f, 4.0f / 3.0f, c);
	for (int i = 0; i < 4; ++i) CHECK(c[i][1] == doctest::Approx(0.0f).epsilon(1e-4));
	check_corner(c[0], 80.0f, 0.0f, 65.0f);
	check_corner(c[1], 120.0f, 0.0f, 65.0f);
	check_corner(c[2], 120.0f, 0.0f, 35.0f);
	check_corner(c[3], 80.0f, 0.0f, 35.0f);
}

TEST_CASE("panel corners: counter-clockwise seen by the viewer, normal toward -forward") {
	const float center[3] = {10.0f, -20.0f, 30.0f};
	const float yaws[] = {0.0f, 37.0f, 90.0f, -135.0f};
	const float pitches[] = {0.0f, 35.0f, -20.0f};
	for (float yaw : yaws)
		for (float pitch : pitches) {
			float c[4][3];
			VR_PanelCorners(center, yaw, pitch, 40.0f, 4.0f / 3.0f, c);

			const double k = 3.14159265358979323846 / 180.0;
			const float fwd[3] = {(float)(std::cos(pitch * k) * std::cos(yaw * k)),
			                      (float)(std::cos(pitch * k) * std::sin(yaw * k)), (float)-std::sin(pitch * k)};
			float e1[3], e2[3], n[3];
			sub(c[1], c[0], e1);   // top edge, left to right
			sub(c[3], c[0], e2);   // left edge, top to bottom
			cross(e2, e1, n);      // BL-TL x TR-TL: counter-clockwise from the viewer's side
			CHECK(dot(n, fwd) < 0.0f);
			const float len = std::sqrt(dot(n, n));
			CHECK(dot(n, fwd) / len == doctest::Approx(-1.0f).epsilon(1e-4));
			// size: 40 wide, 30 tall
			CHECK(std::sqrt(dot(e1, e1)) == doctest::Approx(40.0f).epsilon(1e-4));
			CHECK(std::sqrt(dot(e2, e2)) == doctest::Approx(30.0f).epsilon(1e-4));
			// centred on `center`
			float mid[3];
			for (int i = 0; i < 3; ++i) mid[i] = 0.25f * (c[0][i] + c[1][i] + c[2][i] + c[3][i]);
			check_corner(mid, center[0], center[1], center[2]);
		}
}
