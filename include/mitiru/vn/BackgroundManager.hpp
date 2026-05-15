#pragma once

/// @file BackgroundManager.hpp
/// @brief ビジュアルノベル用背景マネージャ
/// @details 背景画像の表示・遷移・パン/ズーム（ケンバーンズ効果）
///          ・パララックス・レイヤー管理を提供する。

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mitiru/vn/EasingFunctions.hpp>
#include <mitiru/vn/TransitionEngine.hpp>

namespace mitiru::vn
{

/// @brief 背景レイヤーの種類
enum class BackgroundLayerType
{
	Far,        ///< 遠景（最背面）
	Main,       ///< メイン背景
	Overlay,    ///< オーバーレイ（前景）
	Foreground, ///< 前景エフェクト
};

/// @brief 背景カラー（画像未ロード時のフォールバック）
struct BackgroundColor
{
	float r{0.0f};
	float g{0.0f};
	float b{0.0f};
	float a{1.0f};

	[[nodiscard]] static constexpr BackgroundColor black() noexcept
	{
		return {0.0f, 0.0f, 0.0f, 1.0f};
	}

	[[nodiscard]] static constexpr BackgroundColor white() noexcept
	{
		return {1.0f, 1.0f, 1.0f, 1.0f};
	}
};

/// @brief パン/ズームアニメーションのキーフレーム
struct CameraKeyframe
{
	float x{0.0f};       ///< カメラ X 位置（正規化 0.0-1.0）
	float y{0.0f};       ///< カメラ Y 位置（正規化 0.0-1.0）
	float zoom{1.0f};    ///< ズーム倍率
};

/// @brief パン/ズームアニメーション（ケンバーンズ効果）
struct KenBurnsAnimation
{
	CameraKeyframe from;        ///< 開始キーフレーム
	CameraKeyframe to;          ///< 終了キーフレーム
	float duration{5.0f};       ///< アニメーション時間（秒）
	float elapsed{0.0f};        ///< 経過時間
	EasingType easing{EasingType::EaseInOutCubic};
	bool looping{false};        ///< ループ再生
	bool pingPong{false};       ///< 往復再生
	bool reverse{false};        ///< 逆再生中フラグ（pingPong用）

	/// @brief アニメーションを進行させる
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		elapsed += dt;

		if (looping && elapsed >= duration)
		{
			if (pingPong)
			{
				reverse = !reverse;
			}
			elapsed = std::fmod(elapsed, duration);
		}
		else
		{
			elapsed = std::min(elapsed, duration);
		}
	}

	/// @brief 進行度を取得する
	[[nodiscard]] float getProgress() const noexcept
	{
		if (duration <= 0.0f) return 1.0f;
		float raw = std::clamp(elapsed / duration, 0.0f, 1.0f);
		if (reverse)
		{
			raw = 1.0f - raw;
		}
		return Easing::apply(easing, raw);
	}

	/// @brief 現在のカメラ位置を取得する
	[[nodiscard]] CameraKeyframe getCurrent() const noexcept
	{
		const float t = getProgress();
		return {
			from.x + (to.x - from.x) * t,
			from.y + (to.y - from.y) * t,
			from.zoom + (to.zoom - from.zoom) * t,
		};
	}

	/// @brief 完了したか
	[[nodiscard]] bool isComplete() const noexcept
	{
		return !looping && elapsed >= duration;
	}
};

/// @brief 背景レイヤー
struct BackgroundLayer
{
	BackgroundLayerType type{BackgroundLayerType::Main};
	std::uint32_t textureId{0};    ///< テクスチャ識別子
	float alpha{1.0f};             ///< 透明度
	float parallaxSpeed{1.0f};     ///< パララックス速度係数
	float scrollX{0.0f};           ///< スクロール X オフセット
	float scrollY{0.0f};           ///< スクロール Y オフセット
	bool visible{true};            ///< 表示フラグ
	int zOrder{0};                 ///< 描画順（小さいほど奥）
};

/// @brief 背景描画情報
struct BackgroundRenderInfo
{
	/// @brief レイヤー描画情報
	struct LayerInfo
	{
		std::uint32_t textureId{0};
		float alpha{1.0f};
		float offsetX{0.0f};
		float offsetY{0.0f};
		float zoom{1.0f};
		float cameraX{0.0f};
		float cameraY{0.0f};
		int zOrder{0};
		bool visible{true};
	};

	std::vector<LayerInfo> layers;       ///< レイヤー一覧（描画順にソート済み）
	BackgroundColor fallbackColor;       ///< フォールバックカラー
	bool hasBackground{false};           ///< 背景が設定されているか

	/// @brief トランジション中の場合の旧背景テクスチャ
	std::uint32_t oldTextureId{0};
	float transitionProgress{1.0f};      ///< 遷移進行度（1.0=新背景のみ）
	bool inTransition{false};            ///< トランジション中か
};

