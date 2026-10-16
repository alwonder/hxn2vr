// gl_ext.cpp -- resolves the module's GL entry points through the host's getProc.
#include "gl_ext.hpp"

namespace hxvr {
namespace {

template <class Fn>
bool resolve(void *(*getProc)(const char *), const char *name, Fn &out, std::string &err) {
	void *p = getProc(name);
	if (!p) {
		err = std::string("OpenGL entry point not found: ") + name;
		return false;
	}
	out = reinterpret_cast<Fn>(p);
	return true;
}

}  // namespace

bool GlExt::load(void *(*getProc)(const char *), std::string &err) {
	if (!getProc) {
		err = "no GL getProcAddress function";
		return false;
	}
#define HXVR_GL(fn) if (!resolve(getProc, #fn, fn, err)) return false;
	HXVR_GL(glGenFramebuffers)
	HXVR_GL(glDeleteFramebuffers)
	HXVR_GL(glBindFramebuffer)
	HXVR_GL(glFramebufferTexture2D)
	HXVR_GL(glGenRenderbuffers)
	HXVR_GL(glDeleteRenderbuffers)
	HXVR_GL(glBindRenderbuffer)
	HXVR_GL(glRenderbufferStorage)
	HXVR_GL(glFramebufferRenderbuffer)
	HXVR_GL(glCheckFramebufferStatus)
	HXVR_GL(glBlitFramebuffer)
	HXVR_GL(glGenTextures)
	HXVR_GL(glDeleteTextures)
	HXVR_GL(glBindTexture)
	HXVR_GL(glTexImage2D)
	HXVR_GL(glTexParameteri)
	HXVR_GL(glGetIntegerv)
	HXVR_GL(glIsEnabled)
	HXVR_GL(glEnable)
	HXVR_GL(glDisable)
	HXVR_GL(glViewport)
	HXVR_GL(glGetError)
	HXVR_GL(glFlush)
	HXVR_GL(glGetString)
	HXVR_GL(glGetFloatv)
	HXVR_GL(glClearColor)
	HXVR_GL(glClear)
	HXVR_GL(glBlendFunc)
	HXVR_GL(glDepthMask)
	HXVR_GL(glTexEnvf)
	HXVR_GL(glPushAttrib)
	HXVR_GL(glPopAttrib)
	HXVR_GL(glBegin)
	HXVR_GL(glEnd)
	HXVR_GL(glColor4f)
	HXVR_GL(glTexCoord2f)
	HXVR_GL(glVertex3f)
	HXVR_GL(glVertex2f)
	HXVR_GL(glMatrixMode)
	HXVR_GL(glPushMatrix)
	HXVR_GL(glPopMatrix)
	HXVR_GL(glLoadIdentity)
	HXVR_GL(glShadeModel)
#undef HXVR_GL
	return true;
}

}  // namespace hxvr
