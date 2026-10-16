// Controller state: mock hands, trigger/grip hysteresis, inactive hands, mock head offset.
#include "doctest.h"
#include "backend.hpp"
#include "vr_api.h"
#include "vr_module_testing.hpp"

#include <cstring>
#include <memory>
#include <string>

namespace {

double g_now = 100.0;
double fake_time() { return g_now; }

struct Session {
	Session() {
		VR_TestSetBackendFactory([](vr_backend_kind_t) { return hxvr::makeMockBackend(nullptr); });
		vr_host_t host{};
		host.time = fake_time;
		vr_start_params_t p{};
		p.backend = VR_BACKEND_MOCK;
		p.render_scale = 1.0f;
		p.eye_height_m = 1.65f;
		p.mock_hz = 0.0f;   // no pacing
		char err[64] = "";
		g_now = 100.0;
		started = VR_Start(&host, &p, err, sizeof err) == 1;
	}
	~Session() {
		VR_Stop();
		VR_TestSetBackendFactory(nullptr);
	}
	void frame() {
		vr_frame_t f{};
		VR_BeginFrame(&f);
	}
	bool started = false;
};

vr_hand_t get(int hand) {
	vr_hand_t h{};
	VR_GetHand(hand, &h);
	return h;
}

// A default (active, pose valid, right-hand pose) hand with the given analog values.
vr_hand_t hand_with(float trigger, float squeeze, unsigned buttons = 0) {
	vr_hand_t h{};
	h.active = 1;
	h.pose_valid = 1;
	h.grip.pos = h.aim.pos = vr_vec3_t{0.2f, 1.0f, -0.3f};
	h.grip.rot = h.aim.rot = vr_quat_t{0.0f, 0.0f, 0.0f, 1.0f};
	h.trigger = trigger;
	h.squeeze = squeeze;
	h.buttons = buttons;
	return h;
}

}  // namespace

TEST_CASE("mock hands have default poses") {
	Session s;
	REQUIRE(s.started);
	s.frame();
	const vr_hand_t r = get(VR_HAND_RIGHT);
	CHECK(r.active == 1);
	CHECK(r.pose_valid == 1);
	CHECK(r.grip.pos.x == doctest::Approx(0.2f));
	CHECK(r.grip.pos.y == doctest::Approx(1.0f));
	CHECK(r.grip.pos.z == doctest::Approx(-0.3f));
	CHECK(r.aim.pos.x == doctest::Approx(0.2f));
	CHECK(r.grip.rot.w == doctest::Approx(1.0f));
	CHECK(r.buttons == 0u);
	const vr_hand_t l = get(VR_HAND_LEFT);
	CHECK(l.active == 1);
	CHECK(l.grip.pos.x == doctest::Approx(-0.2f));
	CHECK(l.aim.pos.z == doctest::Approx(-0.3f));
}

TEST_CASE("trigger and grip buttons use hysteresis") {
	Session s;
	REQUIRE(s.started);
	s.frame();
	auto trigger_at = [&](float v) {
		vr_hand_t h = hand_with(v, 0.0f);
		VR_MockSetHand(VR_HAND_RIGHT, &h);
		s.frame();
		return (get(VR_HAND_RIGHT).buttons & VR_BTN_TRIGGER) != 0u;
	};
	CHECK(trigger_at(0.5f) == false);   // below the press threshold
	CHECK(trigger_at(0.6f) == true);
	CHECK(trigger_at(0.45f) == true);   // between the thresholds: stays pressed
	CHECK(trigger_at(0.3f) == false);
	CHECK(trigger_at(0.5f) == false);   // not >= 0.55: stays released
	CHECK(get(VR_HAND_RIGHT).trigger == doctest::Approx(0.5f));

	auto grip_at = [&](float v) {
		vr_hand_t h = hand_with(0.0f, v);
		VR_MockSetHand(VR_HAND_RIGHT, &h);
		s.frame();
		return (get(VR_HAND_RIGHT).buttons & VR_BTN_GRIP) != 0u;
	};
	CHECK(grip_at(0.55f) == true);
	CHECK(grip_at(0.36f) == true);
	CHECK(grip_at(0.35f) == false);
}

TEST_CASE("hysteresis is tracked per hand") {
	Session s;
	REQUIRE(s.started);
	vr_hand_t pressed = hand_with(0.9f, 0.0f);
	VR_MockSetHand(VR_HAND_LEFT, &pressed);
	vr_hand_t mid = hand_with(0.45f, 0.0f);
	VR_MockSetHand(VR_HAND_RIGHT, &mid);
	s.frame();
	CHECK((get(VR_HAND_LEFT).buttons & VR_BTN_TRIGGER) != 0u);
	CHECK((get(VR_HAND_RIGHT).buttons & VR_BTN_TRIGGER) == 0u);
}

