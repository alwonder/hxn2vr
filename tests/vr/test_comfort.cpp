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
void HXVR_GLAPI f_begin(gl::GLenum) { g_fake->inBegin = true; g_fake->calls.push_back("begin"); }
void HXVR_GLAPI f_end() { g_fake->inBegin = false; g_fake->calls.push_back("end"); }
void HXVR_GLAPI f_vertex2f(float, float) { if (g_fake->inBegin) ++g_fake->vertices; }

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
