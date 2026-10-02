#pragma once

/// @file ClodImport.hpp
/// @brief drawModel の import cache。OBJ / glTF / GLB を初回に .clod へ変換する
/// @details 変換の実体は src/clod_import_impl.cpp (meshopt_impl)。変換結果は
///          ソースの隣に `<source>.clod` として置き、ソースが新しくなったら作り直す。
///          テクスチャは各画像の隣に BC 圧縮した `<画像>.dds` を作る (TextureCompress.hpp)。

#include <optional>
#include <string>
#include <string_view>

namespace mitiru::render::clod
{

/// @brief drawModel が直接受け取れるモデル形式 (.obj / .gltf / .glb) か
[[nodiscard]] bool isImportableModelPath(std::string_view path) noexcept;

/// @brief `<source>.clod` cache を用意してその path を返す
/// @details cache が今の形式 (CLD6) でソースより新しければ変換せず、古くなった dds だけ作り直す。
///          失敗は nullopt + error に理由。
[[nodiscard]] std::optional<std::string> ensureClodCache(const std::string& sourcePath,
                                                         std::string& error);

}  // namespace mitiru::render::clod
