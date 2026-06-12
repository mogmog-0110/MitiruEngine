#pragma once

/// @file AssetPipeline.hpp
/// @brief アセット処理パイプライン
/// @details アセットの加工（リサイズ、圧縮、ミップマップ生成、アトラス生成）、
///          マニフェスト管理、バンドルパッキングを提供する。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mitiru::resource
{

// ---------------------------------------------------------------------------
// AssetProcessor インターフェース
// ---------------------------------------------------------------------------

/// @brief アセット処理の抽象インターフェース
/// @details 入力ファイルを加工して出力ファイルを生成する。
class AssetProcessor
{
public:
	virtual ~AssetProcessor() = default;

	/// @brief アセットを処理する
	/// @param inputPath 入力ファイルパス
	/// @param outputPath 出力ファイルパス
	/// @return 処理に成功した場合 true
	[[nodiscard]] virtual bool process(const std::string& inputPath,
									   const std::string& outputPath) = 0;

	/// @brief プロセッサー名を取得する
	[[nodiscard]] virtual std::string name() const = 0;
};

// ---------------------------------------------------------------------------
// テクスチャ処理
// ---------------------------------------------------------------------------

/// @brief テクスチャデータ（簡易表現）
struct TextureData
{
	std::uint32_t width = 0;              ///< 幅（ピクセル）
	std::uint32_t height = 0;             ///< 高さ（ピクセル）
	std::uint32_t channels = 4;           ///< チャンネル数（RGBA=4）
	std::vector<std::uint8_t> pixels;     ///< ピクセルデータ
};

/// @brief ミップマップレベル
struct MipLevel
{
	std::uint32_t width = 0;              ///< 幅
	std::uint32_t height = 0;             ///< 高さ
	std::vector<std::uint8_t> pixels;     ///< ピクセルデータ
};

/// @brief アトラス内のスプライト配置情報
struct AtlasEntry
{
	std::string name;                     ///< スプライト名
	std::uint32_t x = 0;                  ///< アトラス内X座標
	std::uint32_t y = 0;                  ///< アトラス内Y座標
	std::uint32_t width = 0;              ///< 幅
	std::uint32_t height = 0;             ///< 高さ
};

/// @brief テクスチャアトラス
struct TextureAtlas
{
	TextureData image;                    ///< アトラス画像
	std::vector<AtlasEntry> entries;      ///< スプライト配置リスト
	std::uint32_t atlasWidth = 0;         ///< アトラス幅
	std::uint32_t atlasHeight = 0;        ///< アトラス高さ
};

/// @brief テクスチャをリサイズする（ニアレストネイバー）
/// @param input 入力テクスチャ
/// @param maxWidth 最大幅
/// @param maxHeight 最大高さ
/// @return リサイズされたテクスチャ
[[nodiscard]] inline TextureData resizeImage(const TextureData& input,
											 std::uint32_t maxWidth,
											 std::uint32_t maxHeight);

/// @brief ミップマップチェインを生成する
/// @param source 元テクスチャ
/// @return ミップマップレベルのリスト（レベル0 = 元画像の半分）
[[nodiscard]] inline std::vector<MipLevel> generateMipmaps(const TextureData& source);

/// @brief テクスチャプロセッサー
/// @details テクスチャのリサイズ・圧縮・ミップマップ生成を行う。
///          実際のファイルI/Oは外部で行い、ここではデータ処理のみ。
class TextureProcessor : public AssetProcessor
{
public:
	/// @param maxWidth 最大出力幅
	/// @param maxHeight 最大出力高さ
	/// @param generateMips ミップマップを生成するか
	explicit TextureProcessor(std::uint32_t maxWidth = 2048,
							  std::uint32_t maxHeight = 2048,
							  bool generateMips = true)
		: m_maxWidth(maxWidth)
		, m_maxHeight(maxHeight)
		, m_generateMips(generateMips)
	{
	}

	[[nodiscard]] bool process(const std::string& inputPath,
							   const std::string& outputPath) override
	{
		/// 実際のI/Oはエンジンのイメージローダーに依存するため、
		/// ここではパスの妥当性のみチェック
		if (inputPath.empty() || outputPath.empty())
		{
			return false;
		}
		m_lastInputPath = inputPath;
		m_lastOutputPath = outputPath;
		return true;
	}

	[[nodiscard]] std::string name() const override { return "TextureProcessor"; }

	/// @brief テクスチャデータを加工する
	/// @param input 入力テクスチャ
	/// @return リサイズ済みテクスチャ
	[[nodiscard]] TextureData processTexture(const TextureData& input) const
	{
		return resizeImage(input, m_maxWidth, m_maxHeight);
	}

	/// @brief 設定: 最大幅
	[[nodiscard]] std::uint32_t maxWidth() const noexcept { return m_maxWidth; }

	/// @brief 設定: 最大高さ
	[[nodiscard]] std::uint32_t maxHeight() const noexcept { return m_maxHeight; }

	/// @brief 設定: ミップマップ生成
	[[nodiscard]] bool generateMipsEnabled() const noexcept { return m_generateMips; }

	/// @brief 最後に処理した入力パス
	[[nodiscard]] const std::string& lastInputPath() const noexcept { return m_lastInputPath; }

	/// @brief 最後に処理した出力パス
	[[nodiscard]] const std::string& lastOutputPath() const noexcept { return m_lastOutputPath; }

private:
	std::uint32_t m_maxWidth;
	std::uint32_t m_maxHeight;
	bool m_generateMips;
	std::string m_lastInputPath;
	std::string m_lastOutputPath;
};

// ---------------------------------------------------------------------------
// テクスチャアトラスビルダー
// ---------------------------------------------------------------------------

/// @brief テクスチャアトラスビルダー
/// @details 複数のテクスチャを1枚のアトラス画像にパッキングする。
///          シンプルな行ベースのパッキングアルゴリズムを使用する。
///
/// @code
/// mitiru::resource::TextureAtlasBuilder builder;
/// builder.addImage("player", playerTex);
/// builder.addImage("enemy", enemyTex);
/// auto atlas = builder.build(1024, 1024);
/// auto metadata = builder.generateMetadata(atlas);
/// @endcode
class TextureAtlasBuilder
{
public:
	/// @brief アトラスに追加する画像を登録する
	/// @param name スプライト名
	/// @param texture テクスチャデータ
	void addImage(const std::string& name, TextureData texture)
	{
		m_images.push_back({name, std::move(texture)});
	}

	/// @brief 登録画像数を取得する
	[[nodiscard]] std::size_t imageCount() const noexcept
	{
		return m_images.size();
	}

	/// @brief アトラスをビルドする
	/// @param atlasWidth アトラス幅
	/// @param atlasHeight アトラス高さ
	/// @return 生成されたテクスチャアトラス
	[[nodiscard]] TextureAtlas build(std::uint32_t atlasWidth,
									 std::uint32_t atlasHeight) const;

	/// @brief アトラスメタデータをテキスト形式で生成する
	/// @param atlas アトラスデータ
	/// @return メタデータ文字列
	[[nodiscard]] static std::string generateMetadata(const TextureAtlas& atlas);

	/// @brief 登録画像をクリアする
	void clear()
	{
		m_images.clear();
	}

private:
	struct NamedImage
	{
		std::string name;
		TextureData texture;
	};

	/// @brief テクスチャをアトラスにコピーする
	static void blitImage(TextureData& dst, const TextureData& src,
						   std::uint32_t dstX, std::uint32_t dstY);

	std::vector<NamedImage> m_images;
};

// ---------------------------------------------------------------------------
// アセットマニフェスト
// ---------------------------------------------------------------------------

/// @brief アセットマニフェストのエントリ
struct ManifestEntry
{
	std::string path;                    ///< ファイルパス（相対）
	std::string type;                    ///< アセット種別（texture, audio, script 等）
	std::uint64_t sizeBytes = 0;         ///< ファイルサイズ
	std::string hash;                    ///< ハッシュ値（簡易チェック用）
	std::vector<std::string> dependencies; ///< 依存アセットパスリスト
};

/// @brief アセットマニフェスト
/// @details ビルド済みアセットの一覧と付帯情報を管理する。
///
/// @code
/// mitiru::resource::AssetManifest manifest;
/// manifest.generateManifest("assets/");
/// auto text = manifest.serialize();
/// auto mismatches = manifest.validate("assets/");
/// @endcode
class AssetManifest
{
public:
	/// @brief エントリを追加する
	void addEntry(ManifestEntry entry)
	{
		const auto path = entry.path;
		m_entries[path] = std::move(entry);
	}

	/// @brief エントリを取得する
	/// @param path ファイルパス
	/// @return エントリへのポインタ（見つからない場合 nullptr）
	[[nodiscard]] const ManifestEntry* getEntry(const std::string& path) const
	{
		const auto it = m_entries.find(path);
		if (it == m_entries.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	/// @brief 全エントリを取得する
	[[nodiscard]] std::vector<ManifestEntry> allEntries() const
	{
		std::vector<ManifestEntry> result;
		result.reserve(m_entries.size());
		for (const auto& [path, entry] : m_entries)
		{
			result.push_back(entry);
		}
		return result;
	}

	/// @brief エントリ数を取得する
	[[nodiscard]] std::size_t entryCount() const noexcept
	{
		return m_entries.size();
	}

	/// @brief 指定ディレクトリからマニフェストを自動生成する
	/// @param assetsDir アセットディレクトリパス
	/// @return 生成に成功した場合 true
	bool generateManifest(const std::string& assetsDir);

	/// @brief マニフェストを検証する
	/// @param assetsDir アセットディレクトリパス
	/// @return 不一致のパスリスト（空なら完全一致）
	[[nodiscard]] std::vector<std::string> validate(const std::string& assetsDir) const;

	/// @brief マニフェストをテキスト形式にシリアライズする
	[[nodiscard]] std::string serialize() const;

	/// @brief テキスト形式からマニフェストをデシリアライズする
	/// @param text シリアライズされたテキスト
	void deserialize(const std::string& text);

	/// @brief マニフェストをクリアする
	void clear()
	{
		m_entries.clear();
	}

private:
	/// @brief 拡張子からアセット種別を推定する
	[[nodiscard]] static std::string inferAssetType(const std::string& ext);

	/// @brief 簡易ハッシュ（パス + サイズベース）
	[[nodiscard]] static std::string computeSimpleHash(
		const std::string& path, std::uint64_t size);

	std::unordered_map<std::string, ManifestEntry> m_entries;
};

// ---------------------------------------------------------------------------
// アセットバンドラー
// ---------------------------------------------------------------------------

/// @brief バンドル内のファイルエントリ（ヘッダー用）
struct BundleFileEntry
{
	std::string path;                    ///< ファイルパス（相対）
	std::uint64_t offset = 0;            ///< バンドル内オフセット
	std::uint64_t size = 0;              ///< データサイズ
};

/// @brief バンドルヘッダー
struct BundleHeader
{
	std::uint32_t magic = 0x4D545242;    ///< マジックナンバー "MTRB"
	std::uint32_t version = 1;           ///< バージョン
	std::uint32_t fileCount = 0;         ///< ファイル数
	std::vector<BundleFileEntry> files;  ///< ファイルエントリリスト
};

/// @brief バンドルデータ（メモリ上の表現）
struct BundleData
{
	BundleHeader header;                 ///< ヘッダー
	std::vector<std::uint8_t> payload;   ///< 連結されたファイルデータ
};

/// @brief アセットバンドラー
/// @details マニフェストに基づいてアセットを1つのバンドルにパッキングする。
///          ヘッダー（ファイル数 + オフセットテーブル）+ 連結ファイルデータの形式。
///
/// @code
/// mitiru::resource::AssetBundler bundler;
/// bundler.addFile("textures/player.png", playerData);
/// bundler.addFile("shaders/basic.hlsl", shaderData);
/// auto bundle = bundler.build();
///
/// mitiru::resource::AssetBundleReader reader;
/// reader.load(bundle);
/// auto data = reader.readFile("textures/player.png");
/// @endcode
class AssetBundler
{
public:
	/// @brief バンドルに含めるファイルを追加する
	/// @param path ファイルパス（相対）
	/// @param data ファイルデータ
	void addFile(const std::string& path, std::vector<std::uint8_t> data)
	{
		m_files.push_back({path, std::move(data)});
	}

	/// @brief 追加済みファイル数を取得する
	[[nodiscard]] std::size_t fileCount() const noexcept
	{
		return m_files.size();
	}

	/// @brief バンドルをビルドする
	/// @return バンドルデータ
	[[nodiscard]] BundleData build() const;

	/// @brief マニフェストからバンドルをビルドする（ファイルシステムから読込）
	/// @param manifest マニフェスト
	/// @param assetsDir アセットディレクトリパス
	/// @return バンドルデータ
	[[nodiscard]] BundleData buildFromManifest(
		const AssetManifest& manifest,
		const std::string& assetsDir) const;

	/// @brief 追加済みファイルをクリアする
	void clear()
	{
		m_files.clear();
	}

private:
	struct FileData
	{
		std::string path;
		std::vector<std::uint8_t> data;
	};

	std::vector<FileData> m_files;
};

// ---------------------------------------------------------------------------
// アセットバンドルリーダー
// ---------------------------------------------------------------------------

/// @brief アセットバンドルリーダー
/// @details バンドルデータからパスを指定してファイルを読み出す。
class AssetBundleReader
{
public:
	/// @brief バンドルデータを読み込む
	/// @param bundle バンドルデータ
	void load(BundleData bundle)
	{
		m_bundle = std::move(bundle);
	}

	/// @brief バンドルが読み込まれているか
	[[nodiscard]] bool isLoaded() const noexcept
	{
		return m_bundle.header.magic == 0x4D545242;
	}

	/// @brief バンドル内のファイル数を取得する
	[[nodiscard]] std::uint32_t fileCount() const noexcept
	{
		return m_bundle.header.fileCount;
	}

	/// @brief バンドル内のファイルパス一覧を取得する
	[[nodiscard]] std::vector<std::string> filePaths() const;

	/// @brief パスを指定してファイルデータを読み出す
	/// @param path ファイルパス（相対）
	/// @return ファイルデータ（見つからない場合は空ベクタ）
	[[nodiscard]] std::vector<std::uint8_t> readFile(const std::string& path) const;

	/// @brief 指定パスのファイルが存在するか
	/// @param path ファイルパス
	/// @return 存在する場合 true
	[[nodiscard]] bool hasFile(const std::string& path) const;

	/// @brief 指定パスのファイルサイズを取得する
	/// @param path ファイルパス
	/// @return ファイルサイズ（見つからない場合は0）
	[[nodiscard]] std::uint64_t fileSize(const std::string& path) const;

private:
	BundleData m_bundle;
};

} // namespace mitiru::resource

// 実装本体（末尾 detail include 流儀）
#include <mitiru/resource/detail/AssetPipeline_impl.hpp>
