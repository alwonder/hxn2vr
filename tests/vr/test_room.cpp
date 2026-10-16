// Room-scale on the client: the head's lean in world axes, tracking jumps, body jumps and the eye origin
// built from the feet, the lean and the eye's offset from the head.
#include "doctest.h"
#include "vr_api.h"

#include <cmath>
#include <limits>

namespace {

constexpr float kUpm = 36.3636f;      // 50 / 1.65 * 1.2
constexpr float kRadius = 9.0909f;    // 0.25 m * kUpm

void step(vr_room_t& r, float x, float y, float z, float yaw, float excess[2]) {
	const vr_vec3_t head{x, y, z};
	VR_RoomStep(&r, &head, yaw, kUpm, kRadius, excess);
}

}  // namespace

TEST_CASE("room lean follows the head") {
	vr_room_t r;
	VR_RoomReset(&r);
	float ex[2] = {99.0f, 99.0f};
	step(r, 0.0f, 1.6f, 0.0f, 0.0f, ex);
	CHECK(r.lean[0] == doctest::Approx(0.0f));
	CHECK(r.lean[1] == doctest::Approx(0.0f));
	CHECK(ex[0] == doctest::Approx(0.0f));
	CHECK(ex[1] == doctest::Approx(0.0f));

	step(r, 0.0f, 1.6f, -0.1f, 0.0f, ex);   // 0.1 m forward
	CHECK(r.lean[0] == doctest::Approx(3.636f).epsilon(1e-3));
	CHECK(r.lean[1] == doctest::Approx(0.0f));
	CHECK(ex[0] == doctest::Approx(0.0f));
	CHECK(ex[1] == doctest::Approx(0.0f));

	step(r, 0.0f, 1.6f, -0.4f, 0.0f, ex);   // 0.3 m more: past the radius
	CHECK(ex[0] == doctest::Approx(5.4545f).epsilon(1e-3));
	CHECK(ex[1] == doctest::Approx(0.0f));
	CHECK(r.lean[0] == doctest::Approx(9.0909f).epsilon(1e-3));
	CHECK(r.lean[1] == doctest::Approx(0.0f));

	// height changes alone do not lean
	step(r, 0.0f, 1.0f, -0.4f, 0.0f, ex);
	CHECK(r.lean[0] == doctest::Approx(9.0909f).epsilon(1e-3));
	CHECK(ex[0] == doctest::Approx(0.0f));
}

TEST_CASE("room lean is in world axes") {
	vr_room_t r;
	VR_RoomReset(&r);
	float ex[2];
	step(r, 0.0f, 1.6f, 0.0f, 90.0f, ex);
	step(r, 0.0f, 1.6f, -0.1f, 90.0f, ex);   // body faces +Y: forward is +Y in the world
	CHECK(r.lean[0] == doctest::Approx(0.0f).epsilon(1e-3));
	CHECK(r.lean[1] == doctest::Approx(3.636f).epsilon(1e-3));

	// the body turns (snap turn), the head does not move: the lean stays where it is in the world
	step(r, 0.0f, 1.6f, -0.1f, 0.0f, ex);
	CHECK(r.lean[0] == doctest::Approx(0.0f).epsilon(1e-3));
	CHECK(r.lean[1] == doctest::Approx(3.636f).epsilon(1e-3));
	step(r, 0.0f, 1.6f, -0.1f, -135.0f, ex);
	CHECK(r.lean[0] == doctest::Approx(0.0f).epsilon(1e-3));
	CHECK(r.lean[1] == doctest::Approx(3.636f).epsilon(1e-3));
	CHECK(ex[0] == doctest::Approx(0.0f));
	CHECK(ex[1] == doctest::Approx(0.0f));
}

