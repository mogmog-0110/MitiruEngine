#pragma once

/// @file VNTextureManager.hpp
/// @brief VN UIスキンのテクスチャ管理
/// @details UISkinLoaderが保持する画像パスを実際のTextureオブジェクトに変換し、
///          キャッシュ管理する。UIスキンの全画像参照を一括読み込みし、
///          キーベースで取得可能にする。
///
/// @code
/// mitiru::vn::VNTextureManager texMgr("assets/vn/");
/// texMgr.loadFromSkin(skin);
///
/// if (texMgr.hasTexture("ui/window.png")) {
///     const auto& tex = texMgr.getTexture("ui/window.png");
///     screen.drawSprite(tex, dstRect);
/// }
///
/// // 便利メソッド
/// if (auto* tex = texMgr.messageWindowTexture()) {
///     screen.drawSprite(*tex, windowRect);
/// }
/// @endcode

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <mitiru/render/ImageLoader.hpp>
#include <mitiru/render/Texture.hpp>
#include <mitiru/vn/UISkinLoader.hpp>

namespace mitiru::vn
{

/// @brief テクスチャロード結果
struct TextureLoadResult
{
	int loaded = 0;          ///< 新規ロード数
	int cached = 0;          ///< キャッシュヒット数
	int failed = 0;          ///< 失敗数
	std::vector<std::string> failedPaths; ///< 失敗したパス一覧
};

/// @brief 選択肢ボタンの状態
enum class ChoiceButtonState : std::uint8_t
{
	Normal,
	Hover,
	Selected,
	Disabled
};

/// @brief VN UIスキンのテクスチャマネージャ
/// @details UISkinの画像パスからTextureを読み込み、キャッシュする。
///          ベースアセットパスとスキン内の相対パスを結合してファイルを読み込む。
class VNTextureManager
{
	std::string m_basePath;
	std::unordered_map<std::string, render::Texture> m_cache;

	// スキンから取得したパスへの参照（便利メソッド用）
	std::string m_messageWindowImagePath;
	std::string m_namePlateImagePath;
	std::string m_waitIconImagePath;
	std::string m_choiceNormalImagePath;
	std::string m_choiceHoverImagePath;
	std::string m_choiceSelectedImagePath;
	std::string m_choiceDisabledImagePath;
	std::string m_sliderTrackImagePath;
	std::string m_sliderFillImagePath;
	std::string m_sliderHandleImagePath;
	std::string m_scrollTrackImagePath;
	std::string m_scrollThumbImagePath;

public:
	/// @brief ベースアセットパスを指定して構築する
	/// @param basePath アセットのベースディレクトリ（末尾スラッシュ推奨）
	explicit VNTextureManager(std::string basePath = "")
		: m_basePath(std::move(basePath))
	{
		normalizeBasePath();
	}

	/// @brief ベースアセットパスを取得する
	[[nodiscard]] const std::string& basePath() const noexcept { return m_basePath; }

	/// @brief ベースアセットパスを変更する
	/// @param path 新しいベースパス
	void setBasePath(std::string path)
	{
		m_basePath = std::move(path);
		normalizeBasePath();
	}

	// ── スキン一括読み込み ─────────────────────────────────────

