#include "doctest.h"
#include "vr_api.h"
#include <initializer_list>

TEST_CASE("compat origin from the muzzle") {
	const float muzzle[3] = {100, 50, 80};
	const float proj_ofs[3] = {0, 0, 44};
	float out[3];
	VR_CompatOrigin(muzzle, proj_ofs, out);
	CHECK(out[0] == doctest::Approx(100));
	CHECK(out[1] == doctest::Approx(50));
	CHECK(out[2] == doctest::Approx(36));
}

TEST_CASE("compat muzzle is clamped by the body trace") {
	const float start[3] = {0, 0, 50};
	const float muzzle[3] = {40, 0, 50};
	float out[3];
	const float body_hit[3] = {20, 0, 50};
	VR_CompatClampMuzzle(start, muzzle, body_hit, 1, 16, out);
	CHECK(out[0] == doctest::Approx(4));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	// No wall on either probe, including the 16 units ahead along aim.
	VR_CompatClampMuzzle(start, muzzle, muzzle, 0, 16, out);
	CHECK(out[0] == doctest::Approx(40));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	// Wall at the muzzle: leave the full stock-progs spawn clearance.
	VR_CompatClampMuzzle(start, muzzle, muzzle, 1, 16, out);
	CHECK(out[0] == doctest::Approx(24));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	// Wall 8 units ahead of the muzzle: pull back another 8, giving 16 to the hit.
	const float ahead_hit[3] = {48, 0, 50};
	VR_CompatClampMuzzle(start, muzzle, ahead_hit, 1, 16, out);
	CHECK(out[0] == doctest::Approx(32));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	const float close_hit[3] = {0.4f, 0, 50};
	VR_CompatClampMuzzle(start, muzzle, close_hit, 1, 16, out);
	CHECK(out[0] == doctest::Approx(0));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	VR_CompatClampMuzzle(start, muzzle, start, 1, 16, out);
	CHECK(out[0] == doctest::Approx(0));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	// Eye-to-hand and aim need not line up. Clearance follows the validated eye->hit path.
	const float corner_muzzle[3] = {24, 24, 50};
	const float corner_hit[3] = {24, 32, 50};
	VR_CompatClampMuzzle(start, corner_muzzle, corner_hit, 1, 16, out);
	CHECK(out[0] == doctest::Approx(14.4f));
	CHECK(out[1] == doctest::Approx(19.2f));
	CHECK(out[2] == doctest::Approx(50));
}

TEST_CASE("compat restore keeps a progs move") {
	const float real[3] = {0, 0, 0};
	const float swapped[3] = {10, 0, -8};
	float out[3];
	VR_CompatRestore(real, swapped, swapped, out);
	CHECK(out[0] == doctest::Approx(0));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(0));

	const float after[3] = {10, 0, 92};
	VR_CompatRestore(real, swapped, after, out);
	CHECK(out[0] == doctest::Approx(0));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(100));
}

TEST_CASE("compat applies only to a live armed player") {
	// Numeric movetypes from hexen2/server.h, per controller ruling P1.
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 3, 0, 0) == 1); // WALK
	CHECK(VR_CompatApplies(0, 0.0, 100, 0, 3, 0, 0) == 0);
	CHECK(VR_CompatApplies(1, 0.2, 100, 0, 3, 0, 0) == 0);
	CHECK(VR_CompatApplies(1, 0.1, 100, 0, 3, 0, 0) == 1);
	CHECK(VR_CompatApplies(1, -0.01, 100, 0, 3, 0, 0) == 0);
	CHECK(VR_CompatApplies(1, 0.0, 0, 0, 3, 0, 0) == 0);
	CHECK(VR_CompatApplies(1, 0.0, 100, 1, 3, 0, 0) == 0);
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 0, 0, 0) == 0); // NONE
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 5, 0, 0) == 1); // FLY
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 8, 0, 0) == 1); // NOCLIP
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 14, 0, 0) == 1); // SWIM
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 9, 0, 0) == 0); // FLYMISSILE
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 3, 1, 0) == 0);
	CHECK(VR_CompatApplies(1, 0.0, 100, 0, 3, 0, 1) == 0);
}

TEST_CASE("compat split keeps low-hand origin above the floor and restores both deltas") {
	const float real[3] = {100, 200, 12};
	const float ofs[3] = {0, 0, 44};
	for (float height : {36.0f, 40.0f, 44.0f}) {
		const float muzzle[3] = {120, 190, real[2] + height};
		float origin[3], offset[3];
		VR_CompatSplit(real, muzzle, ofs, origin, offset);
		CHECK(origin[2] == real[2]);
		for (int i = 0; i < 3; ++i)
			CHECK(origin[i] + offset[i] == doctest::Approx(muzzle[i]));
		CHECK(offset[0] == 0);
		CHECK(offset[1] == 0);
		float restored[3];
		VR_CompatRestore(real, origin, origin, restored);
		for (int i = 0; i < 3; ++i) CHECK(restored[i] == real[i]);
		VR_CompatRestore(ofs, offset, offset, restored);
		for (int i = 0; i < 3; ++i) CHECK(restored[i] == ofs[i]);
		float after_origin[3] = {origin[0] + 3, origin[1] - 2, origin[2] + 100};
		VR_CompatRestore(real, origin, after_origin, after_origin);
		CHECK(after_origin[0] == real[0] + 3);
		CHECK(after_origin[1] == real[1] - 2);
		CHECK(after_origin[2] == real[2] + 100);
		// Preserve the progs delta, including a crouch assignment during the wrapper.
		float after_offset[3] = {offset[0] + 1, offset[1] - 2, 20};
		VR_CompatRestore(ofs, offset, after_offset, after_offset);
		CHECK(after_offset[0] == 1);
		CHECK(after_offset[1] == -2);
		CHECK(after_offset[2] == doctest::Approx(44 + 20 - height));
	}
	// Preserve nonzero horizontal offsets too (without relying on stock zero x/y).
	const float muzzle[3] = {120, 190, 50}, horizontal[3] = {2, -3, 44};
	float origin[3], offset[3];
	VR_CompatSplit(real, muzzle, horizontal, origin, offset);
	for (int i = 0; i < 3; ++i) CHECK(origin[i] + offset[i] == muzzle[i]);
	CHECK(offset[0] == 2);
	CHECK(offset[1] == -3);
}
