#pragma once

/// @file UserMaterial.hpp
/// @brief 自作材質 (ADR 0100) の GPU に依存しない宣言。.mat.json の読み込み、パラメータの格納、材質用 HLSL の前置きを含む。
/// @details 材質は、material() だけを書く assets/materials/<name>.slang と、パラメータの名前と範囲を書く <name>.mat.json で構成する。
/// DX12 は .slang を陰影用の PS に組み込み、FXC でコンパイルする。Dawn は tools/bake_materials.py が Slang で変換した <name>.wgsl を読む。
/// 両方の backend が同じ前置き kUserMaterialPrelude を使うため、1 本の .slang を共用できる。

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/asset/AssetPack.hpp>

namespace mitiru::render
{

/// 1 つの材質が持てる float は最大 16 個。DX12CbDrawEx と Dawn の DrawCB の末尾にある float4 4 本に収める。
inline constexpr int kMaxUserMaterialParams = 16;
/// 1 つの renderer に登録できる材質は最大 32 個。
inline constexpr int kMaxUserMaterials = 32;
/// invokeExtension の id。ADR 0099 の表に従う。data は変更されたファイルのパスで、UTF-8 の終端 NUL を含む。再読み込みした材質の数を返す。
inline constexpr std::uint32_t kExtReloadUserMaterial = 1;

struct UserMaterialParam
{
	std::string name;
	float defaultValue = 0.0f;
	float minValue = 0.0f;
	float maxValue = 1.0f;
};

struct UserMaterialDesc
{
	std::string name;
	bool unlit = false;   ///< true なら照明を掛けず、baseColor と emissive をそのまま出す
	std::vector<UserMaterialParam> params;
};

/// 利用者の material() が読み書きする型。DX12 の HLSL と Dawn の Slang が共用する。
/// mitiruUserVec(i) は backend が先に定義する。float4 の i 番目で、i は 0 から 3。
inline constexpr const char* kUserMaterialPrelude = R"hlsl(
struct SurfaceIn
{
    float2 uv;           // 頂点の UV
    float3 worldPos;     // 世界座標
    float3 normal;       // 面の法線 (世界、正規化済み。法線マップの前)
    float3 view;         // 面からカメラへの向き (正規化済み)
    float4 vertexColor;  // 頂点色 (線形)
    float  time;         // material3D に渡した秒 (ゲームの時刻)
    float2 pixel;        // 画面の画素の位置 (左上が 0, 0)
};

struct SurfaceOut
{
    float3 baseColor;    // 線形。入ってくる値は 材質の色 x 頂点色 x 画像
    float  alpha;
    float3 normal;       // 陰影に使う法線 (世界)。輪郭線の下地へは出ない
    float3 emissive;     // 照明の後に足す色
    float  roughness;
    float  metallic;
    float  ao;
    bool   discardPixel; // true でこの画素を描かない
    // PBR の層。トゥーン・Phong・照明なしは読まない。入ってくる値は glTF の拡張 (KHR_materials_*) の係数で、0 は層なし
    float  clearCoat;          // クリアコートの強さ
    float  clearCoatRoughness;
    float  anisotropy;         // 異方性の強さ
    float  anisotropyRotation; // 異方性の向き (ラジアン)
    float3 sheenColor;         // シーン (布の縁の光) の色
    float  sheenRoughness;
};
)hlsl";

/// ファイル名にそのまま使うため、パスの区切りと .. は材質名に使えない。
[[nodiscard]] inline bool isValidUserMaterialName(std::string_view name) noexcept
{
	if (name.empty() || name.size() > 64) { return false; }
	return std::all_of(name.begin(), name.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
	});
}

[[nodiscard]] inline bool isValidParamIdentifier(std::string_view name) noexcept
{
	if (name.empty() || name.size() > 48) { return false; }
	if (name[0] >= '0' && name[0] <= '9') { return false; }
	return std::all_of(name.begin(), name.end(), [](char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	});
}

