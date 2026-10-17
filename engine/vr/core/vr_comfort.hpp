// vr_comfort.hpp -- module-internal part of the comfort code (vr_comfort.cpp): the fade quad and the
// vignette ring on a given GlExt, so the tests can run them on a fake GL. The engine never sees this header
// (it calls VR_DrawFade / VR_DrawVignette).
#pragma once

namespace hxvr {

struct GlExt;

// Flat alpha-blended overlay state shared by the fade, the vignette and the panel quad: depth test,
// culling, alpha test, lighting, fog, stencil and scissor off, depth writes off, blend SRC_ALPHA /
// ONE_MINUS_SRC_ALPHA. The caller wraps it in glPushAttrib / glPopAttrib covering kEnableBit,
// kColorBufferBit and kDepthBufferBit (and sets texturing itself).
void setOverlayState(const GlExt &gl);

// Black over the whole viewport at alpha (clamped to 0..1; nothing at all at <= 0 or NaN), alpha-blended,
// depth off, both matrices identity; every GL state it touches is restored (glPushAttrib / glPopAttrib with
// the transform bit, so the matrix mode comes back too; the matrices are pushed and popped).
void drawFade(const GlExt &gl, float alpha);

// Black ring around (cx, cy) (NDC): alpha 0 at radius `inner`, 1 at inner + 0.35, solid out to radius 3;
// 48 segments, immediate mode, both matrices identity; the GL state is handled as in drawFade. Nothing is
// drawn for inner >= 1.45 (nothing to see) or a non-finite argument.
void drawVignette(const GlExt &gl, float cx, float cy, float inner);

// World primitives use the current eye matrices and restore their GL attributes.
void drawMarker(const GlExt &gl, const float org[3], float size, const float rgba[4]);
void drawAimDot(const GlExt &gl, const float org[3], const float right[3], const float up[3],
                float size, const float rgba[4]);

}  // namespace hxvr
