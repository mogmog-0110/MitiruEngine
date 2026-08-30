#pragma once

/// @file ClodImport.hpp
/// @brief drawModel の import cache。OBJ / glTF / GLB を初回に .clod へ変換する
/// @details 変換の実体は src/clod_import_impl.cpp (meshopt_impl)。変換結果は
///          ソースの隣に `<source>.clod` として置き、ソースが新しくなったら作り直す。

#include <optional>
#include <string>
#include <string_view>

namespace mitiru::render::clod
{

/// @brief drawModel が直接受け取れるモデル形式 (.obj / .gltf / .glb) か
[[nodiscard]] bool isImportableModelPath(std::string_view path) noexcept;

/// @brief `<source>.clod` cache を用意してその path を返す
/// @details cache がソースより新しければ変換しない。失敗は nullopt + error に理由。
[[nodiscard]] std::optional<std::string> ensureClodCache(const std::string& sourcePath,
                                                         std::string& error);

}  // namespace mitiru::render::clod
