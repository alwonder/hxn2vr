#include "doctest.h"
#include "gl_ext.hpp"
#include "targets.hpp"
#include "vr_api.h"
#include "vr_module_testing.hpp"

#include <glm/gtc/quaternion.hpp>

#include <cstring>
#include <memory>
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

namespace {

// Records the Backend calls the module makes; render flag per frame is set by the test.
struct CallLog {
	int begins = 0, ends = 0, acquires = 0, releases = 0, stops = 0;
	bool render = true;
};

class FakeBackend final : public hxvr::Backend {
public:
	explicit FakeBackend(CallLog *log) : log_(log) {}
	bool start(const hxvr::StartParams &, std::string &) override { return true; }
	void stop() override { ++log_->stops; }
	hxvr::FrameInfo beginFrame(double) override {
		++log_->begins;
		return hxvr::FrameInfo{log_->render, true, false};
	}
	hxvr::Pose headPose() const override { return hxvr::Pose{glm::vec3(0.0f), glm::quat(1, 0, 0, 0)}; }
	hxvr::Pose eyePose(int) const override { return headPose(); }
	hxvr::Fov eyeFov(int) const override { return hxvr::Fov{-0.7f, 0.7f, 0.7f, -0.7f}; }
	glm::ivec2 eyeSize() const override { return glm::ivec2(64, 64); }
	unsigned acquireEye(int) override {
		++log_->acquires;
		return 0;
	}
	void releaseEye(int) override { ++log_->releases; }
	void endFrame() override { ++log_->ends; }

private:
	CallLog *log_;
};

}  // namespace

TEST_CASE("eye API refuses eyes in a non-rendering frame; endFrame follows every beginFrame") {
	CallLog log;
	VR_TestInjectBackend(std::make_unique<FakeBackend>(&log));
	vr_frame_t f{};

	log.render = false;
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(f.render == 0);
	CHECK(VR_BeginEye(0) == 0);
	CHECK(log.acquires == 0);   // contract: no acquireEye when not rendering
	VR_EndEye(0, 2, 640, 480);
	CHECK(log.releases == 0);
	VR_EndFrame();
	CHECK(log.ends == 1);

	log.render = true;
	REQUIRE(VR_BeginFrame(&f) == 1);
	VR_BeginEye(0);   // no GL: target not bound, but the eye image is acquired
	CHECK(log.acquires == 1);
	CHECK(VR_BeginEye(0) == 0);   // same eye twice: refused
	CHECK(log.acquires == 1);
	// eye 0 left acquired, frame left pending (longjmp): the next frame cleans up first
	REQUIRE(VR_BeginFrame(&f) == 1);
	CHECK(log.releases == 1);
	CHECK(log.ends == 2);
	CHECK(log.begins == 3);
	VR_Stop();   // pending frame ended before stop
	CHECK(log.ends == 3);
	CHECK(log.stops == 1);
	CHECK(VR_IsRunning() == 0);
}

