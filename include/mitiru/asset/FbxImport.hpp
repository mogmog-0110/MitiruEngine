#pragma once

/// @file FbxImport.hpp
/// @brief FBX を glb に変換して、glTF と同じ読み込み口へ渡す (ufbx)
/// @details 変換の実体は src/fbx_import_impl.cpp。初回の読み込みでソースの隣に `<source>.glb` を書き、
///          ソースが新しくなったら作り直す。以後 drawModel / スキンモデル / clod の import は
///          この glb を読むので、FBX 由来かどうかを区別しない。
///          座標は glTF と同じ右手系 Y-up、1 単位 = 1 m にそろえる (Mixamo の cm、Z-up の FBX も同じ寸法になる)。
///          動作 (AnimStack) は線形キーに焼き、名前の `Armature|Run` は `Run` にする (Blender の glTF 出力と同じ名前)。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <mitiru/render/GlbWriter.hpp>

namespace mitiru::asset
{

/// @brief 拡張子が .fbx か (大文字小文字を区別しない)
[[nodiscard]] bool isFbxPath(std::string_view path) noexcept;

/// @brief FBX のバイト列を glb の文書へ変換する
/// @param sourceDir 外部テクスチャを探す基準 (FBX の置き場所、UTF-8)。glb もここに置く前提で相対 uri を作る
/// @param fileName FBX のファイル名 (UTF-8)。PNG / JPEG 以外の埋め込み画像は `<fileName>.texN.<拡張子>` として
///        sourceDir に書き出す。空なら書き出さず、名前だけを uri に残す
[[nodiscard]] std::optional<render::GlbDocument> importFbx(const void* data, std::size_t size,
                                                           const std::string& sourceDir,
                                                           const std::string& fileName,
                                                           std::string& error);

/// @brief `<fbx>.glb` を用意してその path を返す
/// @details 相対パスは vfs::readGlobal と同じく cwd → MITIRU_ASSET_ROOT の順に探す。
///          cache が変換器の今の版でソースより新しければ変換しない。失敗は nullopt + error に理由。
[[nodiscard]] std::optional<std::string> ensureFbxGlbCache(const std::string& fbxPath, std::string& error);

}  // namespace mitiru::asset
