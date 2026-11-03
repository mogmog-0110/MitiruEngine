#pragma once

/// @file GltfMeshoptDecode.hpp
/// @brief EXT_meshopt_compression で縮めた glTF の bufferView を meshoptimizer で戻す
/// @details cgltf は拡張を読むだけで復号しない。`cgltf_load_buffers` の後に 1 回呼ぶと、縮めた bufferView ごとに
///          元の大きさの領域を確保して `view->data` に置く。以後の `cgltf_accessor_read_*` はそちらを読む。
///          領域は `cgltf_free` が解放する。実体は src/gltf_meshopt_decode.cpp (meshopt_codec)。

#include <cgltf.h>

namespace mitiru::render
{

/// @brief 縮めた bufferView をすべて戻す。縮めた物が無ければ何もせず success
/// @return 壊れたデータや足りない buffer は cgltf_result_invalid_gltf / data_too_short / out_of_memory
[[nodiscard]] cgltf_result decodeMeshoptBufferViews(cgltf_data* data);

} // namespace mitiru::render
