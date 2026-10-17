#include "doctest.h"
#include "vr_api.h"

#include <cstring>
#include <cmath>
#include <limits>

namespace {
const float mins[3] = {-4, -2, -10};
const float maxs[3] = {20, 6, 2};

struct WeaponTableScope {
	WeaponTableScope() { VR_WeaponClear(); }
	~WeaponTableScope() { VR_WeaponClear(); }
};

void check_vec(const float actual[3], float x, float y, float z) {
	CHECK(actual[0] == doctest::Approx(x).epsilon(0.0001).scale(1));
	CHECK(actual[1] == doctest::Approx(y).epsilon(0.0001).scale(1));
	CHECK(actual[2] == doctest::Approx(z).epsilon(0.0001).scale(1));
}
}

TEST_CASE("weapon from pose inverts the transform") {
	// for several hand poses and entries: Transform(entry) -> model pose -> FromPose -> entry' == entry (eps 1e-3)
	struct HandPose { float org[3]; float ang[3]; };
	const HandPose hands[] = {
		{{0, 0, 0}, {0, 0, 0}},
		{{100, -25, 40}, {0, 90, 0}},
		{{-32, 16, 72}, {30, -45, 0}},
		{{100, 0, 40}, {0, 0, 90}},
		{{-50, 23, -8}, {-35, 170, -65}},
		{{12, -64, 96}, {70, -130, 125}}
	};
	const vr_weapon_entry_t entries[] = {
		{{-8, -2, 4}, {0, 0, 0}, 1},
		{{1, -3, 5}, {25, -40, 35}, 0.25f},
		{{-6, 2, -4}, {-50, 75, -80}, 1.5f},
		{{3, 7, -2}, {15, 350, 200}, 4}
	};
	for (const auto& hand : hands) {
		for (const auto& entry : entries) {
			CAPTURE(hand.ang[0]);
			CAPTURE(hand.ang[1]);
			CAPTURE(hand.ang[2]);
			CAPTURE(entry.scale);
			float model_org[3], model_ang[3], muzzle[3], aim_ang[3];
			VR_WeaponTransform(hand.org, hand.ang, &entry, mins, maxs, 1,
			                   model_org, model_ang, muzzle, aim_ang);
			vr_weapon_entry_t recovered{};
			// Pass ENTITY angles exactly as returned, without converting to view angles.
			VR_WeaponFromPose(hand.org, hand.ang, model_org, model_ang, entry.scale, &recovered);
			for (int i = 0; i < 3; ++i) {
				CAPTURE(i);
				CHECK(std::abs(recovered.offset[i] - entry.offset[i]) <= 1e-3f);
				// These entries have canonical pitch; normalize yaw/roll wrapping.
				CHECK(std::abs(std::remainder(recovered.angles[i] - entry.angles[i], 360.0f)) <= 1e-3f);
			}
			CHECK(std::abs(recovered.scale - entry.scale) <= 1e-3f);
		}
	}
}

TEST_CASE("weapon default entry centres the model in the hand") {
	WeaponTableScope table;
	vr_weapon_entry_t entry{};
	VR_WeaponGet("models/weapon_default_test.mdl", mins, maxs, &entry);
	check_vec(entry.offset, -8, -2, 4);
	check_vec(entry.angles, 0, 0, 0);
	CHECK(entry.scale == doctest::Approx(1));
}

TEST_CASE("weapon override wins and is listed") {
	WeaponTableScope table;
	const vr_weapon_entry_t entry = {{1, 2, 3}, {4, 5, 6}, 1.5f};
	REQUIRE(VR_WeaponSet("models/axe.mdl", &entry) == 1);
	vr_weapon_entry_t actual{};
	VR_WeaponGet("models/axe.mdl", mins, maxs, &actual);
	check_vec(actual.offset, 1, 2, 3);
	check_vec(actual.angles, 4, 5, 6);
	CHECK(actual.scale == doctest::Approx(1.5f));
	CHECK(VR_WeaponCount() == 1);
	const char *name = VR_WeaponName(0);
	REQUIRE(name != nullptr);
	CHECK(std::strcmp(name, "models/axe.mdl") == 0);
}

