// gl_ext.hpp -- OpenGL entry points used by the module (FBOs, blits, eye textures).
// Loaded at VR start through the host's gl_get_proc (wglGetProcAddress with an opengl32.dll
// fallback for the GL 1.1 core functions). No system GL headers: the few types and enums the
// module needs are defined here. Module-internal; the engine never sees this header.
#pragma once

#include <string>

#if defined(_WIN32)
#define HXVR_GLAPI __stdcall
#else
#define HXVR_GLAPI
#endif

namespace hxvr {

namespace gl {
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLboolean = unsigned char;

constexpr GLenum kTexture2D = 0x0DE1;
constexpr GLenum kTextureBinding2D = 0x8069;
constexpr GLenum kTextureMinFilter = 0x2801;
constexpr GLenum kTextureMagFilter = 0x2800;
constexpr GLenum kTextureWrapS = 0x2802;
constexpr GLenum kTextureWrapT = 0x2803;
constexpr GLenum kLinear = 0x2601;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kRgba = 0x1908;
constexpr GLenum kRgba8 = 0x8058;
constexpr GLenum kUnsignedByte = 0x1401;
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kRenderbuffer = 0x8D41;
constexpr GLenum kRenderbufferBinding = 0x8CA7;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kDepthStencilAttachment = 0x821A;
constexpr GLenum kDepth24Stencil8 = 0x88F0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kScissorTest = 0x0C11;
constexpr GLbitfield kColorBufferBit = 0x00004000;
constexpr GLbitfield kDepthBufferBit = 0x00000100;
constexpr GLbitfield kStencilBufferBit = 0x00000400;
}  // namespace gl

struct GlExt {
	// GL 3.0 / ARB_framebuffer_object
	void (HXVR_GLAPI *glGenFramebuffers)(gl::GLsizei, gl::GLuint *) = nullptr;
	void (HXVR_GLAPI *glDeleteFramebuffers)(gl::GLsizei, const gl::GLuint *) = nullptr;
	void (HXVR_GLAPI *glBindFramebuffer)(gl::GLenum, gl::GLuint) = nullptr;
	void (HXVR_GLAPI *glFramebufferTexture2D)(gl::GLenum, gl::GLenum, gl::GLenum, gl::GLuint, gl::GLint) = nullptr;
	void (HXVR_GLAPI *glGenRenderbuffers)(gl::GLsizei, gl::GLuint *) = nullptr;
	void (HXVR_GLAPI *glDeleteRenderbuffers)(gl::GLsizei, const gl::GLuint *) = nullptr;
	void (HXVR_GLAPI *glBindRenderbuffer)(gl::GLenum, gl::GLuint) = nullptr;
	void (HXVR_GLAPI *glRenderbufferStorage)(gl::GLenum, gl::GLenum, gl::GLsizei, gl::GLsizei) = nullptr;
	void (HXVR_GLAPI *glFramebufferRenderbuffer)(gl::GLenum, gl::GLenum, gl::GLenum, gl::GLuint) = nullptr;
	gl::GLenum (HXVR_GLAPI *glCheckFramebufferStatus)(gl::GLenum) = nullptr;
	void (HXVR_GLAPI *glBlitFramebuffer)(gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint, gl::GLint,
	                                     gl::GLint, gl::GLint, gl::GLbitfield, gl::GLenum) = nullptr;
	// GL 1.1 core
	void (HXVR_GLAPI *glGenTextures)(gl::GLsizei, gl::GLuint *) = nullptr;
	void (HXVR_GLAPI *glDeleteTextures)(gl::GLsizei, const gl::GLuint *) = nullptr;
	void (HXVR_GLAPI *glBindTexture)(gl::GLenum, gl::GLuint) = nullptr;
	void (HXVR_GLAPI *glTexImage2D)(gl::GLenum, gl::GLint, gl::GLint, gl::GLsizei, gl::GLsizei, gl::GLint,
	                                gl::GLenum, gl::GLenum, const void *) = nullptr;
	void (HXVR_GLAPI *glTexParameteri)(gl::GLenum, gl::GLenum, gl::GLint) = nullptr;
	void (HXVR_GLAPI *glGetIntegerv)(gl::GLenum, gl::GLint *) = nullptr;
	gl::GLboolean (HXVR_GLAPI *glIsEnabled)(gl::GLenum) = nullptr;
	void (HXVR_GLAPI *glEnable)(gl::GLenum) = nullptr;
	void (HXVR_GLAPI *glDisable)(gl::GLenum) = nullptr;
	void (HXVR_GLAPI *glViewport)(gl::GLint, gl::GLint, gl::GLsizei, gl::GLsizei) = nullptr;
	gl::GLenum (HXVR_GLAPI *glGetError)() = nullptr;
	void (HXVR_GLAPI *glFlush)() = nullptr;

	// Resolves every entry point. On failure returns false, names the missing function in `err`
	// and leaves the struct in an unusable (partially loaded) state.
	bool load(void *(*getProc)(const char *), std::string &err);
};

}  // namespace hxvr
