#include "doctest.h"
#include "backend.hpp"
#include "vr_api.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace {

// mockHz <= 0 disables frame pacing, so tests never sleep.
hxvr::StartParams params(float scale = 1.0f, bool motion = false) {
	return hxvr::StartParams{scale, 1.65f, motion, 0.0f};
}

float yaw_deg(const glm::quat& q) { return glm::degrees(2.0f * std::atan2(q.y, q.w)); }

vr_start_params_t c_params(vr_backend_kind_t kind) {
	vr_start_params_t p{};
	p.backend = kind;
	p.render_scale = 1.0f;
	p.eye_height_m = 1.65f;
	p.mock_motion = 0;
	p.mock_hz = 0.0f;   // no pacing
	return p;
}

}  // namespace

TEST_CASE("mock head pose is at eye height with identity rotation") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	REQUIRE(b->start(params(), err));
	b->beginFrame(0.0);
	const hxvr::Pose h = b->headPose();
	CHECK(h.pos.x == doctest::Approx(0.0f));
	CHECK(h.pos.y == doctest::Approx(1.65f));
	CHECK(h.pos.z == doctest::Approx(0.0f));
	CHECK(h.rot.w == doctest::Approx(1.0f));
	CHECK(h.rot.x == doctest::Approx(0.0f));
	CHECK(h.rot.y == doctest::Approx(0.0f));
	CHECK(h.rot.z == doctest::Approx(0.0f));
	b->stop();
}

TEST_CASE("mock eyes are offset by half the IPD on head-local X") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	REQUIRE(b->start(params(), err));
	b->beginFrame(0.0);
	CHECK(b->eyePose(0).pos.x == doctest::Approx(-0.032f));
	CHECK(b->eyePose(1).pos.x == doctest::Approx(0.032f));
	CHECK(b->eyePose(0).pos.y == doctest::Approx(1.65f));
	const hxvr::Fov f = b->eyeFov(1);
	CHECK(f.left == doctest::Approx(-0.785f));
	CHECK(f.right == doctest::Approx(0.785f));
	CHECK(f.up == doctest::Approx(0.785f));
	CHECK(f.down == doctest::Approx(-0.785f));
	b->stop();
}

TEST_CASE("mock eye size follows render scale") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	REQUIRE(b->start(params(1.0f), err));
	CHECK(b->eyeSize() == glm::ivec2(1440, 1584));
	b->stop();
	REQUIRE(b->start(params(0.5f), err));
	CHECK(b->eyeSize() == glm::ivec2(720, 792));
	b->stop();
}

TEST_CASE("mock rejects invalid render scale") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	CHECK_FALSE(b->start(params(0.0f), err));
	CHECK_FALSE(err.empty());
}

TEST_CASE("mock motion yaws 20 degrees at t=1s, nothing without mockMotion") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	REQUIRE(b->start(params(1.0f, true), err));
	b->beginFrame(1.0);
	CHECK(std::fabs(yaw_deg(b->headPose().rot) - (20.0f)) < 0.01f);
	// The eyes follow the head rotation: eye offset rotates with the head.
	const glm::vec3 d = b->eyePose(1).pos - b->headPose().pos;
	CHECK(glm::length(d) == doctest::Approx(0.032f));
	b->beginFrame(3.0);
	CHECK(std::fabs(yaw_deg(b->headPose().rot) - (-20.0f)) < 0.01f);
	b->stop();

	REQUIRE(b->start(params(1.0f, false), err));
	b->beginFrame(1.0);
	CHECK(std::fabs(yaw_deg(b->headPose().rot) - (0.0f)) < 0.01f);
	b->stop();
}

TEST_CASE("mock beginFrame reports a rendering, focused frame and acquireEye is 0 without GL") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	REQUIRE(b->start(params(), err));
	const hxvr::FrameInfo fi = b->beginFrame(0.0);
	CHECK(fi.render);
	CHECK(fi.focused);
	CHECK_FALSE(fi.lost);
	CHECK(b->acquireEye(0) == 0u);
	b->releaseEye(0);
	b->endFrame();
	b->stop();
}

