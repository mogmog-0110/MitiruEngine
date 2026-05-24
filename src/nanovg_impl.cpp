/// @file nanovg_impl.cpp
/// @brief NanoVG implementation — compiles nanovg.c + GL3 backend

// GLEW must come before any GL headers
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Prevent nanovg from pulling in its own STB implementations —
// stb_image and stb_truetype are already compiled in stb_impl.cpp.
// NVG_NO_STB prevents nanovg.c from compiling stb_image.
// STBTT_STATIC makes fontstash's stb_truetype symbols static (file-local),
// avoiding LNK2005 duplicate symbol errors with stb_impl.lib.
#define NVG_NO_STB
#define STBTT_STATIC

// NanoVG implementation
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"
