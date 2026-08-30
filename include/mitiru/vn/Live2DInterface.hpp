#pragma once

/// @file Live2DInterface.hpp
/// @brief Live2D / Spine キャラクターアニメーション統合インターフェース
/// @details アニメーションキャラクター（Live2D, Spine等）をVNシステムに統合する
///          抽象インターフェース群。IAnimatedCharacterを実装することで任意の
///          アニメーションエンジンを差し替え可能。AnimatedCharacterManagerは
///          CharacterManagerと同等のAPIを提供し、静的スプライトへの自動フォールバック
///          も備える。
///
/// @code
/// // ファクトリを登録
/// auto factory = std::make_unique<MyLive2DFactory>();
/// mitiru::vn::AnimatedCharacterManager mgr(std::move(factory));
///
/// // キャラクター表示
/// mgr.show("sakura", mitiru::vn::CharacterPosition::Center);
/// mgr.setExpression("sakura", "smile");
/// mgr.setMotion("sakura", "greeting", false);
///
/// // 毎フレーム更新
/// mgr.update(dt, mouseX, mouseY);
///
/// // 描画（VNRendererと統合）
/// for (const auto& entry : mgr.visibleCharacters()) {
///     auto tex = mgr.renderCharacter(entry.id);
///     screen.drawSprite(tex, dstRect);
/// }
/// @endcode

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/render/Texture.hpp>
#include <mitiru/vn/CharacterManager.hpp>

namespace mitiru::vn
{

// ════════════════════════════════════════════════════════════════════
//  データ型
// ════════════════════════════════════════════════════════════════════

/// @brief アニメーションキャラクターのパラメータ情報
struct AnimatedCharacterParam
{
	std::string name;							///< パラメータ名
	float minValue = 0.0f;						///< 最小値
	float maxValue = 1.0f;						///< 最大値
	float defaultValue = 0.0f;					///< デフォルト値
};

/// @brief ヒットテスト結果
struct HitTestResult
{
	bool hit = false;							///< ヒットしたか
	std::string areaName;						///< ヒット領域名（"head", "body"等）
};

/// @brief リップシンク設定
struct LipSyncConfig
{
	bool enabled = false;						///< リップシンク有効/無効
	std::string parameterName = "ParamMouthOpenY";	///< 口の開閉パラメータ名
	float sensitivity = 1.0f;					///< 音量感度（0.0-2.0）
	float smoothing = 0.1f;						///< スムージング係数（0.0-1.0）
};

/// @brief アイトラッキング設定
struct EyeTrackingConfig
{
	bool enabled = true;						///< アイトラッキング有効/無効
	std::string paramXName = "ParamEyeBallX";	///< 視線X方向パラメータ名
	std::string paramYName = "ParamEyeBallY";	///< 視線Y方向パラメータ名
	float maxAngle = 30.0f;						///< 最大追従角度（度）
	float speed = 5.0f;							///< 追従速度
};

/// @brief アイドルモーション設定
struct IdleMotionConfig
{
	bool enabled = true;						///< アイドルモーション有効/無効
	std::string breathingParam = "ParamBreath";	///< 呼吸パラメータ名
	float breathingSpeed = 0.5f;				///< 呼吸速度（Hz）
	float breathingAmplitude = 0.5f;			///< 呼吸振幅
	std::string idleMotionName = "idle";		///< アイドルモーション名
};

// ════════════════════════════════════════════════════════════════════
//  IAnimatedCharacter。アニメーションキャラクター抽象インターフェース
// ════════════════════════════════════════════════════════════════════

/// @brief アニメーションキャラクターの抽象インターフェース
/// @details Live2D, Spine等の具体実装はこのインターフェースを実装する。
class IAnimatedCharacter
{
public:
	virtual ~IAnimatedCharacter() = default;

	/// @brief モデルを読み込む
	/// @param path モデルファイルパス
	/// @return 成功ならtrue
	[[nodiscard]] virtual bool loadModel(const std::string& path) = 0;

	/// @brief 表情を設定する（ブレンド遷移）
	/// @param name 表情名
	virtual void setExpression(const std::string& name) = 0;

