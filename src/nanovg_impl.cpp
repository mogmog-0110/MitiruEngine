/// @file nanovg_impl.cpp
/// @brief NanoVG 実装定義 — nanovg.c + GL3 backend をコンパイルする

// GLEW は他の GL ヘッダーより先にインクルードすること
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// nanovg が独自の STB 実装を取り込まないようにする —
// stb_image と stb_truetype は既に stb_impl.cpp でコンパイル済み。
// NVG_NO_STB は nanovg.c が stb_image をコンパイルするのを防ぐ。
// STBTT_STATIC は fontstash の stb_truetype シンボルを static (file-local) にし、
// stb_impl.lib との LNK2005 重複シンボルエラーを回避する。
#define NVG_NO_STB
#define STBTT_STATIC

// NanoVG 実装
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg.h"
#include "nanovg_gl.h"
