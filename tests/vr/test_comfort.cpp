// Comfort functions: the camera height scale from the game's view height, the fade approach, and the fade
// quad's GL state handling (with a recording fake GL).
#include "doctest.h"
#include "gl_ext.hpp"
#include "vr_api.h"
#include "vr_comfort.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

TEST_CASE("camera height scale") {
	CHECK(VR_CameraHeightScale(50.0f, 0) == doctest::Approx(1.0f));
	CHECK(VR_CameraHeightScale(24.0f, 0) == doctest::Approx(0.48f));
	CHECK(VR_CameraHeightScale(24.0f, 1) == doctest::Approx(1.0f));   // physical crouch: the head is low already
	CHECK(VR_CameraHeightScale(60.0f, 0) == doctest::Approx(1.0f));
	CHECK(VR_CameraHeightScale(0.0f, 0) == doctest::Approx(0.2f));
	CHECK(VR_CameraHeightScale(std::numeric_limits<float>::quiet_NaN(), 0) == doctest::Approx(1.0f));
}

TEST_CASE("approach") {
	const float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK(VR_Approach(0.0f, 1.0f, 5.0f, 0.1f) == doctest::Approx(0.5f));
	CHECK(VR_Approach(0.9f, 1.0f, 5.0f, 0.1f) == doctest::Approx(1.0f));   // no overshoot
	CHECK(VR_Approach(1.0f, 0.0f, 5.0f, 0.05f) == doctest::Approx(0.75f));
	CHECK(VR_Approach(0.3f, 1.0f, 5.0f, nan) == doctest::Approx(0.3f));    // dt NaN -> cur
	CHECK(VR_Approach(0.3f, 1.0f, 5.0f, -1.0f) == doctest::Approx(0.3f));
	CHECK(VR_Approach(0.3f, nan, 5.0f, 0.1f) == doctest::Approx(0.3f));
	CHECK(VR_Approach(nan, 1.0f, 5.0f, 0.1f) == doctest::Approx(1.0f));
}

// ---- fade quad with a recording fake GL ----

namespace {

using namespace hxvr;

struct FakeGl {
	std::vector<std::string> calls;
	int attribDepth = 0, maxAttribDepth = 0;
	int matrixDepth[2] = {0, 0};   // projection, modelview pushes outstanding
	gl::GLenum mode = 0x1700;      // GL_MODELVIEW
	gl::GLenum modeStack[4] = {};
	int modeTop = 0;
	bool inBegin = false;
	int vertices = 0;
	float color[4] = {1, 1, 1, 1};
	struct V { float x, y, a; };
	std::vector<V> verts;   // every glVertex2f inside glBegin with the alpha current at that moment
	int begins = 0;
};
FakeGl *g_fake = nullptr;

constexpr gl::GLenum kModelview = gl::kModelview, kProjection = gl::kProjection;
constexpr gl::GLbitfield kTransformBit = gl::kTransformBit;

int mode_index() { return g_fake->mode == kProjection ? 0 : 1; }

void HXVR_GLAPI f_push_attrib(gl::GLbitfield bits) {
	g_fake->calls.push_back("pushattrib");
	if (bits & kTransformBit) g_fake->modeStack[g_fake->modeTop++] = g_fake->mode;
	else g_fake->modeStack[g_fake->modeTop++] = 0;
	++g_fake->attribDepth;
	if (g_fake->attribDepth > g_fake->maxAttribDepth) g_fake->maxAttribDepth = g_fake->attribDepth;
}
void HXVR_GLAPI f_pop_attrib() {
	g_fake->calls.push_back("popattrib");
	const gl::GLenum m = g_fake->modeStack[--g_fake->modeTop];
	if (m) g_fake->mode = m;
	--g_fake->attribDepth;
}
void HXVR_GLAPI f_matrix_mode(gl::GLenum m) { g_fake->mode = m; }
void HXVR_GLAPI f_push_matrix() { ++g_fake->matrixDepth[mode_index()]; }
void HXVR_GLAPI f_pop_matrix() { --g_fake->matrixDepth[mode_index()]; }
void HXVR_GLAPI f_load_identity() { g_fake->calls.push_back(mode_index() == 0 ? "identity proj" : "identity mv"); }
void HXVR_GLAPI f_enable(gl::GLenum) {}
void HXVR_GLAPI f_disable(gl::GLenum) {}
void HXVR_GLAPI f_blend_func(gl::GLenum, gl::GLenum) {}
void HXVR_GLAPI f_depth_mask(gl::GLboolean) {}
void HXVR_GLAPI f_color4f(float r, float g, float b, float a) {
	g_fake->color[0] = r; g_fake->color[1] = g; g_fake->color[2] = b; g_fake->color[3] = a;
}
void HXVR_GLAPI f_begin(gl::GLenum) { g_fake->inBegin = true; ++g_fake->begins; g_fake->calls.push_back("begin"); }
void HXVR_GLAPI f_end() { g_fake->inBegin = false; g_fake->calls.push_back("end"); }
void HXVR_GLAPI f_vertex2f(float x, float y) {
	if (!g_fake->inBegin) return;
	++g_fake->vertices;
	g_fake->verts.push_back({x, y, g_fake->color[3]});
}

GlExt fake_ext() {
	GlExt e;
	e.glPushAttrib = f_push_attrib;
	e.glPopAttrib = f_pop_attrib;
	e.glMatrixMode = f_matrix_mode;
	e.glPushMatrix = f_push_matrix;
	e.glPopMatrix = f_pop_matrix;
	e.glLoadIdentity = f_load_identity;
	e.glEnable = f_enable;
	e.glDisable = f_disable;
	e.glBlendFunc = f_blend_func;
	e.glDepthMask = f_depth_mask;
	e.glColor4f = f_color4f;
	e.glBegin = f_begin;
	e.glEnd = f_end;
	e.glVertex2f = f_vertex2f;
	return e;
}

}  // namespace