	/// @brief モーションを再生する
	/// @param name モーション名
	/// @param loop ループ再生するか
	virtual void setMotion(const std::string& name, bool loop = false) = 0;

	/// @brief パラメータを直接制御する
	/// @param name パラメータ名
	/// @param value 値
	virtual void setParameter(const std::string& name, float value) = 0;

	/// @brief パラメータ値を取得する
	/// @param name パラメータ名
	/// @return 値（存在しない場合はnullopt）
	[[nodiscard]] virtual std::optional<float> getParameter(const std::string& name) const = 0;

	/// @brief 利用可能なパラメータ一覧を取得する
	/// @return パラメータ情報のリスト
	[[nodiscard]] virtual std::vector<AnimatedCharacterParam> parameters() const = 0;

	/// @brief アニメーションを更新する
	/// @param dt デルタタイム（秒）
	virtual void update(float dt) = 0;

	/// @brief 現在のフレームをテクスチャにレンダリングする
	/// @param x 描画X座標
	/// @param y 描画Y座標
	/// @param scale スケール
	/// @return レンダリングされたテクスチャ
	[[nodiscard]] virtual render::Texture render(float x, float y, float scale) = 0;

	/// @brief ヒットテストを実行する
	/// @param x ローカルX座標
	/// @param y ローカルY座標
	/// @return ヒットテスト結果
	[[nodiscard]] virtual HitTestResult hitTest(float x, float y) const = 0;

	/// @brief モデルが読み込み済みか
	[[nodiscard]] virtual bool isLoaded() const = 0;

	/// @brief 利用可能な表情名一覧を取得する
	[[nodiscard]] virtual std::vector<std::string> availableExpressions() const = 0;

	/// @brief 利用可能なモーション名一覧を取得する
	[[nodiscard]] virtual std::vector<std::string> availableMotions() const = 0;
};

// ════════════════════════════════════════════════════════════════════
//  IAnimatedCharacterFactory。ファクトリインターフェース
// ════════════════════════════════════════════════════════════════════

/// @brief アニメーションキャラクターのファクトリ
/// @details モデルパスからIAnimatedCharacterインスタンスを生成する。
class IAnimatedCharacterFactory
{
public:
	virtual ~IAnimatedCharacterFactory() = default;

	/// @brief アニメーションキャラクターを生成する
	/// @param modelPath モデルファイルパス
	/// @return 生成されたキャラクター（失敗時はnullptr）
	[[nodiscard]] virtual std::unique_ptr<IAnimatedCharacter> create(
		const std::string& modelPath) = 0;

	/// @brief 指定パスのモデルが読み込み可能か
	/// @param modelPath モデルファイルパス
	/// @return 対応していればtrue
	[[nodiscard]] virtual bool canLoad(const std::string& modelPath) const = 0;
};

// ════════════════════════════════════════════════════════════════════
//  NullAnimatedCharacter。テスト用スタブ実装
// ════════════════════════════════════════════════════════════════════

/// @brief テスト用アニメーションキャラクタースタブ
/// @details 実際のアニメーションエンジンなしで動作する。
///          単色テクスチャを返し、パラメータ操作をシミュレートする。
class NullAnimatedCharacter final : public IAnimatedCharacter
{
public:
	/// @brief コンストラクタ
	/// @param width レンダリング幅
	/// @param height レンダリング高さ
	/// @param r スタブ色・赤
	/// @param g スタブ色・緑
	/// @param b スタブ色・青
	explicit NullAnimatedCharacter(int width = 256, int height = 512,
	                               std::uint8_t r = 180, std::uint8_t g = 160,
	                               std::uint8_t b = 200) noexcept
		: m_width(width)
		, m_height(height)
		, m_r(r), m_g(g), m_b(b)
	{
	}

	[[nodiscard]] bool loadModel([[maybe_unused]] const std::string& path) override
	{
		m_loaded = true;
		m_modelPath = path;
		return true;
	}

	void setExpression(const std::string& name) override
	{
		m_currentExpression = name;
	}

	void setMotion(const std::string& name, bool loop) override
	{
		m_currentMotion = name;
		m_motionLoop = loop;
	}

