// quake_test_math.hpp -- test-only copies of engine math conventions (no engine headers).
#ifndef HXVR_QUAKE_TEST_MATH_HPP
#define HXVR_QUAKE_TEST_MATH_HPP

#include <cmath>

namespace quake_test {

// Copy of the formula in engine/h2shared/mathlib.c AngleVectors (angles = [PITCH, YAW, ROLL], degrees).
inline void angle_vectors(const float angles[3], float forward[3], float right[3], float up[3]) {
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

}  // namespace quake_test

#endif
