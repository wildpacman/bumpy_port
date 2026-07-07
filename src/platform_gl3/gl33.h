#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

// Every post-GL-1.1 function the port uses, as (typedef, Name) pairs. GL 1.1 is
// exported by opengl32.dll and called directly; these are loaded per-context via
// SDL_GL_GetProcAddress. Supersedes the spec's "vendor glad": SDL already ships
// the headers, so no third-party loader is needed.
#define BUMPY_GL33_FUNCS(X) \
    X(PFNGLCREATESHADERPROC, CreateShader) \
    X(PFNGLSHADERSOURCEPROC, ShaderSource) \
    X(PFNGLCOMPILESHADERPROC, CompileShader) \
    X(PFNGLGETSHADERIVPROC, GetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, DeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram) \
    X(PFNGLATTACHSHADERPROC, AttachShader) \
    X(PFNGLLINKPROGRAMPROC, LinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, UseProgram) \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, Uniform1i) \
    X(PFNGLUNIFORM1FPROC, Uniform1f) \
    X(PFNGLUNIFORM2FPROC, Uniform2f) \
    X(PFNGLUNIFORM3FPROC, Uniform3f) \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv) \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray) \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLGENBUFFERSPROC, GenBuffers) \
    X(PFNGLBINDBUFFERPROC, BindBuffer) \
    X(PFNGLBUFFERDATAPROC, BufferData) \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer) \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture) \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers) \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer) \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer) \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers)

namespace bumpy {

struct Gl33 {
#define BUMPY_GL33_DECL(type, name) type name{};
    BUMPY_GL33_FUNCS(BUMPY_GL33_DECL)
#undef BUMPY_GL33_DECL
};

// Loads every pointer above from the CURRENT GL context. False if any is missing
// (caller treats that as "no GL 3.3" and falls back to the SDL_Renderer path).
[[nodiscard]] bool load_gl33(Gl33& gl);

}  // namespace bumpy