	void setParameter(const std::string& name, float value) override
	{
		m_parameters[name] = value;
	}

	[[nodiscard]] std::optional<float> getParameter(const std::string& name) const override
	{
		auto it = m_parameters.find(name);
		if (it != m_parameters.end())
		{
			return it->second;
		}
		return std::nullopt;
	}

	[[nodiscard]] std::vector<AnimatedCharacterParam> parameters() const override
	{
		std::vector<AnimatedCharacterParam> result;
		for (const auto& [name, value] : m_parameters)
		{
			result.push_back({name, 0.0f, 1.0f, value});
		}
		return result;
	}

	void update([[maybe_unused]] float dt) override
	{
		// スタブ: 何もしない
	}

	[[nodiscard]] render::Texture render(
		[[maybe_unused]] float x,
		[[maybe_unused]] float y,
		[[maybe_unused]] float scale) override
	{
		return render::Texture::solid(m_width, m_height, m_r, m_g, m_b, 255);
	}

	[[nodiscard]] HitTestResult hitTest(float x, float y) const override
	{
		const bool inside = (x >= 0.0f && x < static_cast<float>(m_width) &&
		                     y >= 0.0f && y < static_cast<float>(m_height));
		if (inside)
		{
			// 上半分は "head"、下半分は "body"
			const std::string area = (y < static_cast<float>(m_height) * 0.5f)
				? "head" : "body";
			return {true, area};
		}
		return {false, ""};
	}

	[[nodiscard]] bool isLoaded() const override { return m_loaded; }

	[[nodiscard]] std::vector<std::string> availableExpressions() const override
	{
		return {"normal", "smile", "angry", "sad", "surprised"};
	}

	[[nodiscard]] std::vector<std::string> availableMotions() const override
	{
		return {"idle", "greeting", "nod", "shake_head"};
	}

private:
	int m_width;
	int m_height;
	std::uint8_t m_r, m_g, m_b;
	bool m_loaded = false;
	std::string m_modelPath;
	std::string m_currentExpression;
	std::string m_currentMotion;
	bool m_motionLoop = false;
	std::unordered_map<std::string, float> m_parameters;
};

/// @brief NullAnimatedCharacterのファクトリ
class NullAnimatedCharacterFactory final : public IAnimatedCharacterFactory
{
public:
	[[nodiscard]] std::unique_ptr<IAnimatedCharacter> create(
		[[maybe_unused]] const std::string& modelPath) override
	{
		auto character = std::make_unique<NullAnimatedCharacter>();
		character->loadModel(modelPath);
		return character;
	}

	[[nodiscard]] bool canLoad(
		[[maybe_unused]] const std::string& modelPath) const override
	{
		return true; // スタブは何でも受け入れる
	}
};

// ════════════════════════════════════════════════════════════════════
//  AnimatedCharacterManager。CharacterManager互換マネージャ
// ════════════════════════════════════════════════════════════════════

/// @brief アニメーションキャラクターマネージャ
/// @details CharacterManagerと同等のshow/hide/expression APIを提供しつつ、
///          IAnimatedCharacterを使ってアニメーション付きキャラクターを管理する。
///          アニメーションモデルが利用不可の場合は静的スプライトにフォールバックする。
class AnimatedCharacterManager
{
public:
	/// @brief リップシンク用コールバック（音声振幅 0.0-1.0 を返す）
	using LipSyncAmplitudeCallback = std::function<float()>;

	/// @brief コンストラクタ
	/// @param factory アニメーションキャラクターファクトリ（nullptrで全てフォールバック）
	explicit AnimatedCharacterManager(
		std::unique_ptr<IAnimatedCharacterFactory> factory = nullptr)
		: m_factory(std::move(factory))
	{
	}

	// ── キャラクター登録 ──────────────────────────────────────

	/// @brief アニメーションモデルパスを登録する
	/// @param characterId キャラクターID
	/// @param modelPath モデルファイルパス
	void registerModel(const std::string& characterId, const std::string& modelPath)
	{
		m_modelPaths[characterId] = modelPath;
	}