TEST_CASE("weapon set rejects bad entries") {
	WeaponTableScope table;
	vr_weapon_entry_t entry = {{1, 2, 3}, {4, 5, 6}, 1};
	const int count = VR_WeaponCount();
	CHECK(VR_WeaponSet("", &entry) == 0);
	CHECK(VR_WeaponSet("models/axe\".mdl", &entry) == 0);
	entry.scale = 0;
	CHECK(VR_WeaponSet("models/invalid_weapon_test.mdl", &entry) == 0);
	entry.scale = 1;
	entry.offset[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(VR_WeaponSet("models/invalid_weapon_test.mdl", &entry) == 0);
	CHECK(VR_WeaponCount() == count);
}

TEST_CASE("weapon clear removes overrides and restores defaults") {
	WeaponTableScope table;
	const vr_weapon_entry_t entry = {{1, 2, 3}, {4, 5, 6}, 1.5f};
	REQUIRE(VR_WeaponSet("models/axe.mdl", &entry) == 1);
	VR_WeaponClear();
	CHECK(VR_WeaponCount() == 0);
	CHECK(VR_WeaponName(0) == nullptr);
	vr_weapon_entry_t actual{};
	VR_WeaponGet("models/axe.mdl", mins, maxs, &actual);
	check_vec(actual.offset, -8, -2, 4);
	check_vec(actual.angles, 0, 0, 0);
	CHECK(actual.scale == doctest::Approx(1));
}

TEST_CASE("weapon cfg line") {
	const vr_weapon_entry_t entry = {{1, 2, 3}, {4, 5, 6}, 1.5f};
	const char expected[] = "vr_weapon_offset \"models/axe.mdl\" 1 2 3 4 5 6 1.5\n";
	char buf[128] = {};
	CHECK(VR_WeaponFormat("models/axe.mdl", &entry, buf, sizeof(buf))
	      == static_cast<int>(std::strlen(expected)));
	CHECK(std::strcmp(buf, expected) == 0);
	char small[8] = {};
	CHECK(VR_WeaponFormat("models/axe.mdl", &entry, small, sizeof(small)) == 0);
	CHECK(VR_WeaponFormat("models/axe\".mdl", &entry, buf, sizeof(buf)) == 0);
}

TEST_CASE("weapon hand roll uses view convention and centres the model") {
	const float hand_org[3] = {100, 0, 40};
	const float hand_ang[3] = {0, 0, 90};
	const vr_weapon_entry_t entry = {{-8, -2, 4}, {0, 0, 0}, 1};
	float model_org[3], model_ang[3], muzzle[3], aim_ang[3];
	VR_WeaponTransform(hand_org, hand_ang, &entry, mins, maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	// Rx(+90) maps (x,y,z) to (x,-z,y). Centre (8,2,-4)
	// becomes (8,4,2); adding it to model_org recovers the hand origin.
	check_vec(model_org, 92, -4, 38);
	const float centre_world[3] = {model_org[0]+8, model_org[1]+4, model_org[2]+2};
	check_vec(centre_world, 100, 0, 40);
	check_vec(muzzle, 112, 0, 40);
	check_vec(aim_ang, 0, 0, 90);
	check_vec(model_ang, 0, 0, -90);
}

TEST_CASE("weapon hand roll composes entry pitch in view convention") {
	const float hand_org[3] = {100, 0, 40};
	const float hand_ang[3] = {0, 0, 90};
	const float centred_mins[3] = {-12, -2, -4};
	const float centred_maxs[3] = {12, 2, 4};
	const vr_weapon_entry_t entry = {{0, 0, 0}, {30, 0, 0}, 1};
	float model_org[3], model_ang[3], muzzle[3], aim_ang[3];
	VR_WeaponTransform(hand_org, hand_ang, &entry, centred_mins, centred_maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	// Ry(30)*(12,0,0) = (6*sqrt(3),0,-6), then Rx(+90)
	// gives (6*sqrt(3),+6,0). Mirrored roll would incorrectly give y=-6.
	check_vec(model_org, 100, 0, 40);
	check_vec(muzzle, 100 + 6*std::sqrt(3.0f), 6, 40);
	check_vec(aim_ang, 0, 30, 90);
	check_vec(model_ang, 0, 30, -90);
}

TEST_CASE("weapon transform with a level hand") {
	const float hand_org[3] = {100, 0, 40};
	const float hand_ang[3] = {0, 0, 0};
	vr_weapon_entry_t entry = {{-8, -2, 4}, {0, 0, 0}, 1};
	float model_org[3], model_ang[3], muzzle[3], aim_ang[3];
	VR_WeaponTransform(hand_org, hand_ang, &entry, mins, maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	check_vec(model_org, 92, -2, 44);
	check_vec(model_ang, 0, 0, 0);
	check_vec(muzzle, 112, 0, 40);
	check_vec(aim_ang, 0, 0, 0);

	// P2: scale multiplies both the hand-frame offset and the model front point.
	entry.scale = 2;
	VR_WeaponTransform(hand_org, hand_ang, &entry, mins, maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	check_vec(model_org, 84, -4, 48);
	check_vec(muzzle, 124, 0, 40);
}

TEST_CASE("weapon transform follows hand yaw and pitch") {
	const float hand_org[3] = {100, 0, 40};
	float hand_ang[3] = {0, 90, 0};
	const vr_weapon_entry_t entry = {{-8, -2, 4}, {0, 0, 0}, 1};
	float model_org[3], model_ang[3], muzzle[3], aim_ang[3];
	VR_WeaponTransform(hand_org, hand_ang, &entry, mins, maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	check_vec(muzzle, 100, 12, 40);
	check_vec(model_ang, 0, 90, 0);
	check_vec(aim_ang, 0, 90, 0);

	hand_ang[0] = 30;
	hand_ang[1] = 0;
	VR_WeaponTransform(hand_org, hand_ang, &entry, mins, maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	// sin(30 degrees) = 0.5; positive view pitch looks down.
	CHECK(muzzle[2] == doctest::Approx(40 - 12 * 0.5f));
	check_vec(model_ang, -30, 0, 0);
	check_vec(aim_ang, 30, 0, 0);
}

TEST_CASE("weapon entry rotation turns the aim") {
	const float hand_org[3] = {100, 0, 40};
	const float hand_ang[3] = {0, 0, 0};
	const vr_weapon_entry_t entry = {{-8, -2, 4}, {0, 10, 0}, 1};
	float model_org[3], model_ang[3], muzzle[3], aim_ang[3];
	VR_WeaponTransform(hand_org, hand_ang, &entry, mins, maxs, 1,
	                   model_org, model_ang, muzzle, aim_ang);
	// Entry rotation does not rotate the offset out of the hand frame (P2).
	check_vec(model_org, 92, -2, 44);
	check_vec(model_ang, 0, 10, 0);
	check_vec(aim_ang, 0, 10, 0);
}
