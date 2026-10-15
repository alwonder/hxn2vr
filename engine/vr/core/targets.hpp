// targets.hpp -- per-eye render targets: one FBO per eye with the backend's colour texture
// attached each frame and a GL_DEPTH24_STENCIL8 renderbuffer; desktop mirror blits.
// All GL calls go through GlExt; a GL context must be current. Module-internal.
#pragma once

#include <glm/glm.hpp>

namespace hxvr {

struct GlExt;

// Source and destination rectangles (x0, y0, x1, y1; GL window coordinates, origin bottom-left)
// of one eye's mirror blit.
struct BlitRect {
	int sx0, sy0, sx1, sy1;
	int dx0, dy0, dx1, dy1;
};

// Mirror layout. mode 1: eye 0 fills the window; mode 2: eye 0 left half, eye 1 right half;
// any other mode (0 = none): nothing. The eye image is centre-cropped to the destination aspect
// (no stretching). Returns false when this eye is not mirrored in this mode, or sizes are empty.
bool mirrorBlitRect(int eye, int mirrorMode, glm::ivec2 eyeSize, int winW, int winH, BlitRect &out);

class EyeTargets {
public:
	explicit EyeTargets(const GlExt *gl) : gl_(gl) {}
	~EyeTargets() { destroy(); }
	EyeTargets(const EyeTargets &) = delete;
	EyeTargets &operator=(const EyeTargets &) = delete;

	// (Re)creates both eyes' FBOs and depth-stencil renderbuffers when the size changes.
	bool ensure(glm::ivec2 size);
	// Binds the eye's FBO (GL_FRAMEBUFFER) with colorTex as colour attachment 0.
	// Returns false (and leaves FBO 0 bound) if the framebuffer is incomplete.
	bool bind(unsigned colorTex, int eye);
	// Binds the window framebuffer (0) for read and draw.
	void unbind();
	// Blits the eye's colour into the window framebuffer per mirrorBlitRect.
	void blitToWindow(int eye, int mirrorMode, int winW, int winH);
	void destroy();

private:
	const GlExt *gl_;
	glm::ivec2 size_{0, 0};
	unsigned fbo_[2] = {0, 0};
	unsigned depth_[2] = {0, 0};
	unsigned attached_[2] = {0, 0};   // colour texture currently attached to each FBO
	bool complete_[2] = {false, false};
};

}  // namespace hxvr
