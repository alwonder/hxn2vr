#include "doctest.h"
#include "vr_api.h"

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
	VR_CompatClampMuzzle(start, muzzle, 0.5f, out);
	CHECK(out[0] == doctest::Approx(18));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	VR_CompatClampMuzzle(start, muzzle, 1.0f, out);
	CHECK(out[0] == doctest::Approx(40));
	CHECK(out[1] == doctest::Approx(0));
	CHECK(out[2] == doctest::Approx(50));

	VR_CompatClampMuzzle(start, muzzle, 0.01f, out);
	CHECK(out[0] == doctest::Approx(0));
	CHECK(out[1] == doctest::Approx(0));
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