TEST_CASE("digital buttons pass through") {
	Session s;
	REQUIRE(s.started);
	vr_hand_t h = hand_with(0.0f, 0.0f, VR_BTN_LOWER | VR_BTN_MENU);
	VR_MockSetHand(VR_HAND_LEFT, &h);
	s.frame();
	CHECK(get(VR_HAND_LEFT).buttons == (VR_BTN_LOWER | VR_BTN_MENU));
	h.buttons = VR_BTN_UPPER | VR_BTN_STICK;
	VR_MockSetHand(VR_HAND_LEFT, &h);
	s.frame();
	CHECK(get(VR_HAND_LEFT).buttons == (VR_BTN_UPPER | VR_BTN_STICK));
	CHECK(get(VR_HAND_RIGHT).buttons == 0u);   // the other hand is untouched
}

TEST_CASE("TRIGGER and GRIP bits of the mock input are ignored (they come from the analog values)") {
	Session s;
	REQUIRE(s.started);
	vr_hand_t h = hand_with(0.0f, 0.0f, VR_BTN_TRIGGER | VR_BTN_GRIP);
	VR_MockSetHand(VR_HAND_RIGHT, &h);
	s.frame();
	CHECK(get(VR_HAND_RIGHT).buttons == 0u);
}

TEST_CASE("stick values pass through raw") {
	Session s;
	REQUIRE(s.started);
	vr_hand_t h = hand_with(0.0f, 0.0f);
	h.stick[0] = 0.25f;
	h.stick[1] = -1.0f;
	VR_MockSetHand(VR_HAND_RIGHT, &h);
	s.frame();
	const vr_hand_t r = get(VR_HAND_RIGHT);
	CHECK(r.stick[0] == doctest::Approx(0.25f));
	CHECK(r.stick[1] == doctest::Approx(-1.0f));
}

TEST_CASE("inactive hand reports nothing") {
	Session s;
	REQUIRE(s.started);
	vr_hand_t h = hand_with(1.0f, 1.0f, VR_BTN_LOWER);
	h.stick[0] = h.stick[1] = 1.0f;
	VR_MockSetHand(VR_HAND_RIGHT, &h);
	s.frame();
	CHECK((get(VR_HAND_RIGHT).buttons & VR_BTN_TRIGGER) != 0u);   // sanity: it was pressed
	h.active = 0;
	VR_MockSetHand(VR_HAND_RIGHT, &h);
	s.frame();
	const vr_hand_t r = get(VR_HAND_RIGHT);
	CHECK(r.active == 0);
	CHECK(r.pose_valid == 0);
	CHECK(r.trigger == 0.0f);
	CHECK(r.squeeze == 0.0f);
	CHECK(r.stick[0] == 0.0f);
	CHECK(r.stick[1] == 0.0f);
	CHECK(r.buttons == 0u);
	CHECK(r.grip.pos.y == 0.0f);

	// Reactivated with the trigger still at 0.45: the old pressed state must not survive the gap.
	h.active = 1;
	h.trigger = 0.45f;
	h.buttons = 0;
	VR_MockSetHand(VR_HAND_RIGHT, &h);
	s.frame();
	CHECK((get(VR_HAND_RIGHT).buttons & VR_BTN_TRIGGER) == 0u);
}

TEST_CASE("hands are zero when VR is not running") {
	{
		Session s;
		REQUIRE(s.started);
		vr_hand_t h = hand_with(1.0f, 1.0f, VR_BTN_MENU);
		VR_MockSetHand(VR_HAND_RIGHT, &h);
		s.frame();
		CHECK(get(VR_HAND_RIGHT).active == 1);
	}
	for (int i = 0; i < 2; ++i) {
		const vr_hand_t h = get(i);
		CHECK(h.active == 0);
		CHECK(h.buttons == 0u);
		CHECK(h.trigger == 0.0f);
	}
	// harmless without a backend
	vr_hand_t h{};
	VR_MockSetHand(VR_HAND_LEFT, &h);
	const vr_vec3_t off{0, 0, -1};
	VR_MockSetHeadOffset(&off, 1.0f);
	VR_GetHand(VR_HAND_LEFT, nullptr);
}

