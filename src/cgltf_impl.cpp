/// @file cgltf_impl.cpp
/// @brief cgltf の実装定義（vendor glue）
/// @details CGLTF_IMPLEMENTATION を定義して cgltf.h をインクルードし、cgltf の非 inline な
///          C 関数の実体をこの翻訳単位に限定する。GltfLoader.hpp は宣言のみ参照するため、
///          複数 TU から include しても LNK2005/LNK1169（重複定義）が発生しない
///          （tinyobj_impl.cpp / stb_impl.cpp と同じ方式。header-only 原則を守る）。

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100 4505 4996)
#endif

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
