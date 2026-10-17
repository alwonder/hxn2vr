#include "doctest.h"
#include "vr_api.h"

#include <cstring>
#include <limits>

namespace {
const float mins[3] = {-4, -2, -10};
const float maxs[3] = {20, 6, 2};

void check_vec(const float actual[3], float x, float y, float z) {
	CHECK(actual[0] == doctest::Approx(x).epsilon(0.0001).scale(1));
	CHECK(actual[1] == doctest::Approx(y).epsilon(0.0001).scale(1));
	CHECK(actual[2] == doctest::Approx(z).epsilon(0.0001).scale(1));
}
}

TEST_CASE("weapon default entry centres the model in the hand") {
	vr_weapon_entry_t entry{};
	VR_WeaponGet("models/weapon_default_test.mdl", mins, maxs, &entry);
	check_vec(entry.offset, -8, -2, 4);
	check_vec(entry.angles, 0, 0, 0);
	CHECK(entry.scale == doctest::Approx(1));
}

TEST_CASE("weapon override wins and is listed") {
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
	vr_weapon_entry_t entry = {{1, 2, 3}, {4, 5, 6}, 1};
	const int count = VR_WeaponCount();
	CHECK(VR_WeaponSet("", &entry) == 0);
	entry.scale = 0;
	CHECK(VR_WeaponSet("models/invalid_weapon_test.mdl", &entry) == 0);
	entry.scale = 1;
	entry.offset[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(VR_WeaponSet("models/invalid_weapon_test.mdl", &entry) == 0);
	CHECK(VR_WeaponCount() == count);
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
