#include "doctest.h"
#include "gl_ext.hpp"
#include "targets.hpp"
#include "vr_api.h"

#include <cstring>
#include <string>

namespace {

int g_dummy;
const char *g_missing = nullptr;   // name the fake loader does not know

void *fake_get_proc(const char *name) {
	if (g_missing && std::strcmp(name, g_missing) == 0) return nullptr;
	return &g_dummy;
}

}  // namespace

TEST_CASE("GlExt::load resolves every entry point or names the missing one") {
	hxvr::GlExt gl;
	std::string err;
	g_missing = nullptr;
	CHECK(gl.load(fake_get_proc, err));
	CHECK(gl.glBlitFramebuffer != nullptr);
	CHECK(gl.glTexImage2D != nullptr);

	hxvr::GlExt gl2;
	g_missing = "glBlitFramebuffer";
	CHECK_FALSE(gl2.load(fake_get_proc, err));
	CHECK(err.find("glBlitFramebuffer") != std::string::npos);
	g_missing = nullptr;

	hxvr::GlExt gl3;
	CHECK_FALSE(gl3.load(nullptr, err));
}

TEST_CASE("mirror layout: none, left eye full window, side by side") {
	hxvr::BlitRect r{};
	const glm::ivec2 eye(1000, 1000);
	CHECK_FALSE(hxvr::mirrorBlitRect(0, 0, eye, 1280, 720, r));
	CHECK_FALSE(hxvr::mirrorBlitRect(1, 1, eye, 1280, 720, r));
	CHECK_FALSE(hxvr::mirrorBlitRect(0, 3, eye, 1280, 720, r));
	CHECK_FALSE(hxvr::mirrorBlitRect(0, 2, glm::ivec2(0, 0), 1280, 720, r));

	// mode 1: eye 0 fills the window; square eye cropped to 16:9 (rows cropped, centred)
	REQUIRE(hxvr::mirrorBlitRect(0, 1, eye, 1280, 720, r));
	CHECK(r.dx0 == 0);
	CHECK(r.dx1 == 1280);
	CHECK(r.dy0 == 0);
	CHECK(r.dy1 == 720);
	CHECK(r.sx0 == 0);
	CHECK(r.sx1 == 1000);
	CHECK(r.sy1 - r.sy0 == 562);   // 1000 * 720 / 1280
	CHECK(r.sy0 == 219);

	// mode 2: halves; 640x720 destination is taller than the square eye -> columns cropped
	REQUIRE(hxvr::mirrorBlitRect(0, 2, eye, 1280, 720, r));
	CHECK(r.dx0 == 0);
	CHECK(r.dx1 == 640);
	CHECK(r.sy0 == 0);
	CHECK(r.sy1 == 1000);
	CHECK(r.sx1 - r.sx0 == 888);   // 640 * 1000 / 720
	CHECK(r.sx0 == 56);
	REQUIRE(hxvr::mirrorBlitRect(1, 2, eye, 1280, 720, r));
	CHECK(r.dx0 == 640);
	CHECK(r.dx1 == 1280);

	// same aspect: no crop
	REQUIRE(hxvr::mirrorBlitRect(0, 1, glm::ivec2(640, 360), 1280, 720, r));
	CHECK(r.sx0 == 0);
	CHECK(r.sy0 == 0);
	CHECK(r.sx1 == 640);
	CHECK(r.sy1 == 360);
}

TEST_CASE("eye view angles: head pitch/roll, body + head yaw wrapped") {
	const float head[3] = {-10.0f, 30.0f, 5.0f};
	float out[3];
	VR_ComposeViewAngles(170.0f, head, out);
	CHECK(out[0] == doctest::Approx(-10.0f));
	CHECK(out[1] == doctest::Approx(-160.0f));
	CHECK(out[2] == doctest::Approx(5.0f));
	VR_ComposeViewAngles(-200.0f, head, out);
	CHECK(out[1] == doctest::Approx(-170.0f));
	const float back[3] = {0.0f, 90.0f, 0.0f};
	VR_ComposeViewAngles(90.0f, back, out);
	CHECK(out[1] == doctest::Approx(180.0f));
}

TEST_CASE("eye API without a running backend is inert") {
	VR_Stop();
	CHECK(VR_BeginEye(0) == 0);
	VR_EndEye(0, 2, 1280, 720);
	VR_EndFrame();
}

TEST_CASE("mock without GL: eyes report no target, frames end cleanly") {
	vr_start_params_t p{};
	p.backend = VR_BACKEND_MOCK;
	p.render_scale = 1.0f;
	p.eye_height_m = 1.65f;
	p.mock_hz = 0.0f;
	char err[128];
	REQUIRE(VR_Start(nullptr, &p, err, (int)sizeof(err)) == 1);
	vr_frame_t f{};
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(f.render == 1);
	CHECK(VR_BeginEye(0) == 0);   // acquireEye returns 0 without GL
	VR_EndEye(0, 2, 1280, 720);
	CHECK(VR_BeginFrame(&f) == 1);   // previous frame left pending (eye 1 never begun): ended first
	VR_EndFrame();
	VR_EndFrame();   // no pending frame: no-op
	VR_Stop();
	CHECK(VR_IsRunning() == 0);
}
