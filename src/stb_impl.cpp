/// @file stb_impl.cpp
/// @brief stbライブラリの実装定義
/// @details stb_image / stb_truetype の実装をコンパイル単位に分離する。
///          ヘッダーオンリーのmitiruライブラリはINTERFACEだが、
///          stbはCライブラリのため別ターゲット(stb_impl)として静的リンクする。

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4456 4457 4100 4505)
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