TEST_CASE("mock beginFrame paces to mockHz") {
	auto b = hxvr::makeMockBackend(nullptr);
	std::string err;
	hxvr::StartParams p = params();
	p.mockHz = 100.0f;   // 10 ms per frame
	REQUIRE(b->start(p, err));
	const auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < 4; ++i) b->beginFrame(0.0);
	const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	CHECK(ms >= 25.0);   // first frame immediate, then 3 intervals of 10 ms (with slack)
	b->stop();
}

TEST_CASE("C API mock lifecycle") {
	char err[128] = "x";
	const vr_start_params_t p = c_params(VR_BACKEND_MOCK);
	CHECK(VR_IsRunning() == 0);
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	CHECK(VR_IsRunning() == 1);

	vr_frame_t fr{};
	CHECK(VR_BeginFrame(&fr) == 1);
	CHECK(fr.render == 1);
	CHECK(fr.focused == 1);
	CHECK(fr.lost == 0);

	vr_pose_t head{};
	VR_GetHeadPose(&head);
	CHECK(head.pos.y == doctest::Approx(1.65f));
	CHECK(head.rot.w == doctest::Approx(1.0f));

	vr_pose_t eye{};
	vr_fov_t fov{};
	VR_GetEye(0, &eye, &fov);
	CHECK(eye.pos.x == doctest::Approx(-0.032f));
	CHECK(fov.left == doctest::Approx(-0.785f));
	VR_GetEye(1, &eye, nullptr);
	CHECK(eye.pos.x == doctest::Approx(0.032f));

	int w = 0, h = 0;
	VR_GetEyeSize(&w, &h);
	CHECK(w == 1440);
	CHECK(h == 1584);

	VR_Stop();
	CHECK(VR_IsRunning() == 0);
	CHECK(VR_BeginFrame(&fr) == 0);
	VR_Stop();   // idempotent
	CHECK(VR_IsRunning() == 0);
}

TEST_CASE("C API restart after stop and double start") {
	char err[64];
	const vr_start_params_t p = c_params(VR_BACKEND_MOCK);
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);   // replaces the running backend
	CHECK(VR_IsRunning() == 1);
	VR_Stop();
	CHECK(VR_IsRunning() == 0);
}

TEST_CASE("C API OpenXR backend fails to start without GL entry points") {
	char err[128] = "";
	const vr_start_params_t p = c_params(VR_BACKEND_OPENXR);
	CHECK(VR_Start(nullptr, &p, err, sizeof err) == 0);
	CHECK(std::string(err).find("OpenXR") != std::string::npos);
	CHECK(VR_IsRunning() == 0);
}

TEST_CASE("C API error text is truncated and NUL-terminated") {
	char err[8];
	std::memset(err, 'Z', sizeof err);
	const vr_start_params_t p = c_params(VR_BACKEND_OPENXR);
	CHECK(VR_Start(nullptr, &p, err, sizeof err) == 0);
	CHECK(err[sizeof err - 1] == '\0');
	CHECK(std::strlen(err) == sizeof err - 1);

	// Degenerate buffers and a null param block must not crash.
	CHECK(VR_Start(nullptr, &p, nullptr, 0) == 0);
	char one[1] = {'Z'};
	CHECK(VR_Start(nullptr, &p, one, 1) == 0);
	CHECK(one[0] == '\0');
	CHECK(VR_Start(nullptr, nullptr, nullptr, 0) == 0);
}

namespace {
int g_prints = 0;
void count_print(const char*) { ++g_prints; }
}  // namespace

TEST_CASE("C API failure is reported through err only, not also through host print") {
	vr_host_t host{};
	host.print = count_print;
	g_prints = 0;
	char err[64];
	const vr_start_params_t p = c_params(VR_BACKEND_OPENXR);
	CHECK(VR_Start(&host, &p, err, sizeof err) == 0);
	CHECK(g_prints == 0);   // the caller prints err (once); the module must not duplicate it
	CHECK(std::strlen(err) > 0);
}
