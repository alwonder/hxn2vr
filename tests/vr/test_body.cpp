// Body model in metres and the calibration functions. None of these take a world scale: the body does
// not scale with the world.
#include "doctest.h"
#include "vr_api.h"

#include <cmath>
#include <limits>

namespace {

vr_body_t body_for(float eye) {
	vr_body_params_t p{};
	p.eye_height = eye;
	vr_body_t b{};
	REQUIRE(VR_BodyFromParams(&p, &b) == 1);
	return b;
}

vr_pose_t head_at(float x, float y, float z, vr_quat_t rot = {0.0f, 0.0f, 0.0f, 1.0f}) {
	vr_pose_t h{};
	h.pos = {x, y, z};
	h.rot = rot;
	return h;
}

}  // namespace

TEST_CASE("body proportions from eye height") {
	const vr_body_t b = body_for(1.65f);
	CHECK(b.eye_height == doctest::Approx(1.65f).epsilon(1e-4));
	CHECK(b.stature == doctest::Approx(1.762821f).epsilon(1e-4));
	CHECK(b.shoulder_height == doctest::Approx(1.441987f).epsilon(1e-4));
	CHECK(b.shoulder_width == doctest::Approx(0.456571f).epsilon(1e-4));
	CHECK(b.arm_length == doctest::Approx(0.705128f).epsilon(1e-4));
	CHECK(b.chest_height == doctest::Approx(1.269231f).epsilon(1e-4));
	CHECK(b.waist_height == doctest::Approx(1.057692f).epsilon(1e-4));
	CHECK(b.crouch_height == doctest::Approx(1.0725f).epsilon(1e-4));
}

TEST_CASE("body overrides") {
	vr_body_params_t p{};
	p.eye_height = 1.65f;
	p.arm_length = 0.7f;
	p.shoulder_width = 0.5f;
	p.crouch_fraction = 0.7f;
	vr_body_t b{};
	REQUIRE(VR_BodyFromParams(&p, &b) == 1);
	CHECK(b.arm_length == doctest::Approx(0.7f));
	CHECK(b.shoulder_width == doctest::Approx(0.5f));
	CHECK(b.crouch_height == doctest::Approx(1.155f).epsilon(1e-4));
	CHECK(b.shoulder_height == doctest::Approx(1.441987f).epsilon(1e-4));   // not overridable

	// negative / NaN overrides are derived
	p.arm_length = -1.0f;
	p.shoulder_width = std::numeric_limits<float>::quiet_NaN();
	p.crouch_fraction = 0.0f;
	REQUIRE(VR_BodyFromParams(&p, &b) == 1);
	CHECK(b.arm_length == doctest::Approx(0.705128f).epsilon(1e-4));
	CHECK(b.shoulder_width == doctest::Approx(0.456571f).epsilon(1e-4));
	CHECK(b.crouch_height == doctest::Approx(1.0725f).epsilon(1e-4));
}

TEST_CASE("body model ignores invalid eye height") {
	vr_body_t b{};
	b.eye_height = 42.0f;   // sentinel
	const float bad_eyes[] = {0.3f, 3.0f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
	                           std::numeric_limits<float>::infinity()};
	for (float eye : bad_eyes) {
		vr_body_params_t p{};
		p.eye_height = eye;
		CHECK(VR_BodyFromParams(&p, &b) == 0);
		CHECK(b.eye_height == 42.0f);
	}
	vr_body_params_t ok{};
	ok.eye_height = 1.65f;
	CHECK(VR_BodyFromParams(nullptr, &b) == 0);
	CHECK(VR_BodyFromParams(&ok, nullptr) == 0);
	ok.eye_height = 0.5f;
	CHECK(VR_BodyFromParams(&ok, &b) == 1);
	ok.eye_height = 2.5f;
	CHECK(VR_BodyFromParams(&ok, &b) == 1);
}

