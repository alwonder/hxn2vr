// vr_comfort.hpp -- module-internal part of the comfort code (vr_comfort.cpp): the fade quad on a given
// GlExt, so the tests can run it on a fake GL. The engine never sees this header (it calls VR_DrawFade).
#pragma once

namespace hxvr {

struct GlExt;

// Black over the whole viewport at alpha (clamped to 0..1; nothing at all at <= 0 or NaN), alpha-blended,
// depth off, both matrices identity; every GL state it touches is restored (glPushAttrib / glPopAttrib with
// the transform bit, so the matrix mode comes back too; the matrices are pushed and popped).
void drawFade(const GlExt &gl, float alpha);

}  // namespace hxvr