	/// @brief 静的スプライトのフォールバック用テクスチャIDを登録する
	/// @param characterId キャラクターID
	/// @param expression 表情名
	/// @param textureId テクスチャID
	void registerFallbackSprite(const std::string& characterId,
	                            const std::string& expression,
	                            std::uint32_t textureId)
	{
		m_fallbackSprites[characterId][expression] = textureId;
	}

	// ── 表示制御 ──────────────────────────────────────────────

	/// @brief キャラクターを表示する
	/// @param characterId キャラクターID
	/// @param position 表示位置
	/// @return 成功ならtrue
	bool show(const std::string& characterId, CharacterPosition position = CharacterPosition::Center)
	{
		auto& entry = getOrCreateEntry(characterId);
		entry.visible = true;
		entry.position = position;
		entry.alpha = 1.0f;
		return true;
	}

	/// @brief キャラクターを非表示にする
	/// @param characterId キャラクターID
	void hide(const std::string& characterId)
	{
		auto it = m_entries.find(characterId);
		if (it != m_entries.end())
		{
			it->second.visible = false;
			it->second.alpha = 0.0f;
		}
	}

	/// @brief 全キャラクターを非表示にする
	void hideAll()
	{
		for (auto& [id, entry] : m_entries)
		{
			entry.visible = false;
			entry.alpha = 0.0f;
		}
	}

	/// @brief 表情を変更する
	/// @param characterId キャラクターID
	/// @param expression 表情名
	void setExpression(const std::string& characterId, const std::string& expression)
	{
		auto& entry = getOrCreateEntry(characterId);
		entry.currentExpression = expression;

		if (entry.animatedCharacter)
		{
			entry.animatedCharacter->setExpression(expression);
		}
	}

	/// @brief モーションを再生する
	/// @param characterId キャラクターID
	/// @param motion モーション名
	/// @param loop ループ再生
	void setMotion(const std::string& characterId, const std::string& motion,
	               bool loop = false)
	{
		auto& entry = getOrCreateEntry(characterId);
		if (entry.animatedCharacter)
		{
			entry.animatedCharacter->setMotion(motion, loop);
		}
	}

	/// @brief パラメータを直接制御する
	/// @param characterId キャラクターID
	/// @param paramName パラメータ名
	/// @param value 値
	void setParameter(const std::string& characterId, const std::string& paramName,
	                  float value)
	{
		auto it = m_entries.find(characterId);
		if (it != m_entries.end() && it->second.animatedCharacter)
		{
			it->second.animatedCharacter->setParameter(paramName, value);
		}
	}

	// ── 設定 ──────────────────────────────────────────────────

	/// @brief アイトラッキング設定を変更する
	void setEyeTrackingConfig(const EyeTrackingConfig& config) noexcept
	{
		m_eyeTracking = config;
	}

	/// @brief リップシンク設定を変更する
	void setLipSyncConfig(const LipSyncConfig& config) noexcept
	{
		m_lipSync = config;
	}

	/// @brief アイドルモーション設定を変更する
	void setIdleMotionConfig(const IdleMotionConfig& config) noexcept
	{
		m_idleMotion = config;
	}

	/// @brief リップシンク用の音声振幅コールバックを設定する
	/// @param callback 音声振幅を返すコールバック
	void setLipSyncCallback(LipSyncAmplitudeCallback callback)
	{
		m_lipSyncCallback = std::move(callback);
	}

	// ── 更新 ──────────────────────────────────────────────────

	/// @brief 全キャラクターを更新する
	/// @param dt デルタタイム（秒）
	/// @param mouseX マウスX座標（アイトラッキング用、正規化 0.0-1.0）
	/// @param mouseY マウスY座標（アイトラッキング用、正規化 0.0-1.0）
	void update(float dt, float mouseX = 0.5f, float mouseY = 0.5f)
	{
		for (auto& [id, entry] : m_entries)
		{
			if (!entry.visible || !entry.animatedCharacter)
			{
				continue;
			}

			// アイトラッキング
			if (m_eyeTracking.enabled)
			{
				updateEyeTracking(entry, mouseX, mouseY, dt);
			}

			// 呼吸・アイドルモーション
			if (m_idleMotion.enabled)
			{
				updateIdleMotion(entry, dt);
			}

			// リップシンク
			if (m_lipSync.enabled && m_lipSyncCallback)
			{
				updateLipSync(entry);
			}

			// アニメーション更新
			entry.animatedCharacter->update(dt);
		}
	}

