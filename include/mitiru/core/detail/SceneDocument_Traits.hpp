// SceneDocument の detail header - 直接 include しないこと。core/SceneDocument.hpp 経由で include される
#pragma once

/// @file SceneDocument_Traits.hpp
/// @brief SceneDocument の Trait 層。合成可能な機能単位 + JSON 変換 helper
/// @details Node / Scene (core/SceneDocument.hpp) が依存する下層。
///          ITrait と標準 Trait 群 (mesh/light/camera/physics/script/audio/custom)、
///          および Trait 内部で使う nlohmann/json helper を収める。

#include <algorithm>
#include <map>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace mitiru
{

namespace detail
{

/// @brief Trait 内部で使う JSON シリアライズ用エイリアス
using TraitJson = nlohmann::json;

/// @brief 文字列を寛容にパースする（失敗時は空オブジェクト）
/// @details fromJson() は古い保存ファイル・部分的フラグメント・空文字列など
///          幅広い入力を受けるため、例外を投げず常に nlohmann::json を返す。
inline TraitJson parseTraitJson(const std::string& s) noexcept
{
	if (s.empty()) return TraitJson::object();
	auto j = TraitJson::parse(s, nullptr, /*allow_exceptions=*/false);
	if (j.is_discarded()) return TraitJson::object();
	return j;
}

/// @brief 配列値を float* に書き出す（要素数不足は無視）
inline void readFloatArray(const TraitJson& j, const char* key, float* out, int count)
{
	auto it = j.find(key);
	if (it == j.end() || !it->is_array()) return;
	const auto n = std::min<std::size_t>(it->size(), static_cast<std::size_t>(count));
	for (std::size_t i = 0; i < n; ++i)
	{
		const auto& v = (*it)[i];
		if (v.is_number()) out[i] = v.get<float>();
	}
}

/// @brief float[N] を JSON 配列にする
inline TraitJson floatArray(const float* in, int count)
{
	TraitJson arr = TraitJson::array();
	for (int i = 0; i < count; ++i) arr.push_back(in[i]);
	return arr;
}

} // namespace detail

// =============================================================================
// Traits。合成可能な機能単位
// =============================================================================

/// @brief Trait基底インターフェース
struct ITrait
{
	virtual ~ITrait() = default;

	/// @brief Trait種別を返す
	[[nodiscard]] virtual std::string traitType() const = 0;

	/// @brief TraitをJSON文字列にシリアライズする
	[[nodiscard]] virtual std::string toJson() const = 0;

	/// @brief JSON文字列からTraitを復元する
	virtual void fromJson(const std::string& json) = 0;
};

/// @brief メッシュトレイト。見た目
struct MeshTrait : ITrait
{
	std::string meshPath;      ///< メッシュファイルパス
	std::string materialName;  ///< マテリアル名

	[[nodiscard]] std::string traitType() const override { return "mesh"; }

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = "mesh";
		if (!meshPath.empty()) j["meshPath"] = meshPath;
		if (!materialName.empty()) j["materialName"] = materialName;
		return j.dump();
	}

	void fromJson(const std::string& json) override
	{
		const auto j = detail::parseTraitJson(json);
		meshPath = j.value("meshPath", std::string{});
		materialName = j.value("materialName", std::string{});
	}
};

/// @brief ライトトレイト。光
struct LightTrait : ITrait
{
	std::string lightType = "directional"; ///< directional/point/spot
	float color[3] = {1.0f, 1.0f, 1.0f};  ///< ライト色 (RGB)
	float intensity = 1.0f;                ///< ライト強度
	float range = 10.0f;                   ///< 影響範囲（point/spot用）
	float spotAngle = 45.0f;               ///< スポット角度（spot用）

	[[nodiscard]] std::string traitType() const override { return "light"; }

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = "light";
		j["lightType"] = lightType;
		j["color"] = detail::floatArray(color, 3);
		j["intensity"] = intensity;
		j["range"] = range;
		j["spotAngle"] = spotAngle;
		return j.dump();
	}

	void fromJson(const std::string& json) override
	{
		const auto j = detail::parseTraitJson(json);
		lightType = j.value("lightType", std::string{"directional"});
		intensity = j.value("intensity", 1.0f);
		range = j.value("range", 10.0f);
		spotAngle = j.value("spotAngle", 45.0f);
		detail::readFloatArray(j, "color", color, 3);
	}
};

/// @brief カメラトレイト。視点
struct CameraTrait : ITrait
{
	float fov = 60.0f;         ///< 視野角（度）
	float nearClip = 0.1f;     ///< ニアクリップ
	float farClip = 1000.0f;   ///< ファークリップ
	float target[3] = {0, 0, 0}; ///< 注視点

	[[nodiscard]] std::string traitType() const override { return "camera"; }

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = "camera";
		j["fov"] = fov;
		j["nearClip"] = nearClip;
		j["farClip"] = farClip;
		return j.dump();
	}

	void fromJson(const std::string& json) override
	{
		const auto j = detail::parseTraitJson(json);
		fov = j.value("fov", 60.0f);
		nearClip = j.value("nearClip", 0.1f);
		farClip = j.value("farClip", 1000.0f);
	}
};

