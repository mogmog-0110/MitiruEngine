#pragma once

/// @file TilemapRenderer.hpp
/// @brief GPU加速タイルマップレンダラー
/// @details インスタンス描画によるタイルマップ描画パイプライン。
///          ビューポートカリング・パララックススクロール・
///          アニメーションタイル・タイル反転・オートタイルに対応する。
///          CPU描画パス（ソフトウェア/ヘッドレスモード用）も提供する。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <sgc/math/Rect.hpp>
#include <sgc/math/Vec2.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/render/Texture.hpp>
#include <mitiru/render/SpriteBatch.hpp>
#include <mitiru/core/Screen.hpp>

namespace mitiru::render
{

// ─── Tile Flip Flags ────────────────────────────────────────

/// @brief タイル反転フラグ（Tiledフォーマット互換）
enum class TileFlip : std::uint8_t
{
	None       = 0,
	Horizontal = 1 << 0,   ///< 水平反転
	Vertical   = 1 << 1,   ///< 垂直反転
	Diagonal   = 1 << 2    ///< 対角反転（90度回転相当）
};

/// @brief タイル反転フラグのビット論理和
[[nodiscard]] constexpr TileFlip operator|(TileFlip a, TileFlip b) noexcept
{
	return static_cast<TileFlip>(
		static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

/// @brief タイル反転フラグのビット論理積
[[nodiscard]] constexpr TileFlip operator&(TileFlip a, TileFlip b) noexcept
{
	return static_cast<TileFlip>(
		static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/// @brief タイル反転フラグが含まれるかテストする
[[nodiscard]] constexpr bool hasFlag(TileFlip flags, TileFlip test) noexcept
{
	return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(test)) != 0;
}

// ─── TilesetConfig ──────────────────────────────────────────

/// @brief タイルセット設定
/// @details テクスチャアトラス内のタイル配置パラメータを保持する。
struct TilesetConfig
{
	Texture texture;     ///< タイルセットテクスチャ（RGBA8）
	int tileWidth = 16;  ///< タイル幅（ピクセル）
	int tileHeight = 16; ///< タイル高さ（ピクセル）
	int columns = 1;     ///< テクスチャ内の列数
	int spacing = 0;     ///< タイル間のスペース（ピクセル）
	int margin = 0;      ///< テクスチャ端のマージン（ピクセル）
	int firstGid = 1;    ///< タイルIDオフセット（Tiled互換）

	/// @brief タイルIDからテクスチャ内のソース矩形を計算する
	/// @param tileId タイルID（firstGid起算）
	/// @return ソース矩形（ピクセル座標）
	[[nodiscard]] sgc::Rectf getTileRect(int tileId) const noexcept
	{
		if (columns <= 0 || tileId < 0)
		{
			return {0.0f, 0.0f, 0.0f, 0.0f};
		}
		const int localId = tileId;
		const int col = localId % columns;
		const int row = localId / columns;
		const float x = static_cast<float>(margin + col * (tileWidth + spacing));
		const float y = static_cast<float>(margin + row * (tileHeight + spacing));
		return {x, y,
		        static_cast<float>(tileWidth),
		        static_cast<float>(tileHeight)};
	}

	/// @brief タイルIDからUV矩形を計算する（テクスチャ正規化座標）
	/// @param tileId タイルID（firstGid起算）
	/// @return UV矩形 [0,1]
	[[nodiscard]] sgc::Rectf getTileUV(int tileId) const noexcept
	{
		const auto rect = getTileRect(tileId);
		const float texW = static_cast<float>(texture.width());
		const float texH = static_cast<float>(texture.height());
		if (texW <= 0.0f || texH <= 0.0f)
		{
			return {0.0f, 0.0f, 1.0f, 1.0f};
		}
		return {rect.x() / texW, rect.y() / texH,
		        rect.width() / texW, rect.height() / texH};
	}
};

// ─── AnimatedTile ───────────────────────────────────────────

/// @brief アニメーションタイル定義
/// @details 複数フレームを一定間隔で切り替えるタイルアニメーション。
struct AnimatedTile
{
	std::vector<int> tileIds;     ///< フレームごとのタイルID列
	float frameDuration = 0.2f;   ///< 1フレームの表示時間（秒）

	/// @brief 現在の経過時間に対応するタイルIDを取得する
	/// @param elapsedTime 経過時間（秒）
	/// @return 現在表示すべきタイルID
	[[nodiscard]] int currentTileId(float elapsedTime) const noexcept
	{
		if (tileIds.empty())
		{
			return 0;
		}
		if (frameDuration <= 0.0f)
		{
			return tileIds.front();
		}
		const float totalDuration = frameDuration * static_cast<float>(tileIds.size());
		float t = std::fmod(elapsedTime, totalDuration);
		if (t < 0.0f)
		{
			t += totalDuration;
		}
		const auto frameIndex = static_cast<std::size_t>(t / frameDuration);
		return tileIds[std::min(frameIndex, tileIds.size() - 1)];
	}
};

// ─── AutoTileRules ──────────────────────────────────────────

/// @brief オートタイルの隣接ビットマスク
/// @details 8方向の隣接タイルの有無をビットで表現する。
enum class AutoTileNeighbor : std::uint8_t
{
	None       = 0,
	Top        = 1 << 0,
	TopRight   = 1 << 1,
	Right      = 1 << 2,
	BottomRight= 1 << 3,
	Bottom     = 1 << 4,
	BottomLeft = 1 << 5,
	Left       = 1 << 6,
	TopLeft    = 1 << 7
};

[[nodiscard]] constexpr AutoTileNeighbor operator|(AutoTileNeighbor a, AutoTileNeighbor b) noexcept
{
	return static_cast<AutoTileNeighbor>(
		static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr AutoTileNeighbor operator&(AutoTileNeighbor a, AutoTileNeighbor b) noexcept
{
	return static_cast<AutoTileNeighbor>(
		static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

/// @brief オートタイルルール
/// @details 隣接ビットマスクからタイルIDへのマッピングテーブル。
struct AutoTileRules
{
	/// @brief ビットマスク→タイルIDマッピング
	std::unordered_map<std::uint8_t, int> maskToTileId;

	/// @brief デフォルトタイルID（マッチしない場合）
	int defaultTileId = 0;

	/// @brief ルールを追加する
	/// @param mask 隣接ビットマスク
	/// @param tileId 対応するタイルID
	void addRule(std::uint8_t mask, int tileId)
	{
		maskToTileId[mask] = tileId;
	}

	/// @brief 隣接マスクに対応するタイルIDを取得する
	/// @param mask 隣接ビットマスク
	/// @return タイルID
	[[nodiscard]] int resolve(std::uint8_t mask) const
	{
		const auto it = maskToTileId.find(mask);
		if (it != maskToTileId.end())
		{
			return it->second;
		}
		return defaultTileId;
	}
};

// ─── TilemapLayer ───────────────────────────────────────────

/// @brief タイルマップレイヤー
/// @details 1レイヤー分のタイルデータ（1次元配列）を保持する。
///          パララックススクロール・透明度・表示切替をサポートする。
struct TilemapLayer
{
	std::string name;              ///< レイヤー名
	int width = 0;                 ///< マップ幅（タイル数）
	int height = 0;                ///< マップ高さ（タイル数）
	std::vector<int> data;         ///< タイルIDデータ（row-major、0=空タイル）
	bool visible = true;           ///< 表示フラグ
	float opacity = 1.0f;          ///< 不透明度 [0,1]
	float offsetX = 0.0f;          ///< X方向オフセット（ピクセル）
	float offsetY = 0.0f;          ///< Y方向オフセット（ピクセル）
	float parallaxX = 1.0f;        ///< X方向パララックス係数（1.0=カメラ追従）
	float parallaxY = 1.0f;        ///< Y方向パララックス係数（1.0=カメラ追従）

	/// @brief タイルIDを取得する
	/// @param tileX X座標（タイル単位）
	/// @param tileY Y座標（タイル単位）
	/// @return タイルID（範囲外は0）
	[[nodiscard]] int getTile(int tileX, int tileY) const noexcept
	{
		if (tileX < 0 || tileX >= width || tileY < 0 || tileY >= height)
		{
			return 0;
		}
		const auto index = static_cast<std::size_t>(tileY) * width + tileX;
		if (index >= data.size())
		{
			return 0;
		}
		return data[index];
	}

	/// @brief タイルIDを設定する
	/// @param tileX X座標（タイル単位）
	/// @param tileY Y座標（タイル単位）
	/// @param tileId タイルID
	void setTile(int tileX, int tileY, int tileId) noexcept
	{
		if (tileX < 0 || tileX >= width || tileY < 0 || tileY >= height)
		{
			return;
		}
		const auto index = static_cast<std::size_t>(tileY) * width + tileX;
		if (index < data.size())
		{
			data[index] = tileId;
		}
	}

	/// @brief 指定座標のタイル反転フラグを取得する
	/// @param tileX X座標
	/// @param tileY Y座標
	/// @return 反転フラグ
	[[nodiscard]] TileFlip getFlip(int tileX, int tileY) const noexcept
	{
		if (tileX < 0 || tileX >= width || tileY < 0 || tileY >= height)
		{
			return TileFlip::None;
		}
		const auto index = static_cast<std::size_t>(tileY) * width + tileX;
		if (index >= m_flipData.size())
		{
			return TileFlip::None;
		}
		return m_flipData[index];
	}

	/// @brief 指定座標のタイル反転フラグを設定する
	/// @param tileX X座標
	/// @param tileY Y座標
	/// @param flip 反転フラグ
	void setFlip(int tileX, int tileY, TileFlip flip) noexcept
	{
		if (tileX < 0 || tileX >= width || tileY < 0 || tileY >= height)
		{
			return;
		}
		ensureFlipData();
		const auto index = static_cast<std::size_t>(tileY) * width + tileX;
		if (index < m_flipData.size())
		{
			m_flipData[index] = flip;
		}
	}

	/// @brief レイヤーサイズを初期化する（データをゼロクリア）
	/// @param w 幅（タイル数）
	/// @param h 高さ（タイル数）
	void resize(int w, int h)
	{
		width = w;
		height = h;
		data.assign(static_cast<std::size_t>(w) * h, 0);
		m_flipData.clear();
	}

private:
	std::vector<TileFlip> m_flipData; ///< タイルごとの反転フラグ（遅延確保）

	/// @brief 反転データ配列を確保する
	void ensureFlipData()
	{
		const auto size = static_cast<std::size_t>(width) * height;
		if (m_flipData.size() < size)
		{
			m_flipData.resize(size, TileFlip::None);
		}
	}
};

// ─── Tilemap ────────────────────────────────────────────────

/// @brief タイルマップ（複数レイヤー＋複数タイルセット）
struct Tilemap
{
	std::vector<TilemapLayer> layers;       ///< レイヤーリスト
	std::vector<TilesetConfig> tilesets;    ///< タイルセットリスト
	int mapWidth = 0;                       ///< マップ幅（タイル数）
	int mapHeight = 0;                      ///< マップ高さ（タイル数）

	/// @brief タイルIDに対応するタイルセットを取得する
	/// @param gid グローバルタイルID
	/// @return タイルセットへのポインタ（見つからなければnullptr）
	[[nodiscard]] const TilesetConfig* findTileset(int gid) const noexcept
	{
		const TilesetConfig* best = nullptr;
		for (const auto& ts : tilesets)
		{
			if (gid >= ts.firstGid)
			{
				if (!best || ts.firstGid > best->firstGid)
				{
					best = &ts;
				}
			}
		}
		return best;
	}
};

// ─── TilemapCollision ───────────────────────────────────────

/// @brief タイルマップ衝突判定
/// @details タイルベースのコリジョンレイヤーに対する
///          ソリッド判定・座標変換・矩形取得を提供する。
class TilemapCollision
{
public:
	/// @brief コンストラクタ
	/// @param collisionLayer 衝突判定に使用するレイヤー
	/// @param tileWidth タイル幅（ピクセル）
	/// @param tileHeight タイル高さ（ピクセル）
	/// @param solidTileIds ソリッドとみなすタイルIDの集合
	TilemapCollision(const TilemapLayer& collisionLayer,
	                 int tileWidth, int tileHeight,
	                 std::vector<int> solidTileIds = {})
		: m_layer(collisionLayer)
		, m_tileWidth(tileWidth)
		, m_tileHeight(tileHeight)
		, m_solidTileIds(std::move(solidTileIds))
	{
	}

	/// @brief 指定タイル座標がソリッドか判定する
	/// @param tileX タイルX座標
	/// @param tileY タイルY座標
	/// @return ソリッドならtrue
	[[nodiscard]] bool isSolid(int tileX, int tileY) const noexcept
	{
		const int tileId = m_layer.getTile(tileX, tileY);
		if (tileId <= 0)
		{
			return false;
		}

		/// ソリッドIDリストが空の場合、非ゼロタイルは全てソリッド
		if (m_solidTileIds.empty())
		{
			return true;
		}

		return std::find(m_solidTileIds.begin(), m_solidTileIds.end(), tileId)
		       != m_solidTileIds.end();
	}

	/// @brief ワールド座標からタイルIDを取得する
	/// @param worldX ワールドX座標（ピクセル）
	/// @param worldY ワールドY座標（ピクセル）
	/// @return タイルID（範囲外は0）
	[[nodiscard]] int getTileAt(float worldX, float worldY) const noexcept
	{
		const int tileX = static_cast<int>(std::floor(worldX / m_tileWidth));
		const int tileY = static_cast<int>(std::floor(worldY / m_tileHeight));
		return m_layer.getTile(tileX, tileY);
	}

	/// @brief 指定タイル座標のコリジョン矩形を取得する
	/// @param tileX タイルX座標
	/// @param tileY タイルY座標
	/// @return ワールド座標の矩形
	[[nodiscard]] sgc::Rectf getCollisionRect(int tileX, int tileY) const noexcept
	{
		return {
			static_cast<float>(tileX * m_tileWidth),
			static_cast<float>(tileY * m_tileHeight),
			static_cast<float>(m_tileWidth),
			static_cast<float>(m_tileHeight)
		};
	}

	/// @brief ワールド座標の矩形がソリッドタイルと重なるか判定する
	/// @param rect ワールド座標の矩形
	/// @return ソリッドタイルと重なるならtrue
	[[nodiscard]] bool overlapsAnySolid(const sgc::Rectf& rect) const noexcept
	{
		const int startX = static_cast<int>(std::floor(rect.x() / m_tileWidth));
		const int startY = static_cast<int>(std::floor(rect.y() / m_tileHeight));
		const int endX = static_cast<int>(std::floor((rect.x() + rect.width() - 0.001f) / m_tileWidth));
		const int endY = static_cast<int>(std::floor((rect.y() + rect.height() - 0.001f) / m_tileHeight));

		for (int ty = startY; ty <= endY; ++ty)
		{
			for (int tx = startX; tx <= endX; ++tx)
			{
				if (isSolid(tx, ty))
				{
					return true;
				}
			}
		}
		return false;
	}

	/// @brief コリジョンレイヤーへの参照を取得する
	[[nodiscard]] const TilemapLayer& layer() const noexcept { return m_layer; }

	/// @brief タイル幅を取得する
	[[nodiscard]] int tileWidth() const noexcept { return m_tileWidth; }

	/// @brief タイル高さを取得する
	[[nodiscard]] int tileHeight() const noexcept { return m_tileHeight; }

private:
	const TilemapLayer& m_layer;       ///< 参照するコリジョンレイヤー
	int m_tileWidth;                   ///< タイル幅（ピクセル）
	int m_tileHeight;                  ///< タイル高さ（ピクセル）
	std::vector<int> m_solidTileIds;   ///< ソリッドタイルID集合
};

// ─── TilemapRenderer ────────────────────────────────────────

/// @brief GPU加速タイルマップレンダラー
/// @details ビューポートカリング・パララックス・アニメーション対応の
///          タイルマップ描画エンジン。CPU描画パスとGPU描画パスの
///          両方を提供する。
///
/// @code
/// mitiru::render::TilemapRenderer renderer;
/// renderer.setAnimatedTile(3, {{3, 4, 5, 6}, 0.15f});
/// renderer.drawTilemap(screen, tilemap, cameraX, cameraY);
/// @endcode
class TilemapRenderer
{
public:
	/// @brief デフォルトコンストラクタ
	TilemapRenderer() noexcept = default;

	// ── アニメーション管理 ─────────────────────────────────────

	/// @brief アニメーションタイルを登録する
	/// @param baseTileId ベースタイルID
	/// @param anim アニメーション定義
	void setAnimatedTile(int baseTileId, AnimatedTile anim)
	{
		m_animatedTiles[baseTileId] = std::move(anim);
	}

	/// @brief アニメーションタイルを削除する
	/// @param baseTileId ベースタイルID
	void removeAnimatedTile(int baseTileId)
	{
		m_animatedTiles.erase(baseTileId);
	}

	/// @brief アニメーション時間を進める
	/// @param dt デルタタイム（秒）
	void updateAnimation(float dt) noexcept
	{
		m_elapsedTime += dt;
	}

	/// @brief アニメーション経過時間をリセットする
	void resetAnimation() noexcept
	{
		m_elapsedTime = 0.0f;
	}

	// ── オートタイル ───────────────────────────────────────────

	/// @brief オートタイルルールを設定する
	/// @param terrainTileId 対象タイルID
	/// @param rules オートタイルルール
	void setAutoTileRules(int terrainTileId, AutoTileRules rules)
	{
		m_autoTileRules[terrainTileId] = std::move(rules);
	}

	/// @brief レイヤーにオートタイルを適用する
	/// @param layer 対象レイヤー
	/// @param terrainTileId 地形タイルID
	void applyAutoTile(TilemapLayer& layer, int terrainTileId) const;

	// ── CPU描画パス ────────────────────────────────────────────

	/// @brief 単一レイヤーをCPU描画する（ビューポートカリング付き）
	/// @param screen 描画先サーフェス
	/// @param layer 描画対象レイヤー
	/// @param tileset タイルセット設定
	/// @param cameraX カメラX座標（ピクセル）
	/// @param cameraY カメラY座標（ピクセル）
	/// @param viewportW ビューポート幅（ピクセル）
	/// @param viewportH ビューポート高さ（ピクセル）
	void drawLayer(Screen& screen,
	               const TilemapLayer& layer,
	               const TilesetConfig& tileset,
	               float cameraX, float cameraY,
	               float viewportW, float viewportH) const;

	/// @brief タイルマップ全体をCPU描画する
	/// @param screen 描画先サーフェス
	/// @param tilemap タイルマップ
	/// @param cameraX カメラX座標（ピクセル）
	/// @param cameraY カメラY座標（ピクセル）
	void drawTilemap(Screen& screen,
	                 const Tilemap& tilemap,
	                 float cameraX, float cameraY) const;

	/// @brief タイルマップ全体をビューポート指定でCPU描画する
	/// @param screen 描画先サーフェス
	/// @param tilemap タイルマップ
	/// @param cameraX カメラX座標（ピクセル）
	/// @param cameraY カメラY座標（ピクセル）
	/// @param viewportW ビューポート幅
	/// @param viewportH ビューポート高さ
	void drawTilemap(Screen& screen,
	                 const Tilemap& tilemap,
	                 float cameraX, float cameraY,
	                 float viewportW, float viewportH) const;

	// ── 統計情報 ───────────────────────────────────────────────

	/// @brief 前回の描画で実際に描画されたタイル数を取得する
	[[nodiscard]] int lastDrawnTileCount() const noexcept
	{
		return m_lastDrawnTileCount;
	}

	/// @brief アニメーション経過時間を取得する
	[[nodiscard]] float elapsedTime() const noexcept
	{
		return m_elapsedTime;
	}

private:
	// ── 内部描画ヘルパー ───────────────────────────────────────

	/// @brief CPUパスで1タイルを描画する
	void drawTileCpu(Screen& screen,
	                 const TilesetConfig& tileset,
	                 int localTileId,
	                 float screenX, float screenY,
	                 TileFlip flip,
	                 const sgc::Colorf& tint) const;

	/// @brief テクスチャからタイルをCPU描画する
	/// @details テクスチャのピクセルデータを平均サンプリングして
	///          代表色で矩形描画する（CPUフォールバック）。
	void drawTileFromTexture(Screen& screen,
	                         const Texture& texture,
	                         const sgc::Rectf& srcRect,
	                         const sgc::Rectf& dstRect,
	                         TileFlip flip,
	                         const sgc::Colorf& tint) const;

	/// @brief Screen上に矩形を描画する
	static void drawRectOnScreen(Screen& screen,
	                              const sgc::Rectf& rect,
	                              const sgc::Colorf& color)
	{
		screen.drawRect(rect, color);
	}

	/// @brief Screenの幅を取得する
	static int getScreenWidth(const Screen& screen) noexcept
	{
		return screen.width();
	}

	/// @brief Screenの高さを取得する
	static int getScreenHeight(const Screen& screen) noexcept
	{
		return screen.height();
	}

	// ── オートタイルヘルパー ───────────────────────────────────

	/// @brief 隣接マスクを計算する
	/// @param layer 対象レイヤー
	/// @param x タイルX座標
	/// @param y タイルY座標
	/// @param terrainId 地形タイルID
	/// @return 8方向隣接ビットマスク
	[[nodiscard]] static std::uint8_t computeNeighborMask(
		const TilemapLayer& layer, int x, int y, int terrainId) noexcept;

	// ── アニメーションヘルパー ─────────────────────────────────

	/// @brief タイルIDをアニメーション置換する
	/// @param tileId 元のタイルID
	/// @return 置換後のタイルID
	[[nodiscard]] int resolveAnimatedTile(int tileId) const noexcept;

	// ── メンバ変数 ─────────────────────────────────────────────

	std::unordered_map<int, AnimatedTile> m_animatedTiles;  ///< アニメーションタイル
	std::unordered_map<int, AutoTileRules> m_autoTileRules; ///< オートタイルルール
	float m_elapsedTime = 0.0f;                             ///< アニメーション経過時間
	mutable int m_lastDrawnTileCount = 0;                   ///< 直前の描画タイル数
};

} // namespace mitiru::render

// 実装本体（core/Screen.hpp と同じ末尾 detail include 流儀）
#include <mitiru/render/detail/TilemapRenderer_impl.hpp>