namespace {

// Minimal fake GL for EyeTargets: counts attachments and completeness checks.
struct FakeFbo {
	int attaches = 0, statusChecks = 0;
	unsigned incomplete = 0;   // texture name the fake reports as incomplete
	unsigned attached = 0;
};
FakeFbo g_fbo;
unsigned g_nextName = 100;

void HXVR_GLAPI fakeGen(hxvr::gl::GLsizei n, hxvr::gl::GLuint *out) {
	for (int i = 0; i < n; ++i) out[i] = g_nextName++;
}
void HXVR_GLAPI fakeDel(hxvr::gl::GLsizei, const hxvr::gl::GLuint *) {}
void HXVR_GLAPI fakeBind(hxvr::gl::GLenum, hxvr::gl::GLuint) {}
void HXVR_GLAPI fakeRbStorage(hxvr::gl::GLenum, hxvr::gl::GLenum, hxvr::gl::GLsizei, hxvr::gl::GLsizei) {}
void HXVR_GLAPI fakeFbRb(hxvr::gl::GLenum, hxvr::gl::GLenum, hxvr::gl::GLenum, hxvr::gl::GLuint) {}
void HXVR_GLAPI fakeFbTex(hxvr::gl::GLenum, hxvr::gl::GLenum, hxvr::gl::GLenum, hxvr::gl::GLuint tex, hxvr::gl::GLint) {
	++g_fbo.attaches;
	g_fbo.attached = tex;
}
hxvr::gl::GLenum HXVR_GLAPI fakeStatus(hxvr::gl::GLenum) {
	++g_fbo.statusChecks;
	return g_fbo.attached == g_fbo.incomplete ? 0x8CD6u : hxvr::gl::kFramebufferComplete;
}
void HXVR_GLAPI fakeGetInt(hxvr::gl::GLenum, hxvr::gl::GLint *v) { *v = 0; }
bool g_srgb = false;   // GL_FRAMEBUFFER_SRGB state of the fake
int g_blits = 0;
hxvr::gl::GLboolean HXVR_GLAPI fakeIsEnabled(hxvr::gl::GLenum cap) { return cap == 0x8DB9 && g_srgb ? 1 : 0; }
void HXVR_GLAPI fakeDisable(hxvr::gl::GLenum cap) { if (cap == 0x8DB9) g_srgb = false; }
void HXVR_GLAPI fakeEnable(hxvr::gl::GLenum cap) { if (cap == 0x8DB9) g_srgb = true; }
void HXVR_GLAPI fakeBlit(hxvr::gl::GLint, hxvr::gl::GLint, hxvr::gl::GLint, hxvr::gl::GLint, hxvr::gl::GLint, hxvr::gl::GLint,
                         hxvr::gl::GLint, hxvr::gl::GLint, hxvr::gl::GLbitfield, hxvr::gl::GLenum) {
	CHECK_FALSE(g_srgb);   // raw (gamma-encoded) values copied: no sRGB decode/encode
	++g_blits;
}

hxvr::GlExt fakeFboGl() {
	hxvr::GlExt gl;
	gl.glGenFramebuffers = fakeGen;
	gl.glGenRenderbuffers = fakeGen;
	gl.glDeleteFramebuffers = fakeDel;
	gl.glDeleteRenderbuffers = fakeDel;
	gl.glBindFramebuffer = fakeBind;
	gl.glBindRenderbuffer = fakeBind;
	gl.glRenderbufferStorage = fakeRbStorage;
	gl.glFramebufferRenderbuffer = fakeFbRb;
	gl.glFramebufferTexture2D = fakeFbTex;
	gl.glCheckFramebufferStatus = fakeStatus;
	gl.glGetIntegerv = fakeGetInt;
	gl.glIsEnabled = fakeIsEnabled;
	gl.glDisable = fakeDisable;
	gl.glEnable = fakeEnable;
	gl.glBlitFramebuffer = fakeBlit;
	return gl;
}

}  // namespace

TEST_CASE("eye targets: rotating swapchain images are re-attached, completeness checked once per name") {
	g_fbo = FakeFbo{};
	const hxvr::GlExt gl = fakeFboGl();
	hxvr::EyeTargets t(&gl);
	REQUIRE(t.ensure(glm::ivec2(64, 64)));
	const unsigned names[3] = {7, 8, 9};   // an OpenXR swapchain with 3 images
	for (int frame = 0; frame < 9; ++frame) {
		CHECK(t.bind(names[frame % 3], 0));
		t.unbind();
	}
	CHECK(g_fbo.attaches == 9);        // a different image every frame: attached every frame
	CHECK(g_fbo.statusChecks == 3);    // but each name is checked only the first time
	CHECK(t.bind(9, 0));               // same name as attached: nothing to do
	CHECK(g_fbo.attaches == 9);

	// an incomplete image is refused every time it comes round, and re-checked (not cached)
	g_fbo.incomplete = 10;
	CHECK_FALSE(t.bind(10, 1));
	CHECK(t.bind(7, 1));
	CHECK_FALSE(t.bind(10, 1));
	CHECK(g_fbo.statusChecks == 3 + 3);

	// new FBOs (size change) forget what was checked
	REQUIRE(t.ensure(glm::ivec2(32, 32)));
	CHECK(t.bind(7, 0));
	CHECK(g_fbo.statusChecks == 7);
	t.destroy();
}

TEST_CASE("eye targets: GL_FRAMEBUFFER_SRGB is kept disabled for eye rendering and the mirror blit") {
	g_fbo = FakeFbo{};
	g_blits = 0;
	const hxvr::GlExt gl = fakeFboGl();
	hxvr::EyeTargets t(&gl);
	REQUIRE(t.ensure(glm::ivec2(64, 64)));
	g_srgb = true;
	REQUIRE(t.bind(7, 0));
	CHECK_FALSE(g_srgb);   // gamma-encoded output is written to the sRGB swapchain image as-is
	t.unbind();
	g_srgb = true;
	t.blitToWindow(0, 1, 1280, 720);
	CHECK(g_blits == 1);
	CHECK_FALSE(g_srgb);
	g_srgb = false;
	t.destroy();
}
