#pragma once

/// @file MiniaudioInclude.hpp
/// @brief エンジンのヘッダから miniaudio.h を読み込むときの入口
/// @details miniaudio.h は Windows で <windows.h> を読み込む。NOMINMAX が無いと min / max が
///          マクロになり、後に続くヘッダの std::min / std::max がすべてコンパイルできなくなる。

#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <miniaudio.h>
