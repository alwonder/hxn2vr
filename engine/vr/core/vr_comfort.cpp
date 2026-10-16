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

void setOverlayState(const GlExt &gl) {
	gl.glDisable(gl::kDepthTest);
	gl.glDisable(gl::kCullFace);
	gl.glDisable(gl::kAlphaTest);
	gl.glDisable(gl::kLighting);
	gl.glDisable(gl::kFog);
	gl.glDisable(gl::kStencilTest);
	gl.glDisable(gl::kScissorTest);
	gl.glDepthMask(0);
	gl.glEnable(gl::kBlend);
	gl.glBlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
}

namespace {

// Everything the fade and the vignette touch sits in these attribute groups (enables, blend, depth mask,
// current colour, matrix mode); the matrices themselves are pushed and popped. Both matrices are identity
// afterwards, so vertices are in clip space (NDC).
void beginClipSpace(const GlExt &gl) {
	gl.glPushAttrib(gl::kEnableBit | gl::kColorBufferBit | gl::kDepthBufferBit | gl::kCurrentBit |
	                gl::kPolygonBit | gl::kTransformBit);
	setOverlayState(gl);
	gl.glDisable(gl::kTexture2D);
	gl.glMatrixMode(gl::kProjection);
	gl.glPushMatrix();
	gl.glLoadIdentity();
	gl.glMatrixMode(gl::kModelview);
	gl.glPushMatrix();
	gl.glLoadIdentity();
}

// Popped in reverse: the matrices in their own modes first, then the attributes bring the engine's matrix
// mode back.
void endClipSpace(const GlExt &gl) {
	gl.glPopMatrix();   // modelview
	gl.glMatrixMode(gl::kProjection);
	gl.glPopMatrix();
	gl.glPopAttrib();   // restores the matrix mode too
}

constexpr int kVignetteSegments = 48;
constexpr float kVignetteFeather = 0.35f;   // NDC width of the 0 -> 1 alpha gradient
constexpr float kVignetteOuter = 3.0f;      // solid black out to this radius (past every corner)
constexpr float kVignetteMinInner = 1.45f;  // from here the clear area covers the whole view

}  // namespace

void drawFade(const GlExt &gl, float alpha) {
	if (!(alpha > 0.0f)) return;   // also NaN
	if (alpha > 1.0f) alpha = 1.0f;
	beginClipSpace(gl);
	gl.glColor4f(0.0f, 0.0f, 0.0f, alpha);
	gl.glBegin(gl::kQuads);   // the whole viewport in clip space
	gl.glVertex2f(-1.0f, -1.0f);
	gl.glVertex2f(1.0f, -1.0f);
	gl.glVertex2f(1.0f, 1.0f);
	gl.glVertex2f(-1.0f, 1.0f);
	gl.glEnd();
	endClipSpace(gl);
}

void drawVignette(const GlExt &gl, float cx, float cy, float inner) {
	if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(inner) || inner >= kVignetteMinInner) return;
	if (inner < 0.0f) inner = 0.0f;
	const float mid = inner + kVignetteFeather;
	beginClipSpace(gl);
	// Two rings per segment: the gradient (alpha 0 at inner, 1 at mid) and the solid one out to the outer
	// radius, drawn as quads around (cx, cy). Every vertex sets its own alpha.
	const float twoPi = 6.28318530718f;
	gl.glBegin(gl::kQuads);
	for (int i = 0; i < kVignetteSegments; ++i) {
		const float a0 = twoPi * static_cast<float>(i) / kVignetteSegments;
		const float a1 = twoPi * static_cast<float>(i + 1) / kVignetteSegments;
		const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
		const auto vtx = [&](float r, float c, float s, float alpha) {
			gl.glColor4f(0.0f, 0.0f, 0.0f, alpha);
			gl.glVertex2f(cx + r * c, cy + r * s);
		};
		vtx(inner, c0, s0, 0.0f);
		vtx(inner, c1, s1, 0.0f);
		vtx(mid, c1, s1, 1.0f);
		vtx(mid, c0, s0, 1.0f);
		vtx(mid, c0, s0, 1.0f);
		vtx(mid, c1, s1, 1.0f);
		vtx(kVignetteOuter, c1, s1, 1.0f);
		vtx(kVignetteOuter, c0, s0, 1.0f);
	}
	gl.glEnd();
	endClipSpace(gl);
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

extern "C" {

float VR_VignetteInner(float strength, float amount) {
	const auto clamp01 = [](float v) { return std::isfinite(v) ? (v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v)) : 0.0f; };
	return 1.5f - 0.9f * clamp01(strength) * clamp01(amount);
}

void VR_VignetteCenter(const vr_fov_t *fov, float out[2]) {
	if (!out) return;
	out[0] = out[1] = 0.0f;
	if (!fov) return;
	// NDC of the view direction: the image spans tan(left)..tan(right) horizontally and tan(down)..tan(up)
	// vertically; the direction (0, 0, -1) lands at -(tr + tl) / (tr - tl) (and the same for y).
	const float tl = std::tan(fov->left), tr = std::tan(fov->right);
	const float td = std::tan(fov->down), tu = std::tan(fov->up);
	const float w = tr - tl, h = tu - td;
	if (std::isfinite(w) && w > 0.0f) out[0] = -(tr + tl) / w;
	if (std::isfinite(h) && h > 0.0f) out[1] = -(tu + td) / h;
	if (!std::isfinite(out[0])) out[0] = 0.0f;
	if (!std::isfinite(out[1])) out[1] = 0.0f;
}

}  // extern "C"