	/// @brief UISkinが参照する全画像をロードする
	/// @param skin ロード対象のスキン定義
	/// @return ロード結果
	TextureLoadResult loadFromSkin(const UISkin& skin)
	{
		TextureLoadResult result;

		// パスの記録をリセット
		m_messageWindowImagePath.clear();
		m_namePlateImagePath.clear();
		m_waitIconImagePath.clear();
		m_choiceNormalImagePath.clear();
		m_choiceHoverImagePath.clear();
		m_choiceSelectedImagePath.clear();
		m_choiceDisabledImagePath.clear();
		m_sliderTrackImagePath.clear();
		m_sliderFillImagePath.clear();
		m_sliderHandleImagePath.clear();
		m_scrollTrackImagePath.clear();
		m_scrollThumbImagePath.clear();

		// メッセージウィンドウ背景
		if (!skin.messageWindow.backgroundImage.empty())
		{
			m_messageWindowImagePath = skin.messageWindow.backgroundImage;
			loadAndRecord(m_messageWindowImagePath, result);
		}

		// ネームプレート
		if (!skin.messageWindow.namePlate.image.empty())
		{
			m_namePlateImagePath = skin.messageWindow.namePlate.image;
			loadAndRecord(m_namePlateImagePath, result);
		}

		// 待機アイコン
		if (!skin.messageWindow.waitIcon.image.empty())
		{
			m_waitIconImagePath = skin.messageWindow.waitIcon.image;
			loadAndRecord(m_waitIconImagePath, result);
		}

		// 選択肢ボタン（各状態）
		if (!skin.choiceButton.normal.backgroundImage.empty())
		{
			m_choiceNormalImagePath = skin.choiceButton.normal.backgroundImage;
			loadAndRecord(m_choiceNormalImagePath, result);
		}
		if (!skin.choiceButton.hover.backgroundImage.empty())
		{
			m_choiceHoverImagePath = skin.choiceButton.hover.backgroundImage;
			loadAndRecord(m_choiceHoverImagePath, result);
		}
		if (!skin.choiceButton.selected.backgroundImage.empty())
		{
			m_choiceSelectedImagePath = skin.choiceButton.selected.backgroundImage;
			loadAndRecord(m_choiceSelectedImagePath, result);
		}
		if (!skin.choiceButton.disabled.backgroundImage.empty())
		{
			m_choiceDisabledImagePath = skin.choiceButton.disabled.backgroundImage;
			loadAndRecord(m_choiceDisabledImagePath, result);
		}

		// セーブスロット（normal/hover）
		if (!skin.saveSlot.normal.backgroundImage.empty())
		{
			loadAndRecord(skin.saveSlot.normal.backgroundImage, result);
		}
		if (!skin.saveSlot.hover.backgroundImage.empty())
		{
			loadAndRecord(skin.saveSlot.hover.backgroundImage, result);
		}

		// コンフィグスライダー
		if (!skin.configSlider.trackImage.empty())
		{
			m_sliderTrackImagePath = skin.configSlider.trackImage;
			loadAndRecord(m_sliderTrackImagePath, result);
		}
		if (!skin.configSlider.fillImage.empty())
		{
			m_sliderFillImagePath = skin.configSlider.fillImage;
			loadAndRecord(m_sliderFillImagePath, result);
		}
		if (!skin.configSlider.handleImage.empty())
		{
			m_sliderHandleImagePath = skin.configSlider.handleImage;
			loadAndRecord(m_sliderHandleImagePath, result);
		}

		// スクロールバー
		if (!skin.scrollBar.trackImage.empty())
		{
			m_scrollTrackImagePath = skin.scrollBar.trackImage;
			loadAndRecord(m_scrollTrackImagePath, result);
		}
		if (!skin.scrollBar.thumbImage.empty())
		{
			m_scrollThumbImagePath = skin.scrollBar.thumbImage;
			loadAndRecord(m_scrollThumbImagePath, result);
		}

		return result;
	}

	// ── 個別テクスチャ操作 ─────────────────────────────────────

	/// @brief 個別のテクスチャをロードする
	/// @param path 画像パス（ベースパスからの相対またはスキン内パス）
	/// @return ロード成功ならtrue
	bool loadTexture(const std::string& path)
	{
		if (path.empty()) { return false; }
		if (m_cache.count(path) > 0) { return true; }

		const std::string fullPath = m_basePath + path;
		auto texture = render::ImageLoader::fromFile(fullPath);
		if (!texture.valid()) { return false; }

		m_cache.emplace(path, std::move(texture));
		return true;
	}

	/// @brief 指定パスのテクスチャがロード済みか確認する
	/// @param path 画像パス
	/// @return ロード済みならtrue
	[[nodiscard]] bool hasTexture(const std::string& path) const
	{
		return m_cache.count(path) > 0;
	}

	/// @brief ロード済みテクスチャを取得する
	/// @param path 画像パス
	/// @return テクスチャへの参照
	/// @throws std::out_of_range テクスチャが見つからない場合
	[[nodiscard]] const render::Texture& getTexture(const std::string& path) const
	{
		return m_cache.at(path);
	}