	// ── レンダリング ──────────────────────────────────────────

	/// @brief キャラクターをテクスチャとしてレンダリングする
	/// @param characterId キャラクターID
	/// @return レンダリングされたテクスチャ
	[[nodiscard]] render::Texture renderCharacter(const std::string& characterId) const
	{
		auto it = m_entries.find(characterId);
		if (it == m_entries.end() || !it->second.visible)
		{
			return {};
		}

		const auto& entry = it->second;

		// アニメーションキャラクターがあればそれを使う
		if (entry.animatedCharacter)
		{
			const auto pos = resolvePosition(entry.position);
			return entry.animatedCharacter->render(pos.first, pos.second, entry.scale);
		}

		// フォールバック：静的スプライト用のプレースホルダテクスチャ
		return render::Texture::solid(256, 512, 128, 128, 128, 200);
	}

	/// @brief ヒットテストを実行する
	/// @param characterId キャラクターID
	/// @param x ヒット位置X
	/// @param y ヒット位置Y
	/// @return ヒットテスト結果
	[[nodiscard]] HitTestResult hitTest(const std::string& characterId,
	                                    float x, float y) const
	{
		auto it = m_entries.find(characterId);
		if (it == m_entries.end() || !it->second.visible || !it->second.animatedCharacter)
		{
			return {false, ""};
		}
		return it->second.animatedCharacter->hitTest(x, y);
	}

	// ── 状態照会 ──────────────────────────────────────────────

	/// @brief キャラクターがアニメーション対応かチェックする
	/// @param characterId キャラクターID
	/// @return アニメーションモデル使用中ならtrue
	[[nodiscard]] bool isAnimated(const std::string& characterId) const
	{
		auto it = m_entries.find(characterId);
		return (it != m_entries.end() && it->second.animatedCharacter != nullptr);
	}

	/// @brief 表示中のキャラクターID一覧を取得する
	/// @return 表示中のキャラクターIDリスト
	[[nodiscard]] std::vector<std::string> visibleCharacters() const
	{
		std::vector<std::string> result;
		for (const auto& [id, entry] : m_entries)
		{
			if (entry.visible)
			{
				result.push_back(id);
			}
		}
		return result;
	}

	/// @brief キャラクター数を取得する
	[[nodiscard]] std::size_t size() const noexcept { return m_entries.size(); }

private:
	/// @brief キャラクターエントリ
	struct CharacterEntry
	{
		std::string id;
		std::unique_ptr<IAnimatedCharacter> animatedCharacter;
		bool visible = false;
		CharacterPosition position = CharacterPosition::Center;
		std::string currentExpression = "normal";
		float alpha = 1.0f;
		float scale = 1.0f;
		float breathTimer = 0.0f;							///< 呼吸タイマー
		float eyeTargetX = 0.0f;							///< 視線目標X
		float eyeTargetY = 0.0f;							///< 視線目標Y
		float eyeCurrentX = 0.0f;							///< 視線現在X
		float eyeCurrentY = 0.0f;							///< 視線現在Y
	};

	/// @brief エントリを取得または新規作成する
	CharacterEntry& getOrCreateEntry(const std::string& characterId)
	{
		auto it = m_entries.find(characterId);
		if (it != m_entries.end())
		{
			return it->second;
		}

		CharacterEntry entry;
		entry.id = characterId;

		// アニメーションモデルの読み込みを試行
		if (m_factory)
		{
			auto modelIt = m_modelPaths.find(characterId);
			if (modelIt != m_modelPaths.end() && m_factory->canLoad(modelIt->second))
			{
				entry.animatedCharacter = m_factory->create(modelIt->second);
			}
		}

		auto [insertIt, _] = m_entries.emplace(characterId, std::move(entry));
		return insertIt->second;
	}