/// <name>.mat.json を読む。書式の誤りは、位置が分かる理由を error に返す。out.name は呼び出し側で設定する。
[[nodiscard]] inline bool parseUserMaterialJson(std::string_view text, UserMaterialDesc& out, std::string& error)
{
	const auto doc = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_object())
	{
		error = "JSON の object として読めません";
		return false;
	}
	UserMaterialDesc desc;
	desc.name = out.name;
	if (const auto it = doc.find("lighting"); it != doc.end())
	{
		const std::string v = it->is_string() ? it->get<std::string>() : std::string();
		if (v == "unlit") { desc.unlit = true; }
		else if (v != "scene")
		{
			error = "lighting は \"scene\" か \"unlit\" です";
			return false;
		}
	}
	if (const auto it = doc.find("params"); it != doc.end())
	{
		if (!it->is_array())
		{
			error = "params は配列です";
			return false;
		}
		for (const auto& p : *it)
		{
			if (desc.params.size() >= static_cast<std::size_t>(kMaxUserMaterialParams))
			{
				error = "params は " + std::to_string(kMaxUserMaterialParams) + " 個までです";
				return false;
			}
			UserMaterialParam param;
			if (!p.is_object() || !p.contains("name") || !p["name"].is_string())
			{
				error = "params の各要素は name を持つ object です";
				return false;
			}
			param.name = p["name"].get<std::string>();
			if (!isValidParamIdentifier(param.name))
			{
				error = "params の name \"" + param.name + "\" は英数字と _ だけの名前にしてください";
				return false;
			}
			for (const auto& other : desc.params)
			{
				if (other.name == param.name)
				{
					error = "params の name \"" + param.name + "\" が重なっています";
					return false;
				}
			}
			const auto num = [&p](const char* key, float fallback) {
				const auto f = p.find(key);
				return (f != p.end() && f->is_number()) ? f->get<float>() : fallback;
			};
			param.defaultValue = num("default", 0.0f);
			param.minValue = num("min", std::min(0.0f, param.defaultValue));
			param.maxValue = num("max", std::max(1.0f, param.defaultValue));
			if (!(param.minValue <= param.maxValue) || !std::isfinite(param.minValue) || !std::isfinite(param.maxValue) ||
			    !std::isfinite(param.defaultValue))
			{
				error = "params の \"" + param.name + "\" の min / max / default が不正です";
				return false;
			}
			param.defaultValue = std::clamp(param.defaultValue, param.minValue, param.maxValue);
			desc.params.push_back(std::move(param));
		}
	}
	out = std::move(desc);
	return true;
}

/// .slang の先頭に置く #define。param_<name> は float4 の i / 4 番目にある i % 4 番目の成分を指す。
[[nodiscard]] inline std::string userMaterialParamDefines(const UserMaterialDesc& desc)
{
	static constexpr const char* kLane[4] = {"x", "y", "z", "w"};
	std::string s;
	for (std::size_t i = 0; i < desc.params.size(); ++i)
	{
		s += "#define param_" + desc.params[i].name + " (mitiruUserVec(" + std::to_string(i / 4) + ")." + kLane[i % 4] + ")\n";
	}
	return s;
}

/// ゲームから渡された値を材質の範囲内に制限し、16 個の float に格納する。不足分と NaN、無限大には既定値を使う。
inline void packUserMaterialParams(const UserMaterialDesc& desc, const float* params, int count, float (&out)[kMaxUserMaterialParams])
{
	for (int i = 0; i < kMaxUserMaterialParams; ++i) { out[i] = 0.0f; }
	for (std::size_t i = 0; i < desc.params.size(); ++i)
	{
		const UserMaterialParam& p = desc.params[i];
		const bool given = params != nullptr && static_cast<int>(i) < count && std::isfinite(params[i]);
		out[i] = given ? std::clamp(params[i], p.minValue, p.maxValue) : p.defaultValue;
	}
}

