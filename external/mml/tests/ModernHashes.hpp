#pragma once
/// @file ModernHashes.hpp
/// @brief 現代的な音源の出力 PCM の SHA-256 の表を、tests/modern_hashes.txt から読む
/// @details 表をコードに埋めずにテキストにしてあるので、音を変えた後の作り直しにコンパイルは要らない。
///          テスト [hash] が、見つからなかった物と食い違った物の行 (example または source で始まる) を標準出力に書くので、
///          それを modern_hashes.txt に貼る。16bit ステレオをリトルエンディアンのバイト列としてハッシュした値で、
///          例の曲 (example) はディザあり、音源 (source) はディザなし。MSVC 17.14 (x64、Debug と Release で同じ値) で記録した。

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef MITIRU_MML_TEST_DIR
#define MITIRU_MML_TEST_DIR "."
#endif

namespace mitiru_mml_test
{

/// @brief 基準の出力 1 件
struct ModernHashEntry
{
	std::string kind;   ///< example (曲の例) か source (音源 1 つ)
	std::string name;
	std::size_t frames = 0;  ///< ステレオのフレーム数 (44100 Hz)
	std::string sha256;
};

/// @brief 表を読む (ファイルが無い、行が読めないときは、その分が空になる)
[[nodiscard]] inline const std::vector<ModernHashEntry>& modernHashTable()
{
	static const std::vector<ModernHashEntry> TABLE = []()
	{
		std::vector<ModernHashEntry> table;
		std::ifstream in(std::string(MITIRU_MML_TEST_DIR) + "/modern_hashes.txt");
		std::string line;
		while (std::getline(in, line))
		{
			if (line.empty() || line[0] == '#')
			{
				continue;
			}
			std::istringstream words(line);
			ModernHashEntry e;
			if (words >> e.kind >> e.name >> e.frames >> e.sha256)
			{
				table.push_back(e);
			}
		}
		return table;
	}();
	return TABLE;
}

[[nodiscard]] inline const ModernHashEntry* findHash(const std::string& kind, const std::string& name)
{
	for (const auto& entry : modernHashTable())
	{
		if (entry.kind == kind && entry.name == name)
		{
			return &entry;
		}
	}
	return nullptr;
}

/// @brief 曲の例の記録を探す (無ければ nullptr)
[[nodiscard]] inline const ModernHashEntry* findModernHash(const std::string& name)
{
	return findHash("example", name);
}

/// @brief 音源 1 つの記録を探す (無ければ nullptr)
[[nodiscard]] inline const ModernHashEntry* findSourceHash(const std::string& name)
{
	return findHash("source", name);
}

} // namespace mitiru_mml_test
