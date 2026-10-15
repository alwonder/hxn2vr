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
		// Swapchain images rotate: attach this frame's texture (cheap). The completeness check runs
		// once per image name; incomplete names are not remembered, so they are re-checked.
		gl_->glFramebufferTexture2D(gl::kFramebuffer, gl::kColorAttachment0, gl::kTexture2D, colorTex, 0);
		attached_[eye] = colorTex;
		bool known = false;
		for (unsigned k : known_[eye]) known = known || k == colorTex;
		complete_[eye] = known || gl_->glCheckFramebufferStatus(gl::kFramebuffer) == gl::kFramebufferComplete;
		if (complete_[eye] && !known) {
			known_[eye][knownNext_[eye]] = colorTex;
			knownNext_[eye] = (knownNext_[eye] + 1) % kKnownComplete;
		}
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
		for (unsigned &k : known_[i]) k = 0;
		knownNext_[i] = 0;
	}
	size_ = glm::ivec2(0, 0);
}


bool PanelTarget::ensure(glm::ivec2 size) {
	if (!gl_ || size.x <= 0 || size.y <= 0) return false;
	if (fbo_ && size == size_) return complete_;
	destroy();

	gl::GLint prevTex = 0, prevFbo = 0;
	gl_->glGetIntegerv(gl::kTextureBinding2D, &prevTex);   // the engine caches its bound texture
	gl_->glGetIntegerv(gl::kFramebufferBinding, &prevFbo);
	gl_->glGenTextures(1, &tex_);
	gl_->glBindTexture(gl::kTexture2D, tex_);
	gl_->glTexParameteri(gl::kTexture2D, gl::kTextureMinFilter, static_cast<gl::GLint>(gl::kLinear));
	gl_->glTexParameteri(gl::kTexture2D, gl::kTextureMagFilter, static_cast<gl::GLint>(gl::kLinear));
	gl_->glTexParameteri(gl::kTexture2D, gl::kTextureWrapS, static_cast<gl::GLint>(gl::kClampToEdge));
	gl_->glTexParameteri(gl::kTexture2D, gl::kTextureWrapT, static_cast<gl::GLint>(gl::kClampToEdge));
	gl_->glTexImage2D(gl::kTexture2D, 0, static_cast<gl::GLint>(gl::kRgba8), size.x, size.y, 0,
	                  gl::kRgba, gl::kUnsignedByte, nullptr);
	gl_->glBindTexture(gl::kTexture2D, static_cast<gl::GLuint>(prevTex));

	gl_->glGenFramebuffers(1, &fbo_);
	gl_->glBindFramebuffer(gl::kFramebuffer, fbo_);
	gl_->glFramebufferTexture2D(gl::kFramebuffer, gl::kColorAttachment0, gl::kTexture2D, tex_, 0);
	complete_ = gl_->glCheckFramebufferStatus(gl::kFramebuffer) == gl::kFramebufferComplete;
	gl_->glBindFramebuffer(gl::kFramebuffer, static_cast<gl::GLuint>(prevFbo));
	size_ = size;
	return complete_;
}

bool PanelTarget::begin(glm::ivec2 size) {
	if (!gl_ || active_ || !ensure(size)) return false;
	gl_->glGetIntegerv(gl::kFramebufferBinding, &prevFbo_);
	gl_->glGetIntegerv(gl::kViewport, prevViewport_);
	gl_->glBindFramebuffer(gl::kFramebuffer, fbo_);
	gl_->glViewport(0, 0, size_.x, size_.y);

	// Transparent black; the engine's clear colour and scissor state are left as they were.
	gl::GLfloat saved[4] = {0, 0, 0, 0};
	gl_->glGetFloatv(gl::kColorClearValue, saved);
	const bool scissor = gl_->glIsEnabled(gl::kScissorTest) != 0;
	if (scissor) gl_->glDisable(gl::kScissorTest);
	gl_->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	gl_->glClear(gl::kColorBufferBit);
	gl_->glClearColor(saved[0], saved[1], saved[2], saved[3]);
	if (scissor) gl_->glEnable(gl::kScissorTest);
	active_ = true;
	return true;
}

void PanelTarget::end() {
	if (!gl_ || !active_) return;
	active_ = false;
	gl_->glBindFramebuffer(gl::kFramebuffer, static_cast<gl::GLuint>(prevFbo_));
	gl_->glViewport(prevViewport_[0], prevViewport_[1], prevViewport_[2], prevViewport_[3]);
}

void PanelTarget::drawQuad(const float corners[4][3]) {
	if (!gl_ || active_ || !tex_ || !complete_) return;
	// Everything touched below sits in these attribute groups (enables, blend, depth mask,
	// texture binding and environment, current colour); the engine's state is restored by the pop.
	gl_->glPushAttrib(gl::kEnableBit | gl::kColorBufferBit | gl::kDepthBufferBit | gl::kTextureBit |
	                  gl::kCurrentBit | gl::kPolygonBit);
	gl_->glDisable(gl::kDepthTest);
	gl_->glDisable(gl::kCullFace);
	gl_->glDisable(gl::kAlphaTest);
	gl_->glDisable(gl::kLighting);
	gl_->glDisable(gl::kFog);
	gl_->glDisable(gl::kStencilTest);
	gl_->glDisable(gl::kScissorTest);
	gl_->glDepthMask(0);
	gl_->glEnable(gl::kBlend);
	gl_->glBlendFunc(gl::kSrcAlpha, gl::kOneMinusSrcAlpha);
	gl_->glEnable(gl::kTexture2D);
	gl_->glBindTexture(gl::kTexture2D, tex_);
	gl_->glTexEnvf(gl::kTextureEnv, gl::kTextureEnvMode, static_cast<gl::GLfloat>(gl::kModulate));
	gl_->glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	// Texture row 0 is the bottom of the image: the panel's top edge is t = 1.
	static const float uv[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
	gl_->glBegin(gl::kQuads);
	for (int i = 0; i < 4; ++i) {
		gl_->glTexCoord2f(uv[i][0], uv[i][1]);
		gl_->glVertex3f(corners[i][0], corners[i][1], corners[i][2]);
	}
	gl_->glEnd();
	gl_->glPopAttrib();
}

void PanelTarget::destroy() {
	if (gl_ && active_) end();
	if (gl_ && fbo_) gl_->glDeleteFramebuffers(1, &fbo_);
	if (gl_ && tex_) gl_->glDeleteTextures(1, &tex_);
	fbo_ = tex_ = 0;
	complete_ = false;
	size_ = glm::ivec2(0, 0);
}

}  // namespace hxvr