/// @brief ビジュアルノベル用背景マネージャ
/// @details 背景画像のロード・遷移・カメラアニメーション・パララックスを統合管理する。
///
/// @code
/// mitiru::vn::BackgroundManager bg;
///
/// // 背景を設定
/// bg.setBackground(textureId);
///
/// // ケンバーンズ効果でパン
/// mitiru::vn::KenBurnsAnimation kenBurns;
/// kenBurns.from = {0.0f, 0.0f, 1.0f};
/// kenBurns.to = {0.3f, 0.2f, 1.5f};
/// kenBurns.duration = 5.0f;
/// bg.startKenBurns(kenBurns);
///
/// // 背景をトランジション付きで切り替え
/// auto effect = mitiru::vn::TransitionBuilder::dissolve(1.0f);
/// bg.transitionTo(newTextureId, std::move(effect));
///
/// // 毎フレーム
/// bg.update(dt);
/// auto info = bg.getRenderInfo();
/// @endcode
class BackgroundManager
{
public:
	// ── 背景設定 ──────────────────────────────────────────────

	/// @brief メイン背景をInstantで設定する
	/// @param textureId テクスチャ識別子
	void setBackground(std::uint32_t textureId) noexcept
	{
		m_mainLayer.textureId = textureId;
		m_mainLayer.visible = true;
		m_hasBackground = true;
	}

	/// @brief トランジション付きで背景を切り替える
	/// @param textureId 新しいテクスチャ識別子
	/// @param effect トランジションエフェクト
	void transitionTo(std::uint32_t textureId, TransitionEffect effect)
	{
		m_oldTextureId = m_mainLayer.textureId;
		m_mainLayer.textureId = textureId;
		m_mainLayer.visible = true;
		m_hasBackground = true;
		m_transition.startTransition(std::move(effect));
	}

	/// @brief 背景をクリアする
	void clearBackground() noexcept
	{
		m_mainLayer.textureId = 0;
		m_mainLayer.visible = false;
		m_hasBackground = false;
	}

	/// @brief フォールバックカラーを設定する
	/// @param color 背景色
	void setFallbackColor(BackgroundColor color) noexcept
	{
		m_fallbackColor = color;
	}

	// ── レイヤー管理 ──────────────────────────────────────────

	/// @brief レイヤーを追加する
	/// @param layer 背景レイヤー
	void addLayer(BackgroundLayer layer)
	{
		m_layers.push_back(std::move(layer));
		sortLayers();
	}

	/// @brief レイヤーを削除する
	/// @param zOrder 削除するレイヤーのZ順序
	void removeLayer(int zOrder) noexcept
	{
		m_layers.erase(
			std::remove_if(m_layers.begin(), m_layers.end(),
				[zOrder](const BackgroundLayer& layer)
				{
					return layer.zOrder == zOrder;
				}),
			m_layers.end());
	}

	/// @brief 全レイヤーをクリアする
	void clearLayers() noexcept
	{
		m_layers.clear();
	}

	/// @brief レイヤー数を取得する
	[[nodiscard]] std::size_t layerCount() const noexcept
	{
		return m_layers.size();
	}

	// ── パン/ズーム（ケンバーンズ効果）────────────────────────

	/// @brief ケンバーンズアニメーションを開始する
	/// @param animation アニメーション設定
	void startKenBurns(KenBurnsAnimation animation) noexcept
	{
		m_kenBurns = std::move(animation);
		m_kenBurns->elapsed = 0.0f;
	}

	/// @brief ケンバーンズアニメーションを停止する
	void stopKenBurns() noexcept
	{
		m_kenBurns.reset();
	}

	/// @brief ケンバーンズアニメーション中か
	[[nodiscard]] bool isKenBurnsActive() const noexcept
	{
		return m_kenBurns.has_value() && !m_kenBurns->isComplete();
	}

	/// @brief カメラ位置を直接設定する
	/// @param x X 位置（正規化）
	/// @param y Y 位置（正規化）
	/// @param zoom ズーム倍率
	void setCamera(float x, float y, float zoom = 1.0f) noexcept
	{
		m_cameraX = x;
		m_cameraY = y;
		m_cameraZoom = zoom;
	}

	// ── パララックス ──────────────────────────────────────────

	/// @brief パララックススクロールを更新する
	/// @param dx X 方向のスクロール量
	/// @param dy Y 方向のスクロール量
	void scrollParallax(float dx, float dy) noexcept
	{
		for (auto& layer : m_layers)
		{
			layer.scrollX += dx * layer.parallaxSpeed;
			layer.scrollY += dy * layer.parallaxSpeed;
		}
	}

	// ── 更新 ──────────────────────────────────────────────────

	/// @brief 全アニメーションを更新する
	/// @param dt デルタタイム（秒）
	void update(float dt) noexcept
	{
		/// トランジション更新
		m_transition.update(dt);

		/// ケンバーンズ更新
		if (m_kenBurns.has_value())
		{
			m_kenBurns->update(dt);
			const auto current = m_kenBurns->getCurrent();
			m_cameraX = current.x;
			m_cameraY = current.y;
			m_cameraZoom = current.zoom;

			if (m_kenBurns->isComplete())
			{
				m_kenBurns.reset();
			}
		}
	}

	// ── 描画情報取得 ──────────────────────────────────────────