TEST_CASE("room ignores tracking jumps") {
	vr_room_t r;
	VR_RoomReset(&r);
	float ex[2];
	step(r, 0.0f, 1.6f, 0.0f, 0.0f, ex);
	step(r, 0.0f, 1.6f, -0.6f, 0.0f, ex);   // 0.6 m in one frame: the runtime moved the origin
	CHECK(r.lean[0] == doctest::Approx(0.0f));
	CHECK(r.lean[1] == doctest::Approx(0.0f));
	CHECK(ex[0] == doctest::Approx(0.0f));
	CHECK(ex[1] == doctest::Approx(0.0f));
	step(r, 0.0f, 1.6f, -0.7f, 0.0f, ex);   // the next 0.1 m counts from the new head
	CHECK(r.lean[0] == doctest::Approx(3.636f).epsilon(1e-3));
	CHECK(r.lean[1] == doctest::Approx(0.0f));

	// a non-finite head is no step and does not become the last head
	const float nan = std::numeric_limits<float>::quiet_NaN();
	step(r, nan, 1.6f, -0.7f, 0.0f, ex);
	CHECK(r.lean[0] == doctest::Approx(3.636f).epsilon(1e-3));
	CHECK(ex[0] == doctest::Approx(0.0f));
	step(r, 0.0f, 1.6f, -0.8f, 0.0f, ex);
	CHECK(r.lean[0] == doctest::Approx(7.2727f).epsilon(1e-3));
}

TEST_CASE("room lean resets after a body jump") {
	const float a[3] = {0.0f, 0.0f, 0.0f};
	const float b[3] = {64.0f, 0.0f, 0.0f};
	const float c[3] = {46.0f, 46.0f, 0.0f};   // 65.05 units
	const float d[3] = {0.0f, 0.0f, 500.0f};   // vertical: falls, lifts
	CHECK(VR_BodyJumped(a, b) == 0);
	CHECK(VR_BodyJumped(a, c) == 1);
	CHECK(VR_BodyJumped(a, d) == 0);

	vr_room_t r;
	VR_RoomReset(&r);
	float ex[2];
	step(r, 0.0f, 1.6f, 0.0f, 0.0f, ex);
	step(r, 0.0f, 1.6f, -0.1f, 0.0f, ex);
	REQUIRE(r.has_last == 1);
	REQUIRE(r.lean[0] != 0.0f);
	VR_RoomReset(&r);
	CHECK(r.lean[0] == 0.0f);
	CHECK(r.lean[1] == 0.0f);
	CHECK(r.has_last == 0);
	// after a reset the first step only takes the head
	step(r, 0.0f, 1.6f, -0.3f, 0.0f, ex);
	CHECK(r.lean[0] == 0.0f);
	CHECK(r.has_last == 1);
}

TEST_CASE("eye origin with lean and height scale") {
	const float feet[3] = {100.0f, 200.0f, 24.0f};
	const float lean[2] = {5.0f, -3.0f};
	const vr_vec3_t head{0.1f, 1.65f, 0.2f};
	const vr_vec3_t eye{0.132f, 1.65f, 0.2f};   // head + (0.032, 0, 0): the right eye
	float o[3];
	VR_EyeOrigin(feet, 0.0f, lean, &head, &eye, kUpm, 1.0f, o);
	CHECK(o[0] == doctest::Approx(105.0f).epsilon(1e-4));
	CHECK(o[1] == doctest::Approx(195.836f).epsilon(1e-4));
	CHECK(o[2] == doctest::Approx(84.0f).epsilon(1e-4));
	VR_EyeOrigin(feet, 0.0f, lean, &head, &eye, kUpm, 0.48f, o);
	CHECK(o[2] == doctest::Approx(52.8f).epsilon(1e-4));
	VR_EyeOrigin(feet, 90.0f, lean, &head, &eye, kUpm, 1.0f, o);
	CHECK(o[0] == doctest::Approx(106.1636f).epsilon(1e-4));
	CHECK(o[1] == doctest::Approx(197.0f).epsilon(1e-4));
	CHECK(o[2] == doctest::Approx(84.0f).epsilon(1e-4));
}
