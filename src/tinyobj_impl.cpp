/// @file tinyobj_impl.cpp
/// @brief tinyobjloader の実装定義
/// @details TINYOBJLOADER_IMPLEMENTATION を定義して tiny_obj_loader.h をインクルードし、
///          実装シンボルをこの翻訳単位に限定する。
///          ObjLoaderTiny.hpp は宣言のみ参照するため、複数 TU から umbrella ヘッダーを
///          インクルードしても LNK2005 が発生しない。

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4505)
#endif

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
