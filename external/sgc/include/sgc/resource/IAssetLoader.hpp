#pragma once

/// @file IAssetLoader.hpp
/// @brief 型付きアセットローダーインターフェースとコンポジットローダー
///
/// 拡張子に基づくアセットロードの抽象化と、
/// 複数ローダーを連結するCompositeAssetLoaderを提供する。
///
/// @code
/// struct TextureLoader : sgc::ITypedAssetLoader<Texture>
/// {
///     sgc::Result<Texture> load(const std::string& path) override
///     {
///         return Texture::fromFile(path);
///     }
///     bool canLoad(const std::string& ext) const override { return ext == ".png"; }
///     std::string loaderName() const override { return "TextureLoader"; }
/// };
///
/// sgc::CompositeAssetLoader<Texture> composite;
/// composite.addLoader(std::make_unique<TextureLoader>());
/// auto result = composite.load("player.png");
/// @endcode

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "sgc/types/Result.hpp"

namespace sgc
{

/// @brief 型付きアセットローダーインターフェース
/// @tparam T ロードするアセットの型
///
/// 拡張子判定によるロード可否チェックと、
/// パスからアセットをロードする機能を提供する。
template <typename T>
class ITypedAssetLoader
{
public:
	/// @brief 仮想デストラクタ
	virtual ~ITypedAssetLoader() = default;

	/// @brief 指定パスからアセットをロードする
	/// @param path ファイルパス
	/// @return ロード結果（成功時はアセット、失敗時はエラー）
	[[nodiscard]] virtual Result<T> load(const std::string& path) = 0;

	/// @brief 指定拡張子のファイルをロード可能か判定する
	/// @param extension ファイル拡張子（ドット付き、例: ".png"）
	/// @return ロード可能ならtrue
	[[nodiscard]] virtual bool canLoad(const std::string& extension) const = 0;

	/// @brief ローダー名を取得する
	/// @return ローダーの識別名
	[[nodiscard]] virtual std::string loaderName() const = 0;
};

/// @brief 複数ローダーを連結するコンポジットアセットローダー
/// @tparam T ロードするアセットの型
///
/// 登録されたローダーを順番に試行し、最初に成功したローダーの結果を返す。
/// 拡張子によるフィルタリングを行い、対応可能なローダーのみを試行する。
template <typename T>
class CompositeAssetLoader final : public ITypedAssetLoader<T>
{
public:
	/// @brief ローダーを追加する
	/// @param loader 追加するローダー（所有権を移譲）
	void addLoader(std::unique_ptr<ITypedAssetLoader<T>> loader)
	{
		if (loader)
		{
			m_loaders.push_back(std::move(loader));
		}
	}

	/// @brief 指定パスからアセットをロードする
	/// @param path ファイルパス
	/// @return ロード結果
	///
	/// 登録済みローダーのうち、拡張子に対応するものを順に試行する。
	/// いずれかが成功すればその結果を返す。全て失敗した場合はエラー。
	[[nodiscard]] Result<T> load(const std::string& path) override
	{
		const std::string ext = extractExtension(path);

		for (auto& loader : m_loaders)
		{
			if (loader->canLoad(ext))
			{
				auto result = loader->load(path);
				if (result) return result;
			}
		}

		return {ERROR_TAG, Error{"No loader found for: " + path}};
	}

	/// @brief 指定拡張子のファイルをロード可能か判定する
	/// @param extension ファイル拡張子
	/// @return いずれかのローダーが対応していればtrue
	[[nodiscard]] bool canLoad(const std::string& extension) const override
	{
		return std::any_of(m_loaders.begin(), m_loaders.end(),
			[&extension](const auto& loader) { return loader->canLoad(extension); });
	}

	/// @brief ローダー名を取得する
	/// @return "CompositeAssetLoader"
	[[nodiscard]] std::string loaderName() const override
	{
		return "CompositeAssetLoader";
	}

	/// @brief 登録されたローダー数を取得する
	/// @return ローダー数
	[[nodiscard]] std::size_t loaderCount() const noexcept
	{
		return m_loaders.size();
	}

private:
	std::vector<std::unique_ptr<ITypedAssetLoader<T>>> m_loaders;  ///< 登録済みローダー

	/// @brief ファイルパスから拡張子を取得する
	/// @param path ファイルパス
	/// @return 拡張子（ドット付き）。拡張子がなければ空文字列
	[[nodiscard]] static std::string extractExtension(const std::string& path)
	{
		const auto pos = path.rfind('.');
		if (pos == std::string::npos) return {};
		return path.substr(pos);
	}
};

// ── 型エイリアス ─────────────────────────────────────────

/// @brief int型アセットローダー（テスト用）
using IntAssetLoader = ITypedAssetLoader<int>;

/// @brief string型アセットローダー（テスト用）
using StringAssetLoader = ITypedAssetLoader<std::string>;

} // namespace sgc