	/// @brief 描画情報を取得する
	/// @return BackgroundRenderInfo
	[[nodiscard]] BackgroundRenderInfo getRenderInfo() const noexcept
	{
		BackgroundRenderInfo info;
		info.fallbackColor = m_fallbackColor;
		info.hasBackground = m_hasBackground;

		/// トランジション状態
		info.inTransition = m_transition.isTransitioning();
		info.oldTextureId = m_oldTextureId;
		info.transitionProgress = m_transition.progress();

		/// 追加レイヤー
		for (const auto& layer : m_layers)
		{
			if (!layer.visible)
			{
				continue;
			}

			BackgroundRenderInfo::LayerInfo layerInfo;
			layerInfo.textureId = layer.textureId;
			layerInfo.alpha = layer.alpha;
			layerInfo.offsetX = layer.scrollX;
			layerInfo.offsetY = layer.scrollY;
			layerInfo.zoom = m_cameraZoom;
			layerInfo.cameraX = m_cameraX * layer.parallaxSpeed;
			layerInfo.cameraY = m_cameraY * layer.parallaxSpeed;
			layerInfo.zOrder = layer.zOrder;
			layerInfo.visible = true;
			info.layers.push_back(layerInfo);
		}

		/// メインレイヤーを追加
		if (m_mainLayer.visible)
		{
			BackgroundRenderInfo::LayerInfo mainInfo;
			mainInfo.textureId = m_mainLayer.textureId;
			mainInfo.alpha = m_mainLayer.alpha;
			mainInfo.offsetX = 0.0f;
			mainInfo.offsetY = 0.0f;
			mainInfo.zoom = m_cameraZoom;
			mainInfo.cameraX = m_cameraX;
			mainInfo.cameraY = m_cameraY;
			mainInfo.zOrder = 0;
			mainInfo.visible = true;
			info.layers.push_back(mainInfo);
		}

		/// Z 順序でソート
		std::sort(info.layers.begin(), info.layers.end(),
			[](const BackgroundRenderInfo::LayerInfo& a,
			   const BackgroundRenderInfo::LayerInfo& b)
			{
				return a.zOrder < b.zOrder;
			});

		return info;
	}

	// ── シリアライズ ──────────────────────────────────────────

	/// @brief 背景状態をJSON文字列として返す
	[[nodiscard]] std::string toJson() const
	{
		std::string json;
		json += "{";
		json += "\"hasBackground\":" + std::string(m_hasBackground ? "true" : "false");
		json += ",\"mainTextureId\":" + std::to_string(m_mainLayer.textureId);
		json += ",\"cameraX\":" + std::to_string(m_cameraX);
		json += ",\"cameraY\":" + std::to_string(m_cameraY);
		json += ",\"cameraZoom\":" + std::to_string(m_cameraZoom);
		json += ",\"layerCount\":" + std::to_string(m_layers.size());
		json += ",\"inTransition\":" + std::string(m_transition.isTransitioning() ? "true" : "false");
		json += ",\"kenBurnsActive\":" + std::string(isKenBurnsActive() ? "true" : "false");

		json += ",\"fallbackColor\":{";
		json += "\"r\":" + std::to_string(m_fallbackColor.r);
		json += ",\"g\":" + std::to_string(m_fallbackColor.g);
		json += ",\"b\":" + std::to_string(m_fallbackColor.b);
		json += ",\"a\":" + std::to_string(m_fallbackColor.a);
		json += "}";

		json += ",\"layers\":[";
		bool first = true;
		for (const auto& layer : m_layers)
		{
			if (!first) json += ",";
			json += "{\"type\":" + std::to_string(static_cast<int>(layer.type));
			json += ",\"textureId\":" + std::to_string(layer.textureId);
			json += ",\"alpha\":" + std::to_string(layer.alpha);
			json += ",\"parallaxSpeed\":" + std::to_string(layer.parallaxSpeed);
			json += ",\"zOrder\":" + std::to_string(layer.zOrder);
			json += ",\"visible\":" + std::string(layer.visible ? "true" : "false");
			json += "}";
			first = false;
		}
		json += "]";

		json += "}";
		return json;
	}

private:
	/// @brief レイヤーをZ順序でソートする
	void sortLayers() noexcept
	{
		std::sort(m_layers.begin(), m_layers.end(),
			[](const BackgroundLayer& a, const BackgroundLayer& b)
			{
				return a.zOrder < b.zOrder;
			});
	}

	BackgroundLayer m_mainLayer;                    ///< メイン背景レイヤー
	std::vector<BackgroundLayer> m_layers;           ///< 追加レイヤー
	BackgroundColor m_fallbackColor{BackgroundColor::black()}; ///< フォールバック色
	bool m_hasBackground{false};                     ///< 背景が設定されているか

	/// カメラ状態
	float m_cameraX{0.0f};
	float m_cameraY{0.0f};
	float m_cameraZoom{1.0f};

	/// ケンバーンズアニメーション
	std::optional<KenBurnsAnimation> m_kenBurns;

	/// トランジション
	TransitionManager m_transition;
	std::uint32_t m_oldTextureId{0};
};

} // namespace mitiru::vn