TEST_CASE("arm calibration") {
	const vr_body_t b = body_for(1.65f);
	float arm = 0.0f;

	// head at (0, 1.65, 0) looking down -z: the right shoulder is half a shoulder width to the right
	const vr_pose_t head = head_at(0.0f, 1.65f, 0.0f);
	vr_vec3_t right_hand{0.228286f, 1.441987f, -0.70f};
	REQUIRE(VR_CalibrateArm(&b, &head, &right_hand, VR_HAND_RIGHT, &arm) == 1);
	CHECK(arm == doctest::Approx(0.70f).epsilon(1e-3));

	vr_vec3_t left_hand{-0.228286f, 1.441987f, -0.70f};
	arm = 0.0f;
	REQUIRE(VR_CalibrateArm(&b, &head, &left_hand, VR_HAND_LEFT, &arm) == 1);
	CHECK(arm == doctest::Approx(0.70f).epsilon(1e-3));

	// a left hand measured as the right shoulder's arm is far off: 0.4567 m further
	arm = 0.0f;
	REQUIRE(VR_CalibrateArm(&b, &head, &left_hand, VR_HAND_RIGHT, &arm) == 1);
	CHECK(arm == doctest::Approx(std::sqrt(0.456571f * 0.456571f + 0.70f * 0.70f)).epsilon(1e-3));

	// head yawed 90 degrees to the left (angleAxis(90, +y)): forward is -x, right is -z
	const float s = std::sqrt(0.5f);
	const vr_pose_t yawed = head_at(0.0f, 1.65f, 0.0f, vr_quat_t{0.0f, s, 0.0f, s});
	vr_vec3_t yawed_hand{-0.70f, 1.441987f, -0.228286f};
	arm = 0.0f;
	REQUIRE(VR_CalibrateArm(&b, &yawed, &yawed_hand, VR_HAND_RIGHT, &arm) == 1);
	CHECK(arm == doctest::Approx(0.70f).epsilon(1e-3));

	// the shoulder follows the head position on the floor plane, not its height
	const vr_pose_t moved = head_at(1.0f, 1.20f, 2.0f);   // e.g. seated: head y does not matter
	vr_vec3_t moved_hand{1.228286f, 1.441987f, 1.30f};
	arm = 0.0f;
	REQUIRE(VR_CalibrateArm(&b, &moved, &moved_hand, VR_HAND_RIGHT, &arm) == 1);
	CHECK(arm == doctest::Approx(0.70f).epsilon(1e-3));
}

TEST_CASE("calibration rejects implausible values") {
	float out = 99.0f;
	CHECK(VR_CalibrateEyeHeight(1.70f, &out) == 1);
	CHECK(out == doctest::Approx(1.70f));
	out = 99.0f;
	CHECK(VR_CalibrateEyeHeight(0.3f, &out) == 0);
	CHECK(VR_CalibrateEyeHeight(3.0f, &out) == 0);
	CHECK(VR_CalibrateEyeHeight(std::numeric_limits<float>::quiet_NaN(), &out) == 0);
	CHECK(VR_CalibrateEyeHeight(std::numeric_limits<float>::infinity(), &out) == 0);
	CHECK(out == 99.0f);
	CHECK(VR_CalibrateEyeHeight(1.7f, nullptr) == 0);

	const vr_body_t b = body_for(1.65f);
	const vr_pose_t head = head_at(0.0f, 1.65f, 0.0f);
	vr_vec3_t low{0.23f, 1.44f, -0.2f};   // 0.2 m from the shoulder: arm hanging / not stretched
	CHECK(VR_CalibrateArm(&b, &head, &low, VR_HAND_RIGHT, &out) == 0);
	vr_vec3_t too_far{0.23f, 1.44f, -1.5f};   // 1.5 m
	CHECK(VR_CalibrateArm(&b, &head, &too_far, VR_HAND_RIGHT, &out) == 0);
	vr_vec3_t nan{std::numeric_limits<float>::quiet_NaN(), 1.44f, -0.7f};
	CHECK(VR_CalibrateArm(&b, &head, &nan, VR_HAND_RIGHT, &out) == 0);
	vr_pose_t nan_head = head;
	nan_head.pos.y = std::numeric_limits<float>::quiet_NaN();
	vr_vec3_t ok{0.2283f, 1.442f, -0.7f};
	CHECK(VR_CalibrateArm(&b, &nan_head, &ok, VR_HAND_RIGHT, &out) == 0);
	vr_pose_t zero_rot = head;
	zero_rot.rot = {0.0f, 0.0f, 0.0f, 0.0f};
	CHECK(VR_CalibrateArm(&b, &zero_rot, &ok, VR_HAND_RIGHT, &out) == 0);
	CHECK(VR_CalibrateArm(nullptr, &head, &ok, VR_HAND_RIGHT, &out) == 0);
	CHECK(VR_CalibrateArm(&b, &head, &ok, VR_HAND_RIGHT, nullptr) == 0);
	CHECK(out == 99.0f);
}

TEST_CASE("arm distance reports the measured length") {
	const vr_body_t b = body_for(1.65f);
	const vr_pose_t head = head_at(0.0f, 1.65f, 0.0f);
	vr_vec3_t low{0.228286f, 1.441987f, -0.2f};
	CHECK(VR_ArmDistance(&b, &head, &low, VR_HAND_RIGHT) == doctest::Approx(0.2f).epsilon(1e-3));
	CHECK(VR_ArmDistance(nullptr, &head, &low, VR_HAND_RIGHT) == -1.0f);
	vr_vec3_t nan{std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.0f};
	CHECK(VR_ArmDistance(&b, &head, &nan, VR_HAND_RIGHT) == -1.0f);
}
