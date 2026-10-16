// Lifecycle: failed start, lost session, restart after stop, recenter requests, vr_status data.
#include "doctest.h"
#include "backend.hpp"
#include "vr_api.h"
#include "vr_module_testing.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <functional>
#include <memory>
#include <string>

namespace {

vr_start_params_t params(vr_backend_kind_t kind) {
	vr_start_params_t p{};
	p.backend = kind;
	p.render_scale = 1.0f;
	p.eye_height_m = 1.65f;
	p.mock_hz = 0.0f;
	return p;
}

struct Counters {
	int starts = 0, stops = 0, begins = 0, ends = 0, acquires = 0, releases = 0;
};

// Configurable fake: start result, lost from the Nth beginFrame on, a recenter request on the Mth
// beginFrame, canned info().
class ScriptedBackend final : public hxvr::Backend {
public:
	ScriptedBackend(Counters *c, bool startOk, int lostAtFrame = 0, int recenterAtFrame = 0)
	    : c_(c), startOk_(startOk), lostAt_(lostAtFrame), recenterAt_(recenterAtFrame) {}
	bool start(const hxvr::StartParams &, std::string &err) override {
		++c_->starts;
		if (!startOk_) err = "no runtime";
		return startOk_;
	}
	void stop() override { ++c_->stops; }
	hxvr::FrameInfo beginFrame(double) override {
		++c_->begins;
		const bool lost = lostAt_ > 0 && c_->begins >= lostAt_;
		return hxvr::FrameInfo{!lost, !lost, lost, c_->begins == recenterAt_};
	}
	hxvr::Pose headPose() const override { return hxvr::Pose{glm::vec3(0.0f), glm::quat(1, 0, 0, 0)}; }
	hxvr::Pose eyePose(int) const override { return headPose(); }
	hxvr::Fov eyeFov(int) const override { return hxvr::Fov{-0.7f, 0.7f, 0.7f, -0.7f}; }
	glm::ivec2 eyeSize() const override { return glm::ivec2(800, 900); }
	unsigned acquireEye(int) override {
		++c_->acquires;
		return 0;
	}
	void releaseEye(int) override { ++c_->releases; }
	void endFrame() override { ++c_->ends; }
	hxvr::BackendInfo info() const override {
		return hxvr::BackendInfo{"fake", "visible", "FakeRuntime 1.2.3", "0x8C43 (sRGB)"};
	}

private:
	Counters *c_;
	bool startOk_;
	int lostAt_;
	int recenterAt_;
};

int g_prints = 0;
void count_print(const char *) { ++g_prints; }

double g_now = 0.0;
double fake_time() { return g_now; }

struct FactoryGuard {
	~FactoryGuard() { VR_TestSetBackendFactory(nullptr); VR_Stop(); }
};

}  // namespace

TEST_CASE("failed start: error text kept, printed once by nobody, nothing running") {
	FactoryGuard guard;
	Counters c;
	VR_TestSetBackendFactory([&](vr_backend_kind_t) -> std::unique_ptr<hxvr::Backend> {
		return std::make_unique<ScriptedBackend>(&c, false);
	});
	vr_host_t host{};
	host.print = count_print;
	g_prints = 0;
	char err[128] = "";
	const vr_start_params_t p = params(VR_BACKEND_OPENXR);
	CHECK(VR_Start(&host, &p, err, sizeof err) == 0);
	CHECK(std::string(err).find("no runtime") != std::string::npos);
	CHECK(VR_IsRunning() == 0);
	CHECK(g_prints == 0);   // the caller (glue) prints err; the module must not print it a second time
	CHECK(c.starts == 1);
	CHECK(c.stops == 0);    // a failed start() is never followed by stop()
	vr_status_t st{};
	VR_GetStatus(&st);
	CHECK(st.running == 0);
	CHECK(std::string(st.state) == "stopped");
}

TEST_CASE("a start after a failed start works") {
	FactoryGuard guard;
	Counters bad, good;
	int n = 0;
	VR_TestSetBackendFactory([&](vr_backend_kind_t) -> std::unique_ptr<hxvr::Backend> {
		return n++ == 0 ? std::make_unique<ScriptedBackend>(&bad, false) : std::make_unique<ScriptedBackend>(&good, true);
	});
	char err[64];
	const vr_start_params_t p = params(VR_BACKEND_MOCK);
	CHECK(VR_Start(nullptr, &p, err, sizeof err) == 0);
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	CHECK(VR_IsRunning() == 1);
	VR_Stop();
	CHECK(VR_IsRunning() == 0);
	CHECK(good.stops == 1);
}

TEST_CASE("lost session: the second frame reports lost, stop and restart are clean") {
	FactoryGuard guard;
	Counters c;
	VR_TestSetBackendFactory([&](vr_backend_kind_t) -> std::unique_ptr<hxvr::Backend> {
		return std::make_unique<ScriptedBackend>(&c, true, 2);
	});
	char err[64];
	const vr_start_params_t p = params(VR_BACKEND_MOCK);
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	vr_frame_t f{};
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(f.lost == 0);
	CHECK(f.render == 1);
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(f.lost == 1);
	CHECK(f.render == 0);
	VR_Stop();   // the glue's reaction: stop; the pending frame is ended first
	CHECK(c.ends == c.begins);
	CHECK(c.stops == 1);
	CHECK(VR_IsRunning() == 0);
	CHECK(VR_BeginFrame(&f) == 0);   // inert after stop
	CHECK(f.lost == 0);

	Counters c2;
	VR_TestSetBackendFactory([&](vr_backend_kind_t) -> std::unique_ptr<hxvr::Backend> {
		return std::make_unique<ScriptedBackend>(&c2, true);
	});
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);   // the same module instance starts again
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(f.render == 1);
	VR_Stop();
}

