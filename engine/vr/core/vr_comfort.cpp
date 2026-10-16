// vr_comfort.cpp -- comfort helpers: the camera height scale from the game's view height, the fade
// approach and the fade quad (black over the eye while the head is inside a wall). No engine headers.
#include "vr_api.h"

#include "gl_ext.hpp"
#include "vr_comfort.hpp"

#include <cmath>

namespace {

constexpr float kStandViewHeight = 50.0f;   // Hexen II view_ofs z standing
constexpr float kMinHeightScale = 0.2f;

}  // namespace

namespace hxvr {

void drawFade(const GlExt &gl, float alpha) {
	if (!(alpha > 0.0f)) return;   // also NaN
	if (alpha > 1.0f) alpha = 1.0f;
	// Everything touched below sits in these attribute groups (enables, blend, depth mask, current colour,
	// matrix mode); the matrices themselves are pushed and popped. Popped in reverse: the matrices in their
	// own modes first, then the attributes bring the engine's matrix mode back.
	gl.glPushAttrib(gl::kEnableBit | gl::kColorBufferBit | gl::kDepthBufferBit | gl::kCurrentBit |
	                gl::kPolygonBit | gl::kTransformBit);
	gl.glDisable(gl::kDepthTest);
	gl.glDisable(gl::kCullFace);
	gl.glDisable(gl::kAlphaTest);
	gl.glDisable(gl::kLighting);
	gl.glDisable(gl::kFog);
	gl.glDisable(gl::kStencilTest);
	gl.glDisable(gl::kScissorTest);
	gl.glDisable(gl::kTexture2D);
	gl.glDepthMask(0);
	gl.glEnable(gl::kBlend);
	gl.glBlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
	gl.glMatrixMode(gl::kProjection);
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glMatrixMode(gl::kModelview);
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glColor4f(0.0f, 0.0f, 0.0f, alpha);
	gl.glBegin(gl::kQuads);   // the whole viewport in clip space
	gl.glVertex2f(-1.0f, -1.0f);
	gl.glVertex2f(1.0f, -1.0f);
	gl.glVertex2f(1.0f, 1.0f);
	gl.glVertex2f(-1.0f, 1.0f);
	gl.glEnd();
	gl.glPopMatrix();   // modelview
	gl.glMatrixMode(gl::kProjection);
	gl.glPopMatrix();
	gl.glPopAttrib();   // restores the matrix mode too
}

}  // namespace hxvr

extern "C" {

float VR_CameraHeightScale(float viewheight, int physical_crouch) {
	if (physical_crouch || !std::isfinite(viewheight)) return 1.0f;
	const float s = viewheight / kStandViewHeight;
	return s < kMinHeightScale ? kMinHeightScale : (s > 1.0f ? 1.0f : s);
}

float VR_Approach(float cur, float target, float rate_per_s, float dt) {
	if (!std::isfinite(target)) return cur;
	if (!std::isfinite(cur)) return target;
	const float step = rate_per_s * dt;
	if (!std::isfinite(step) || !(step > 0.0f)) return cur;
	const float d = target - cur;
	if (std::fabs(d) <= step) return target;
	return d > 0.0f ? cur + step : cur - step;
}

}  // extern "C"