	/// @brief アイトラッキングを更新する
	void updateEyeTracking(CharacterEntry& entry, float mouseX, float mouseY, float dt) const
	{
		if (!entry.animatedCharacter) { return; }

		// マウス位置からキャラクター位置への相対方向を計算
		const auto [charX, charY] = resolvePosition(entry.position);
		const float deltaX = mouseX - charX;
		const float deltaY = mouseY - charY;

		// 角度制限を適用してスムージング
		const float maxRange = m_eyeTracking.maxAngle / 90.0f; // 正規化
		entry.eyeTargetX = std::clamp(deltaX, -maxRange, maxRange);
		entry.eyeTargetY = std::clamp(deltaY, -maxRange, maxRange);

		const float lerpFactor = 1.0f - std::exp(-m_eyeTracking.speed * dt);
		entry.eyeCurrentX += (entry.eyeTargetX - entry.eyeCurrentX) * lerpFactor;
		entry.eyeCurrentY += (entry.eyeTargetY - entry.eyeCurrentY) * lerpFactor;

		entry.animatedCharacter->setParameter(m_eyeTracking.paramXName, entry.eyeCurrentX);
		entry.animatedCharacter->setParameter(m_eyeTracking.paramYName, entry.eyeCurrentY);
	}

	/// @brief アイドルモーション（呼吸）を更新する
	void updateIdleMotion(CharacterEntry& entry, float dt) const
	{
		if (!entry.animatedCharacter) { return; }

		entry.breathTimer += dt * m_idleMotion.breathingSpeed;
		const float breathValue = (std::sin(entry.breathTimer * 6.2831853f) + 1.0f) * 0.5f
			* m_idleMotion.breathingAmplitude;

		entry.animatedCharacter->setParameter(m_idleMotion.breathingParam, breathValue);
	}

	/// @brief リップシンクを更新する
	void updateLipSync(CharacterEntry& entry) const
	{
		if (!entry.animatedCharacter || !m_lipSyncCallback) { return; }

		float amplitude = m_lipSyncCallback();
		amplitude = std::clamp(amplitude * m_lipSync.sensitivity, 0.0f, 1.0f);

		// 現在値とスムージング
		auto currentVal = entry.animatedCharacter->getParameter(m_lipSync.parameterName);
		const float current = currentVal.value_or(0.0f);
		const float smoothed = current + (amplitude - current) * (1.0f - m_lipSync.smoothing);

		entry.animatedCharacter->setParameter(m_lipSync.parameterName, smoothed);
	}

	/// @brief CharacterPositionを正規化座標に変換する
	[[nodiscard]] static std::pair<float, float> resolvePosition(CharacterPosition pos)
	{
		switch (pos)
		{
		case CharacterPosition::Left:        return {0.15f, 0.8f};
		case CharacterPosition::CenterLeft:  return {0.30f, 0.8f};
		case CharacterPosition::Center:      return {0.50f, 0.8f};
		case CharacterPosition::CenterRight: return {0.70f, 0.8f};
		case CharacterPosition::Right:       return {0.85f, 0.8f};
		case CharacterPosition::Custom:      return {0.50f, 0.8f};
		}
		return {0.50f, 0.8f};
	}

	// ── メンバ ────────────────────────────────────────────────

	std::unique_ptr<IAnimatedCharacterFactory> m_factory;		///< キャラクターファクトリ
	std::unordered_map<std::string, CharacterEntry> m_entries;	///< キャラクターエントリ
	std::unordered_map<std::string, std::string> m_modelPaths;	///< モデルパス登録

	/// @brief フォールバック用静的スプライト（characterId → expression → textureId）
	std::unordered_map<std::string,
		std::unordered_map<std::string, std::uint32_t>> m_fallbackSprites;

	EyeTrackingConfig m_eyeTracking;							///< アイトラッキング設定
	LipSyncConfig m_lipSync;									///< リップシンク設定
	IdleMotionConfig m_idleMotion;								///< アイドルモーション設定
	LipSyncAmplitudeCallback m_lipSyncCallback;					///< リップシンクコールバック
};

} // namespace mitiru::vn