TEST_CASE("TestInjectBackend after a stop leaves no stale per-session state") {
	Counters c1, c2;
	VR_TestInjectBackend(std::make_unique<ScriptedBackend>(&c1, true));
	vr_frame_t f{};
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(VR_BeginEye(0) == 0);   // no texture (fake): acquired but not bound
	CHECK(c1.acquires == 1);
	// acquired, never released (like a longjmp out of the frame): the next inject stops the first
	// backend, which releases the eye and ends the frame before stop()
	VR_TestInjectBackend(std::make_unique<ScriptedBackend>(&c2, true));
	CHECK(c1.releases == 1);
	CHECK(c1.ends == c1.begins);
	CHECK(c1.stops == 1);

	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(VR_BeginEye(0) == 0);
	CHECK(c2.acquires == 1);      // fresh session: the eye was not left "acquired", it is acquired again
	VR_EndEye(0, 0, 64, 64);
	CHECK(c2.releases == 1);      // released by VR_EndEye (a stale flag would make both calls no-ops)
	VR_Stop();
	CHECK(c2.releases == 1);      // nothing left to release at stop
	CHECK(c2.ends == c2.begins);
	CHECK(c2.stops == 1);
	CHECK(c1.acquires == 1);      // the first backend was not touched after its stop
	CHECK(c1.releases == 1);
}

TEST_CASE("recenter request: passed through VR_BeginFrame for exactly the frame that reports it") {
	FactoryGuard guard;
	Counters c;
	VR_TestSetBackendFactory([&](vr_backend_kind_t) -> std::unique_ptr<hxvr::Backend> {
		return std::make_unique<ScriptedBackend>(&c, true, 0, 3);   // recenter on the 3rd frame
	});
	char err[64];
	const vr_start_params_t p = params(VR_BACKEND_OPENXR);
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	vr_frame_t f;
	int recenters[5] = {};
	for (int i = 0; i < 5; ++i) {
		std::memset(&f, 0x7f, sizeof f);   // every field is written, also recenter = 0
		REQUIRE(VR_BeginFrame(&f) == 1);
		recenters[i] = f.recenter;
		CHECK(f.render == 1);
	}
	CHECK(recenters[0] == 0);
	CHECK(recenters[1] == 0);
	CHECK(recenters[2] == 1);
	CHECK(recenters[3] == 0);
	CHECK(recenters[4] == 0);
	VR_Stop();
	std::memset(&f, 0x7f, sizeof f);
	CHECK(VR_BeginFrame(&f) == 0);   // stopped: cleared, no request
	CHECK(f.recenter == 0);
}

TEST_CASE("the mock never requests a recenter") {
	const vr_start_params_t p = params(VR_BACKEND_MOCK);
	char err[64];
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	vr_frame_t f{};
	for (int i = 0; i < 10; ++i) {
		REQUIRE(VR_BeginFrame(&f) == 1);
		CHECK(f.recenter == 0);
	}
	VR_Stop();
}

TEST_CASE("status: stopped, then fake backend info, eye size and frame rates") {
	FactoryGuard guard;
	vr_status_t st;
	std::memset(&st, 0x7f, sizeof st);
	VR_GetStatus(&st);
	CHECK(st.running == 0);
	CHECK(std::string(st.state) == "stopped");
	CHECK(st.backend[0] == '\0');
	CHECK(st.eye_w == 0);
	CHECK(st.frames_per_s == doctest::Approx(0.0f));
	VR_GetStatus(nullptr);   // no crash

	Counters c;
	VR_TestSetBackendFactory([&](vr_backend_kind_t) -> std::unique_ptr<hxvr::Backend> {
		return std::make_unique<ScriptedBackend>(&c, true);
	});
	vr_host_t host{};
	host.time = fake_time;
	char err[64];
	const vr_start_params_t p = params(VR_BACKEND_MOCK);
	g_now = 100.0;
	REQUIRE(VR_Start(&host, &p, err, sizeof err) == 1);
	vr_frame_t f{};
	for (int i = 0; i < 10; ++i) {   // 10 frames in the first second
		g_now = 100.0 + 0.1 * i;
		VR_BeginFrame(&f);
	}
	g_now = 101.0;
	VR_BeginFrame(&f);   // closes the one-second window
	VR_GetStatus(&st);
	CHECK(st.running == 1);
	CHECK(std::string(st.backend) == "fake");
	CHECK(std::string(st.state) == "visible");
	CHECK(std::string(st.runtime) == "FakeRuntime 1.2.3");
	CHECK(std::string(st.format) == "0x8C43 (sRGB)");
	CHECK(st.eye_w == 800);
	CHECK(st.eye_h == 900);
	CHECK(st.frames_per_s == doctest::Approx(10.0f).epsilon(0.01));
	CHECK(st.rendered_per_s == doctest::Approx(10.0f).epsilon(0.01));

	g_now = 110.0;   // frames stopped coming: the stale rate reads 0
	VR_GetStatus(&st);
	CHECK(st.frames_per_s == doctest::Approx(0.0f));
	VR_Stop();
	VR_GetStatus(&st);
	CHECK(std::string(st.state) == "stopped");
}

TEST_CASE("status of the real mock backend") {
	const vr_start_params_t p = params(VR_BACKEND_MOCK);
	char err[64];
	REQUIRE(VR_Start(nullptr, &p, err, sizeof err) == 1);
	vr_status_t st{};
	VR_GetStatus(&st);
	CHECK(st.running == 1);
	CHECK(std::string(st.backend) == "mock");
	CHECK(std::string(st.state) == "focused");
	CHECK(st.eye_w == 1440);
	CHECK(st.eye_h == 1584);
	CHECK(std::string(st.format).empty() == false);
	VR_Stop();
}