TEST_CASE("a restart starts with fresh hands") {
	{
		Session s;
		REQUIRE(s.started);
		vr_hand_t h = hand_with(1.0f, 0.0f, VR_BTN_LOWER);
		VR_MockSetHand(VR_HAND_RIGHT, &h);
		s.frame();
	}
	Session s2;
	REQUIRE(s2.started);
	s2.frame();
	CHECK(get(VR_HAND_RIGHT).buttons == 0u);
	CHECK(get(VR_HAND_RIGHT).trigger == 0.0f);
}

TEST_CASE("out-of-range hand index is treated as zero") {
	Session s;
	REQUIRE(s.started);
	s.frame();
	vr_hand_t h = get(5);
	CHECK(h.active == 0);
	CHECK(get(-1).active == 0);
}

TEST_CASE("mock head offset moves linearly over time") {
	Session s;
	REQUIRE(s.started);
	s.frame();   // host time t0
	const double t0 = g_now;
	const vr_vec3_t target{0.0f, 0.0f, -1.0f};
	VR_MockSetHeadOffset(&target, 2.0f);

	g_now = t0 + 1.0;
	s.frame();
	vr_pose_t head{};
	VR_GetHeadPose(&head);
	CHECK(head.pos.z == doctest::Approx(-0.5f));
	CHECK(head.pos.y == doctest::Approx(1.65f));
	vr_pose_t eye{};
	VR_GetEye(0, &eye, nullptr);
	CHECK(eye.pos.z == doctest::Approx(-0.5f));
	VR_GetEye(1, &eye, nullptr);
	CHECK(eye.pos.z == doctest::Approx(-0.5f));

	g_now = t0 + 3.0;
	s.frame();
	VR_GetHeadPose(&head);
	CHECK(head.pos.z == doctest::Approx(-1.0f));
	CHECK(head.pos.y == doctest::Approx(1.65f));
}

TEST_CASE("mock head offset: zero seconds is immediate, and a new move starts from the current offset") {
	Session s;
	REQUIRE(s.started);
	s.frame();
	vr_vec3_t target{0.5f, 0.1f, -1.0f};
	VR_MockSetHeadOffset(&target, 0.0f);
	s.frame();
	vr_pose_t head{};
	VR_GetHeadPose(&head);
	CHECK(head.pos.x == doctest::Approx(0.5f));
	CHECK(head.pos.y == doctest::Approx(1.75f));
	CHECK(head.pos.z == doctest::Approx(-1.0f));

	const double t0 = g_now;
	target = vr_vec3_t{0.5f, 0.1f, 0.0f};
	VR_MockSetHeadOffset(&target, 2.0f);   // back towards z = 0 from z = -1
	g_now = t0 + 1.0;
	s.frame();
	VR_GetHeadPose(&head);
	CHECK(head.pos.z == doctest::Approx(-0.5f));
	CHECK(head.pos.x == doctest::Approx(0.5f));
}

TEST_CASE("hands keep their poses when the head offset moves") {
	Session s;
	REQUIRE(s.started);
	s.frame();
	const vr_vec3_t target{0.0f, 0.0f, -1.0f};
	VR_MockSetHeadOffset(&target, 0.0f);
	s.frame();
	CHECK(get(VR_HAND_RIGHT).grip.pos.z == doctest::Approx(-0.3f));
}

TEST_CASE("vr_status reports the hand profiles and whether each hand is active") {
	{
		Session s;
		REQUIRE(s.started);
		s.frame();
		vr_status_t st{};
		VR_GetStatus(&st);
		CHECK(std::string(st.hand_profile[VR_HAND_LEFT]) == "mock");   // the mock's controllers are always "bound"
		CHECK(std::string(st.hand_profile[VR_HAND_RIGHT]) == "mock");
		CHECK(st.hand_active[VR_HAND_LEFT] == 1);
		CHECK(st.hand_active[VR_HAND_RIGHT] == 1);

		vr_hand_t off{};   // inactive
		VR_MockSetHand(VR_HAND_LEFT, &off);
		s.frame();
		VR_GetStatus(&st);
		CHECK(st.hand_active[VR_HAND_LEFT] == 0);
		CHECK(st.hand_active[VR_HAND_RIGHT] == 1);
		CHECK(std::string(st.hand_profile[VR_HAND_LEFT]) == "mock");
	}
	vr_status_t st;
	std::memset(&st, 0x7f, sizeof st);
	VR_GetStatus(&st);   // stopped: no profiles, no active hands
	CHECK(st.running == 0);
	CHECK(st.hand_profile[0][0] == '\0');
	CHECK(st.hand_profile[1][0] == '\0');
	CHECK(st.hand_active[0] == 0);
	CHECK(st.hand_active[1] == 0);
}