TEST_CASE("fade quad restores the GL state") {
	FakeGl fake;
	g_fake = &fake;
	const GlExt e = fake_ext();

	drawFade(e, 0.6f);
	CHECK(fake.vertices == 4);
	CHECK(fake.color[3] == doctest::Approx(0.6f));
	CHECK(fake.color[0] == 0.0f);
	CHECK(fake.attribDepth == 0);
	CHECK(fake.maxAttribDepth == 1);
	CHECK(fake.matrixDepth[0] == 0);
	CHECK(fake.matrixDepth[1] == 0);
	CHECK(fake.mode == kModelview);   // matrix mode restored (by the attribute pop)
	// both matrices are identity while drawing
	bool proj = false, mv = false;
	for (const auto &c : fake.calls) {
		if (c == "identity proj") proj = true;
		if (c == "identity mv") mv = true;
	}
	CHECK(proj);
	CHECK(mv);

	// a projection mode left by the engine is restored too
	fake = FakeGl{};
	fake.mode = kProjection;
	drawFade(e, 2.0f);   // clamped
	CHECK(fake.color[3] == doctest::Approx(1.0f));
	CHECK(fake.mode == kProjection);
	CHECK(fake.matrixDepth[0] == 0);
	CHECK(fake.matrixDepth[1] == 0);

	// nothing at all for alpha <= 0 or NaN
	fake = FakeGl{};
	drawFade(e, 0.0f);
	drawFade(e, -1.0f);
	drawFade(e, std::numeric_limits<float>::quiet_NaN());
	CHECK(fake.calls.empty());
	CHECK(fake.vertices == 0);
	g_fake = nullptr;
}

// ---- vignette ----

TEST_CASE("vignette inner radius") {
	CHECK(VR_VignetteInner(0.6f, 1.0f) == doctest::Approx(0.96f));
	CHECK(VR_VignetteInner(1.0f, 1.0f) == doctest::Approx(0.6f));
	CHECK(VR_VignetteInner(0.6f, 0.0f) == doctest::Approx(1.5f));
	CHECK(VR_VignetteInner(2.0f, 1.0f) == doctest::Approx(0.6f));   // clamped
	CHECK(VR_VignetteInner(0.6f, 0.5f) == doctest::Approx(1.23f));
	CHECK(VR_VignetteInner(-1.0f, 1.0f) == doctest::Approx(1.5f));
	const float nan = std::numeric_limits<float>::quiet_NaN();
	CHECK(VR_VignetteInner(nan, 1.0f) == doctest::Approx(1.5f));    // nothing to draw
	CHECK(VR_VignetteInner(0.6f, nan) == doctest::Approx(1.5f));
}

