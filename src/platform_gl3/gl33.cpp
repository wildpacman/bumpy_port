#include "platform_gl3/gl33.h"

namespace bumpy {

bool load_gl33(Gl33& gl) {
    bool ok = true;
#define BUMPY_GL33_LOAD(type, name)                                          \
    gl.name = reinterpret_cast<type>(SDL_GL_GetProcAddress("gl" #name));     \
    ok = ok && gl.name != nullptr;
    BUMPY_GL33_FUNCS(BUMPY_GL33_LOAD)
#undef BUMPY_GL33_LOAD
    return ok;
}

}  // namespace bumpy
