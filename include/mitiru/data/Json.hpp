#pragma once
/// @file Json.hpp
/// @brief nlohmann::json のファイル読み書きと文字列パース (失敗は nullopt / false で返す)

#include <nlohmann/json.hpp>
#include <string>
#include <optional>
#include <fstream>

namespace mitiru::data {

using Json = nlohmann::json;

/// @brief ファイルから JSON を読み込む
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

/// @brief JSON をファイルに保存する
inline bool saveJsonFile(const std::string& path, const Json& j, int indent = 2) {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << j.dump(indent);
    return true;
}

/// @brief 文字列から JSON をパースする
[[nodiscard]] inline std::optional<Json> parseJson(const std::string& str) {
    try {
        return Json::parse(str);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace mitiru::data
