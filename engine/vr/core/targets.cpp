// targets.cpp -- per-eye FBOs and the desktop mirror blit.
#include "targets.hpp"

#include "gl_ext.hpp"

namespace hxvr {

bool mirrorBlitRect(int eye, int mirrorMode, glm::ivec2 eyeSize, int winW, int winH, BlitRect &out) {
	if (eyeSize.x <= 0 || eyeSize.y <= 0 || winW <= 0 || winH <= 0) return false;
	int dx0 = 0, dx1 = winW;
	if (mirrorMode == 1) {
		if (eye != 0) return false;
	} else if (mirrorMode == 2) {
		if (eye != 0 && eye != 1) return false;
		const int mid = winW / 2;
		dx0 = eye == 0 ? 0 : mid;
		dx1 = eye == 0 ? mid : winW;
	} else {
		return false;
	}
	const int dw = dx1 - dx0, dh = winH;
	if (dw <= 0) return false;

	// Centre crop of the eye image to the destination aspect.
	int sx0 = 0, sy0 = 0, sx1 = eyeSize.x, sy1 = eyeSize.y;
	const long long lhs = static_cast<long long>(eyeSize.x) * dh;   // src aspect vs dst aspect
	const long long rhs = static_cast<long long>(dw) * eyeSize.y;
	if (lhs > rhs) {   // source wider: crop columns
		const int w = static_cast<int>(rhs / dh);
		sx0 = (eyeSize.x - w) / 2;
		sx1 = sx0 + w;
	} else if (lhs < rhs) {   // source taller: crop rows
		const int h = static_cast<int>(lhs / dw);
		sy0 = (eyeSize.y - h) / 2;
		sy1 = sy0 + h;
	}
	out = BlitRect{sx0, sy0, sx1, sy1, dx0, 0, dx1, dh};
	return true;
}

bool EyeTargets::ensure(glm::ivec2 size) {
	if (!gl_ || size.x <= 0 || size.y <= 0) return false;
	if (fbo_[0] && size == size_) return true;
	destroy();

	gl::GLint prevRb = 0;
	gl_->glGetIntegerv(gl::kRenderbufferBinding, &prevRb);
	gl_->glGenFramebuffers(2, fbo_);
	gl_->glGenRenderbuffers(2, depth_);
	for (int i = 0; i < 2; ++i) {
		gl_->glBindRenderbuffer(gl::kRenderbuffer, depth_[i]);
		gl_->glRenderbufferStorage(gl::kRenderbuffer, gl::kDepth24Stencil8, size.x, size.y);
		gl_->glBindFramebuffer(gl::kFramebuffer, fbo_[i]);
		gl_->glFramebufferRenderbuffer(gl::kFramebuffer, gl::kDepthStencilAttachment, gl::kRenderbuffer, depth_[i]);
	}
	gl_->glBindFramebuffer(gl::kFramebuffer, 0);
	gl_->glBindRenderbuffer(gl::kRenderbuffer, static_cast<gl::GLuint>(prevRb));
	size_ = size;
	return true;
}

bool EyeTargets::bind(unsigned colorTex, int eye) {
	if (!gl_ || eye < 0 || eye > 1 || !fbo_[eye] || !colorTex) return false;
	gl_->glBindFramebuffer(gl::kFramebuffer, fbo_[eye]);
	if (attached_[eye] != colorTex) {
		// Swapchain images rotate: attach this frame's texture (cheap; status checked on change).
		gl_->glFramebufferTexture2D(gl::kFramebuffer, gl::kColorAttachment0, gl::kTexture2D, colorTex, 0);
		attached_[eye] = colorTex;
		complete_[eye] = gl_->glCheckFramebufferStatus(gl::kFramebuffer) == gl::kFramebufferComplete;
	}
	if (!complete_[eye]) {
		gl_->glBindFramebuffer(gl::kFramebuffer, 0);
		return false;
	}
	return true;
}

void EyeTargets::unbind() {
	if (gl_) gl_->glBindFramebuffer(gl::kFramebuffer, 0);
}

void EyeTargets::blitToWindow(int eye, int mirrorMode, int winW, int winH) {
	if (!gl_ || eye < 0 || eye > 1 || !fbo_[eye] || !complete_[eye]) return;
	BlitRect r;
	if (!mirrorBlitRect(eye, mirrorMode, size_, winW, winH, r)) return;
	const bool scissor = gl_->glIsEnabled(gl::kScissorTest) != 0;
	if (scissor) gl_->glDisable(gl::kScissorTest);
	gl_->glBindFramebuffer(gl::kReadFramebuffer, fbo_[eye]);
	gl_->glBindFramebuffer(gl::kDrawFramebuffer, 0);
	gl_->glBlitFramebuffer(r.sx0, r.sy0, r.sx1, r.sy1, r.dx0, r.dy0, r.dx1, r.dy1, gl::kColorBufferBit, gl::kLinear);
	gl_->glBindFramebuffer(gl::kFramebuffer, 0);
	if (scissor) gl_->glEnable(gl::kScissorTest);
}

void EyeTargets::destroy() {
	if (gl_ && fbo_[0]) {
		gl_->glDeleteFramebuffers(2, fbo_);
		gl_->glDeleteRenderbuffers(2, depth_);
	}
	for (int i = 0; i < 2; ++i) {
		fbo_[i] = depth_[i] = attached_[i] = 0;
		complete_[i] = false;
	}
	size_ = glm::ivec2(0, 0);
}

}  // namespace hxvr
