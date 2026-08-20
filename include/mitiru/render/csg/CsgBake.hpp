#pragma once

/// @file CsgBake.hpp
/// @brief インポート時に焼かれた CSG シェーダ（DXIL）を読む
/// @details Makina 側の `makina_bake` が `<name>.vs.cso` / `<name>.ps.cso` /
///          `<name>.csgbake.json` を出す。ここはそれを読むだけで、コンパイラは呼ばない。
///
///          @b なぜ焼くのか：シーンごとに直線コードを生成するのが一番速いが、
///          それには実行時に DXC が要る。プロップ 1 個読むためにシェーダコンパイラを
///          同梱するわけにはいかない。かといって評価プログラムをバッファから解釈すると、
///          実測で 25 ノード 5.6 倍・75 ノード 11.4 倍遅く、1280x720 でプロップ 1 個に
///          55 ms かかる（makina/docs/SPIKE_PERF.md 9）。
///          **コンパイルをインポート時に動かす**のが両方を満たす唯一の形である。
///
///          @b ハッシュ照合が要る理由：シーンを編集して焼き直し忘れると、
///          古い .cso が読めて、描けて、**黙って違う形になる**。
///          落ちてくれないのが最悪なので、マニフェストの `sceneHash` を
///          `CsgSolid::sourceHash()` と突き合わせる。
///
///          `MITIRU_HAS_MAKINA` が立っていないビルドではこのヘッダーは空になる。

#ifdef MITIRU_HAS_MAKINA

#include <mitiru/render/csg/CsgSolid.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mitiru::render::csg
{

/// @brief 焼かれた頂点シェーダとピクセルシェーダ、およびその出自
class CsgBake
{
public:
	/// @brief このビルドが読めるマニフェストの版
	/// @details 新しい版は拒否する。読み方が変わっているのに古い読み手が通してしまうと、
	///          知らないフィールドを無視したまま動いてしまう。
	static constexpr int kSupportedVersion = 1;

	CsgBake() = default;

	/// @brief `<name>.csgbake.json` を読み、隣の .cso を取り込む
	/// @return 読めなかったときは false。理由は @ref error
	[[nodiscard]] bool loadFromFile(const std::string& manifestPath)
	{
		m_valid = false;
		m_vs.clear();
		m_ps.clear();

		std::string text;
		if (!readWhole(manifestPath, text))
		{
			return fail("could not open '" + manifestPath + "'");
		}

		nlohmann::json j;
		try
		{
			j = nlohmann::json::parse(text);
		}
		catch (const std::exception& e)
		{
			return fail(std::string("manifest is not well formed JSON: ") + e.what());
		}

		if (j.value("format", std::string()) != "makina-bake")
		{
			return fail("'" + manifestPath + "' is not a makina bake manifest");
		}
		const int version = j.value("version", 0);
		if (version > kSupportedVersion)
		{
			return fail("bake manifest version " + std::to_string(version) +
			            " is newer than this build understands (" +
			            std::to_string(kSupportedVersion) + ")");
		}

		m_sceneHash = j.value("sceneHash", std::string());
		m_sceneName = j.value("scene", std::string());
		m_shading = j.value("shading", std::string());
		m_programNodes = j.value("programNodes", 0);
		// D-15: 葉の数値をプログラムバッファから読むシェーダか。無ければ false —
		// 古いマニフェストのシェーダは数値を焼き込んでいて、載せたものを読まない。
		m_live = j.value("live", false);

		const std::string dir = directoryOf(manifestPath);
		const std::string vsName = j.value("vs", std::string());
		const std::string psName = j.value("ps", std::string());
		if (vsName.empty() || psName.empty())
		{
			return fail("bake manifest names no shader blobs");
		}
		if (!readBinary(dir + vsName, m_vs))
		{
			return fail("could not open '" + dir + vsName + "' named by the manifest");
		}
		if (!readBinary(dir + psName, m_ps))
		{
			return fail("could not open '" + dir + psName + "' named by the manifest");
		}
		if (m_vs.empty() || m_ps.empty())
		{
			return fail("a shader blob named by the manifest is empty");
		}

		m_valid = true;
		m_error.clear();
		return true;
	}

	/// @brief この bake が渡されたシーンから焼かれたものか
	/// @details **これに通らない bake を描いてはいけない。** 古い .cso は問題なく
	///          PSO になり、問題なく描画され、形だけが違う。
	[[nodiscard]] bool matches(const CsgSolid& solid) const
	{
		return m_valid && solid.valid() && !m_sceneHash.empty() &&
		       m_sceneHash == solid.sourceHash();
	}

	[[nodiscard]] bool valid() const noexcept { return m_valid; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	[[nodiscard]] const std::vector<std::uint8_t>& vertexShader() const noexcept { return m_vs; }
	[[nodiscard]] const std::vector<std::uint8_t>& pixelShader() const noexcept { return m_ps; }

	[[nodiscard]] const std::string& sceneHash() const noexcept { return m_sceneHash; }
	[[nodiscard]] const std::string& sceneName() const noexcept { return m_sceneName; }
	/// @brief どの見た目で焼かれたか（`scene_shading.hlsl` / `scene_weathered.hlsl` など）
	[[nodiscard]] const std::string& shading() const noexcept { return m_shading; }
	[[nodiscard]] int programNodes() const noexcept { return m_programNodes; }
	/// @brief 葉の数値をプログラムバッファから読む (毎フレーム姿を載せ替えられる) か
	[[nodiscard]] bool live() const noexcept { return m_live; }

private:
	/// 失敗時は掴みかけたものを全部落とす。
	///
	/// 読み込みは途中まで進みうる — vs は読めて ps が無い、など。そこで vs を残すと、
	/// `valid()` を見ずに `vertexShader()` を取った呼び手が**前回の、あるいは半端な**
	/// ブロブで PSO を作ってしまう。失敗したら何も持っていない状態に戻す。
	bool fail(const std::string& why)
	{
		m_vs.clear();
		m_ps.clear();
		m_sceneHash.clear();
		m_sceneName.clear();
		m_shading.clear();
		m_programNodes = 0;
		m_live = false;
		m_valid = false;
		m_error = why;
		return false;
	}

	static bool readWhole(const std::string& path, std::string& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			return false;
		}
		out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return true;
	}

	static bool readBinary(const std::string& path, std::vector<std::uint8_t>& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			return false;
		}
		out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		return true;
	}

	/// マニフェストが名指しする .cso はその隣にある。区切りが無ければカレント。
	static std::string directoryOf(const std::string& path)
	{
		const std::size_t cut = path.find_last_of("/\\");
		return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
	}

	std::vector<std::uint8_t> m_vs;
	std::vector<std::uint8_t> m_ps;
	std::string m_sceneHash;
	std::string m_sceneName;
	std::string m_shading;
	int m_programNodes = 0;
	bool m_live = false;
	bool m_valid = false;
	std::string m_error;
};

} // namespace mitiru::render::csg

#endif // MITIRU_HAS_MAKINA