/// @brief 物理トレイト
struct PhysicsTrait : ITrait
{
	std::string bodyType = "dynamic"; ///< static/dynamic/kinematic
	float mass = 1.0f;               ///< 質量
	float friction = 0.5f;           ///< 摩擦係数
	float restitution = 0.3f;        ///< 反発係数

	// コライダー形状設定
	std::string colliderType = "box";      ///< "box", "sphere", "capsule", "mesh", "none"
	float colliderSize[3] = {1, 1, 1};     ///< box: half-extents, sphere: [radius,0,0], capsule: [radius,height,0]
	float colliderOffset[3] = {0, 0, 0};   ///< ノード位置からのオフセット
	bool isTrigger = false;                ///< トリガー（物理応答なし、イベントのみ）
	int collisionLayer = 0;                ///< 所属レイヤー (0-31)
	int collisionMask = 0x7FFFFFFF;        ///< 衝突対象レイヤーマスク

	[[nodiscard]] std::string traitType() const override { return "physics"; }

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = "physics";
		j["bodyType"] = bodyType;
		j["mass"] = mass;
		j["friction"] = friction;
		j["restitution"] = restitution;
		j["colliderType"] = colliderType;
		j["colliderSize"] = detail::floatArray(colliderSize, 3);
		j["colliderOffset"] = detail::floatArray(colliderOffset, 3);
		j["isTrigger"] = isTrigger;
		j["collisionLayer"] = collisionLayer;
		j["collisionMask"] = collisionMask;
		return j.dump();
	}

	void fromJson(const std::string& json) override
	{
		const auto j = detail::parseTraitJson(json);
		bodyType = j.value("bodyType", std::string{"dynamic"});
		mass = j.value("mass", 1.0f);
		friction = j.value("friction", 0.5f);
		restitution = j.value("restitution", 0.3f);
		colliderType = j.value("colliderType", std::string{"box"});
		detail::readFloatArray(j, "colliderSize", colliderSize, 3);
		detail::readFloatArray(j, "colliderOffset", colliderOffset, 3);
		isTrigger = j.value("isTrigger", false);
		collisionLayer = j.value("collisionLayer", 0);
		collisionMask = j.value("collisionMask", 0x7FFFFFFF);
	}
};

/// @brief スクリプトトレイト
struct ScriptTrait : ITrait
{
	std::string scriptPath;                       ///< スクリプトファイルパス
	std::map<std::string, std::string> variables; ///< スクリプト変数

	[[nodiscard]] std::string traitType() const override { return "script"; }

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = "script";
		if (!scriptPath.empty()) j["scriptPath"] = scriptPath;
		if (!variables.empty())
		{
			detail::TraitJson vars = detail::TraitJson::object();
			for (const auto& [k, v] : variables) vars[k] = v;
			j["variables"] = std::move(vars);
		}
		return j.dump();
	}

	void fromJson(const std::string& json) override
	{
		const auto j = detail::parseTraitJson(json);
		scriptPath = j.value("scriptPath", std::string{});
		variables.clear();
		auto it = j.find("variables");
		if (it != j.end() && it->is_object())
		{
			for (const auto& [k, v] : it->items())
			{
				if (v.is_string()) variables.emplace(k, v.get<std::string>());
			}
		}
	}
};

/// @brief オーディオトレイト
struct AudioTrait : ITrait
{
	std::string audioPath;   ///< 音声ファイルパス
	float volume = 1.0f;     ///< 音量
	bool loop = false;       ///< ループ再生
	bool spatial = false;    ///< 空間オーディオ

	[[nodiscard]] std::string traitType() const override { return "audio"; }

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = "audio";
		if (!audioPath.empty()) j["audioPath"] = audioPath;
		j["volume"] = volume;
		j["loop"] = loop;
		j["spatial"] = spatial;
		return j.dump();
	}

	void fromJson(const std::string& json) override
	{
		const auto j = detail::parseTraitJson(json);
		audioPath = j.value("audioPath", std::string{});
		volume = j.value("volume", 1.0f);
		loop = j.value("loop", false);
		spatial = j.value("spatial", false);
	}
};

/// @brief カスタムトレイト。ゲーム固有データ
struct CustomTrait : ITrait
{
	std::string customType;                        ///< カスタム種別名
	std::map<std::string, std::string> properties; ///< キーバリューストア

	[[nodiscard]] std::string traitType() const override
	{
		return customType.empty() ? "custom" : customType;
	}

	[[nodiscard]] std::string toJson() const override
	{
		detail::TraitJson j;
		j["type"] = traitType();
		for (const auto& [k, v] : properties)
		{
			// "type" は既存実装と整合させて上書きされないよう優先順位を下げる。
			if (k == "type") continue;
			j[k] = v;
		}
		return j.dump();
	}

	void fromJson(const std::string& /*json*/) override
	{
		// ゲーム固有: サブクラスまたは利用側で実装
	}
};

} // namespace mitiru