TEST_CASE("vignette centre follows the asymmetric fov") {
	float c[2] = {9, 9};
	vr_fov_t sym{-0.785f, 0.785f, 0.785f, -0.785f};
	VR_VignetteCenter(&sym, c);
	CHECK(c[0] == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(c[1] == doctest::Approx(0.0f).epsilon(1e-4));
	// more FOV to the left: the view centre is right of the image centre
	vr_fov_t asym{-0.9f, 0.7f, 0.8f, -0.8f};
	VR_VignetteCenter(&asym, c);
	CHECK(c[0] == doctest::Approx(0.19876f).epsilon(1e-4));
	CHECK(c[1] == doctest::Approx(0.0f).epsilon(1e-4));
	// more FOV up: the centre is below the image centre
	vr_fov_t up{-0.8f, 0.8f, 0.9f, -0.7f};
	VR_VignetteCenter(&up, c);
	CHECK(c[0] == doctest::Approx(0.0f).epsilon(1e-4));
	CHECK(c[1] == doctest::Approx(-0.19876f).epsilon(1e-4));
	// bad input: the image centre
	vr_fov_t zero{0, 0, 0, 0};
	VR_VignetteCenter(&zero, c);
	CHECK(c[0] == 0.0f);
	CHECK(c[1] == 0.0f);
	c[0] = c[1] = 5;
	VR_VignetteCenter(nullptr, c);
	CHECK(c[0] == 0.0f);
	CHECK(c[1] == 0.0f);
}

TEST_CASE("vignette ring geometry and GL state") {
	FakeGl fake;
	g_fake = &fake;
	const GlExt e = fake_ext();

	drawVignette(e, 0.1f, -0.2f, 0.96f);
	// 48 segments, an inner gradient quad and an outer solid quad each
	CHECK(fake.verts.size() == 48u * 2u * 4u);
	float minR = 1e9f, maxR = 0.0f, aAtInner = -1, aAtSolid = -1;
	for (const auto &v : fake.verts) {
		const float r = std::hypot(v.x - 0.1f, v.y + 0.2f);
		minR = std::fmin(minR, r);
		maxR = std::fmax(maxR, r);
		CHECK(v.a >= 0.0f);
		CHECK(v.a <= 1.0f);
		if (std::fabs(r - 0.96f) < 1e-4f) aAtInner = v.a;
		if (std::fabs(r - 1.31f) < 1e-4f) aAtSolid = v.a;
	}
	CHECK(minR == doctest::Approx(0.96f).epsilon(1e-4));
	CHECK(maxR == doctest::Approx(3.0f).epsilon(1e-4));
	CHECK(aAtInner == doctest::Approx(0.0f));   // clear at `inner`
	CHECK(aAtSolid == doctest::Approx(1.0f));   // solid from inner + 0.35
	CHECK(fake.color[0] == 0.0f);
	CHECK(fake.color[1] == 0.0f);
	CHECK(fake.color[2] == 0.0f);
	CHECK(fake.attribDepth == 0);
	CHECK(fake.maxAttribDepth == 1);
	CHECK(fake.matrixDepth[0] == 0);
	CHECK(fake.matrixDepth[1] == 0);
	CHECK(fake.mode == kModelview);
	CHECK(fake.begins >= 1);
	CHECK_FALSE(fake.inBegin);

	// the engine's projection matrix mode comes back
	fake = FakeGl{};
	fake.mode = kProjection;
	drawVignette(e, 0.0f, 0.0f, 0.6f);
	CHECK(fake.mode == kProjection);
	CHECK(fake.matrixDepth[0] == 0);
	CHECK(fake.matrixDepth[1] == 0);

	// nothing to draw at inner >= 1.45, NaN, or a non-finite centre
	fake = FakeGl{};
	drawVignette(e, 0.0f, 0.0f, 1.45f);
	drawVignette(e, 0.0f, 0.0f, 1.5f);
	drawVignette(e, 0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN());
	drawVignette(e, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.96f);
	CHECK(fake.calls.empty());
	CHECK(fake.vertices == 0);
	g_fake = nullptr;
}