	/// @brief ロード済みテクスチャを取得する（見つからない場合はnullptr）
	/// @param path 画像パス
	/// @return テクスチャへのポインタ（見つからない場合nullptr）
	[[nodiscard]] const render::Texture* tryGetTexture(const std::string& path) const
	{
		auto it = m_cache.find(path);
		if (it == m_cache.end()) { return nullptr; }
		return &it->second;
	}

	/// @brief キャッシュからテクスチャを削除する
	/// @param path 削除対象のパス
	/// @return 削除された場合true
	bool unload(const std::string& path)
	{
		return m_cache.erase(path) > 0;
	}

	/// @brief 全キャッシュをクリアする
	void clearAll()
	{
		m_cache.clear();
	}

	/// @brief キャッシュ内のテクスチャ数を取得する
	[[nodiscard]] std::size_t cacheSize() const noexcept
	{
		return m_cache.size();
	}

	// ── 便利メソッド ───────────────────────────────────────────

	/// @brief メッセージウィンドウ背景テクスチャを取得する
	/// @return テクスチャへのポインタ（未設定/未ロード時はnullptr）
	[[nodiscard]] const render::Texture* messageWindowTexture() const
	{
		return tryGetTexture(m_messageWindowImagePath);
	}

	/// @brief ネームプレートテクスチャを取得する
	/// @return テクスチャへのポインタ（未設定/未ロード時はnullptr）
	[[nodiscard]] const render::Texture* namePlateTexture() const
	{
		return tryGetTexture(m_namePlateImagePath);
	}

	/// @brief 待機アイコンテクスチャを取得する
	/// @return テクスチャへのポインタ（未設定/未ロード時はnullptr）
	[[nodiscard]] const render::Texture* waitIconTexture() const
	{
		return tryGetTexture(m_waitIconImagePath);
	}

	/// @brief 選択肢ボタンテクスチャを状態別に取得する
	/// @param state ボタンの状態
	/// @return テクスチャへのポインタ（未設定/未ロード時はnullptr）
	[[nodiscard]] const render::Texture* choiceButtonTexture(ChoiceButtonState state) const
	{
		switch (state)
		{
		case ChoiceButtonState::Normal:   return tryGetTexture(m_choiceNormalImagePath);
		case ChoiceButtonState::Hover:    return tryGetTexture(m_choiceHoverImagePath);
		case ChoiceButtonState::Selected: return tryGetTexture(m_choiceSelectedImagePath);
		case ChoiceButtonState::Disabled: return tryGetTexture(m_choiceDisabledImagePath);
		}
		return nullptr;
	}

	/// @brief スキン内の全画像パスを列挙する
	/// @return キャッシュに存在する全パスのリスト
	[[nodiscard]] std::vector<std::string> loadedPaths() const
	{
		std::vector<std::string> paths;
		paths.reserve(m_cache.size());
		for (const auto& [key, _] : m_cache)
		{
			paths.push_back(key);
		}
		return paths;
	}

	/// @brief テクスチャを直接登録する（テスト用・プロシージャル生成用）
	/// @param path キーとなるパス
	/// @param texture 登録するテクスチャ
	void registerTexture(const std::string& path, render::Texture texture)
	{
		m_cache.insert_or_assign(path, std::move(texture));
	}

private:
	void normalizeBasePath()
	{
		if (!m_basePath.empty() && m_basePath.back() != '/' && m_basePath.back() != '\\')
		{
			m_basePath += '/';
		}
	}

	void loadAndRecord(const std::string& path, TextureLoadResult& result)
	{
		if (path.empty()) { return; }

		if (m_cache.count(path) > 0)
		{
			++result.cached;
			return;
		}

		const std::string fullPath = m_basePath + path;
		auto texture = render::ImageLoader::fromFile(fullPath);
		if (texture.valid())
		{
			m_cache.emplace(path, std::move(texture));
			++result.loaded;
		}
		else
		{
			result.failedPaths.push_back(path);
			++result.failed;
		}
	}
};

} // namespace mitiru::vn
