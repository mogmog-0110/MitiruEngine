#pragma once
/// @file Json.hpp
/// @brief JSON操作 — nlohmann/json ベース
/// @details MitiruEngineのJSON操作を nlohmann/json に移行するブリッジ。
///          既存のJsonBuilder/JsonReaderと互換性を保ちながら、
///          nlohmann::jsonの全機能にアクセス可能。

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <fstream>

namespace mitiru::data {

using Json = nlohmann::json;

/// @brief ファイルからJSON読み込み
[[nodiscard]] inline std::optional<Json> loadJsonFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;
    try {
        Json j;
        file >> j;
        return j;
    } catch (...) {
        return std::nullopt;
    }
}

/// @brief JSONをファイルに保存
inline bool saveJsonFile(const std::string& path, const Json& j, int indent = 2) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(indent);
    return true;
}

/// @brief 文字列からJSONパース
[[nodiscard]] inline std::optional<Json> parseJson(const std::string& str) {
    try {
        return Json::parse(str);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace mitiru::data