struct UserMaterialSource
{
	UserMaterialDesc desc;
	std::string slang;        ///< <name>.slang の中身。Dawn は焼いた .wgsl を読むので空のことがある
	std::string jsonText;     ///< <name>.mat.json の中身 (無ければ空)
	std::string slangPath;    ///< 読んだ論理パス (エラーの表示用)
	std::string descPath;
};

[[nodiscard]] inline std::string userMaterialPath(std::string_view name, std::string_view extension)
{
	return "assets/materials/" + std::string(name) + std::string(extension);
}

/// <name>.mat.json だけを VFS から読む。.slang は読まない。.mat.json がなければ、パラメータなしの場面の照明を使う。
/// 変換済みの .wgsl を読む backend が使う。失敗時は error に理由を入れ、nullopt を返す。
[[nodiscard]] inline std::optional<UserMaterialSource> loadUserMaterialDesc(std::string_view name, std::string& error)
{
	if (!isValidUserMaterialName(name))
	{
		error = "材質の名前は英数字と _ と - だけで 64 文字までです";
		return std::nullopt;
	}
	UserMaterialSource src;
	src.desc.name = std::string(name);
	src.slangPath = userMaterialPath(name, ".slang");
	src.descPath = userMaterialPath(name, ".mat.json");
	if (const auto json = vfs::readAssetText(src.descPath))
	{
		std::string why;
		if (!parseUserMaterialJson(*json, src.desc, why))
		{
			error = src.descPath + ": " + why;
			return std::nullopt;
		}
		src.jsonText = *json;
	}
	return src;
}

/// assets/materials/<name>.slang と <name>.mat.json を VFS から読む。失敗時は error に理由を入れ、nullopt を返す。
[[nodiscard]] inline std::optional<UserMaterialSource> loadUserMaterialSource(std::string_view name, std::string& error)
{
	auto src = loadUserMaterialDesc(name, error);
	if (!src) { return std::nullopt; }
	const auto slang = vfs::readAssetText(src->slangPath);
	if (!slang)
	{
		error = src->slangPath + " を読めません";
		return std::nullopt;
	}
	src->slang = *slang;
	if (src->slang.find("material") == std::string::npos)
	{
		error = src->slangPath + " に material() がありません";
		return std::nullopt;
	}
	return src;
}

/// .slang と .mat.json の内容から FNV-1a 64 で求めた 16 桁の 16 進数。改行は LF にそろえる。
/// tools/bake_materials.py は同じ値を生成した .wgsl の先頭行に書き、読み込み側は生成後の変更を検出する。
[[nodiscard]] inline std::string userMaterialSourceHash(const UserMaterialSource& src)
{
	std::uint64_t h = 14695981039346656037ull;
	const auto feed = [&h](const std::string& s) {
		for (const char c : s)
		{
			if (c == '\r') { continue; }
			h = (h ^ static_cast<unsigned char>(c)) * 1099511628211ull;
		}
		h = (h ^ 0u) * 1099511628211ull;   // 2 つの文字列の境
	};
	feed(src.slang);
	feed(src.jsonText);
	static constexpr char kHex[] = "0123456789abcdef";
	std::string out(16, '0');
	for (int i = 15; i >= 0; --i, h >>= 4) { out[static_cast<std::size_t>(i)] = kHex[h & 0xF]; }
	return out;
}

/// 前置き、#define、元のファイル名と行番号に合わせる #line、利用者のコードの順に並べる。
/// コンパイラのエラーには .slang の行番号が出る。backend の glue は末尾に加える。
[[nodiscard]] inline std::string assembleUserMaterialCode(const UserMaterialSource& src)
{
	std::string s = kUserMaterialPrelude;
	s += userMaterialParamDefines(src.desc);
	s += "#line 1 \"" + src.desc.name + ".slang\"\n";
	s += src.slang;
	s += "\n#line 1 \"mitiru\"\n";
	return s;
}

} // namespace mitiru::render
