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
											 std::uint32_t maxHeight)
{
	if (input.width == 0 || input.height == 0)
	{
		return input;
	}

	/// アスペクト比を維持してスケールを計算
	const float scaleX = static_cast<float>(maxWidth) / static_cast<float>(input.width);
	const float scaleY = static_cast<float>(maxHeight) / static_cast<float>(input.height);
	const float scale = std::min(scaleX, scaleY);

	if (scale >= 1.0f)
	{
		return input;  ///< 拡大は行わない
	}

	const auto newWidth = static_cast<std::uint32_t>(
		static_cast<float>(input.width) * scale);
	const auto newHeight = static_cast<std::uint32_t>(
		static_cast<float>(input.height) * scale);

	TextureData output;
	output.width = std::max(newWidth, 1u);
	output.height = std::max(newHeight, 1u);
	output.channels = input.channels;
	output.pixels.resize(
		static_cast<std::size_t>(output.width) * output.height * output.channels);

	/// ニアレストネイバーサンプリング
	for (std::uint32_t y = 0; y < output.height; ++y)
	{
		for (std::uint32_t x = 0; x < output.width; ++x)
		{
			const auto srcX = static_cast<std::uint32_t>(
				static_cast<float>(x) / scale);
			const auto srcY = static_cast<std::uint32_t>(
				static_cast<float>(y) / scale);

			const auto clampedX = std::min(srcX, input.width - 1);
			const auto clampedY = std::min(srcY, input.height - 1);

			const std::size_t srcIdx =
				(static_cast<std::size_t>(clampedY) * input.width + clampedX) * input.channels;
			const std::size_t dstIdx =
				(static_cast<std::size_t>(y) * output.width + x) * output.channels;

			for (std::uint32_t c = 0; c < output.channels; ++c)
			{
				output.pixels[dstIdx + c] = input.pixels[srcIdx + c];
			}
		}
	}

	return output;
}

/// @brief ミップマップチェインを生成する
/// @param source 元テクスチャ
/// @return ミップマップレベルのリスト（レベル0 = 元画像の半分）
[[nodiscard]] inline std::vector<MipLevel> generateMipmaps(const TextureData& source)
{
	std::vector<MipLevel> mips;

	auto currentWidth = source.width;
	auto currentHeight = source.height;
	const auto* currentPixels = source.pixels.data();
	std::vector<std::uint8_t> currentBuffer = source.pixels;

	while (currentWidth > 1 || currentHeight > 1)
	{
		const auto newWidth = std::max(currentWidth / 2, 1u);
		const auto newHeight = std::max(currentHeight / 2, 1u);

		MipLevel mip;
		mip.width = newWidth;
		mip.height = newHeight;
		mip.pixels.resize(
			static_cast<std::size_t>(newWidth) * newHeight * source.channels);

		/// 2x2 ボックスフィルター
		for (std::uint32_t y = 0; y < newHeight; ++y)
		{
			for (std::uint32_t x = 0; x < newWidth; ++x)
			{
				const auto sx = std::min(x * 2, currentWidth - 1);
				const auto sy = std::min(y * 2, currentHeight - 1);
				const auto sx1 = std::min(sx + 1, currentWidth - 1);
				const auto sy1 = std::min(sy + 1, currentHeight - 1);

				for (std::uint32_t c = 0; c < source.channels; ++c)
				{
					const auto idx00 = (static_cast<std::size_t>(sy) * currentWidth + sx) * source.channels + c;
					const auto idx10 = (static_cast<std::size_t>(sy) * currentWidth + sx1) * source.channels + c;
					const auto idx01 = (static_cast<std::size_t>(sy1) * currentWidth + sx) * source.channels + c;
					const auto idx11 = (static_cast<std::size_t>(sy1) * currentWidth + sx1) * source.channels + c;

					const auto avg = static_cast<std::uint8_t>(
						(static_cast<std::uint32_t>(currentBuffer[idx00])
						+ static_cast<std::uint32_t>(currentBuffer[idx10])
						+ static_cast<std::uint32_t>(currentBuffer[idx01])
						+ static_cast<std::uint32_t>(currentBuffer[idx11])) / 4);

					const auto dstIdx = (static_cast<std::size_t>(y) * newWidth + x) * source.channels + c;
					mip.pixels[dstIdx] = avg;
				}
			}
		}

		currentBuffer = mip.pixels;
		currentWidth = newWidth;
		currentHeight = newHeight;
		mips.push_back(std::move(mip));
	}

	return mips;
}

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
									 std::uint32_t atlasHeight) const
	{
		TextureAtlas atlas;
		atlas.atlasWidth = atlasWidth;
		atlas.atlasHeight = atlasHeight;

		/// アトラス画像を初期化（透明）
		atlas.image.width = atlasWidth;
		atlas.image.height = atlasHeight;
		atlas.image.channels = 4;
		atlas.image.pixels.resize(
			static_cast<std::size_t>(atlasWidth) * atlasHeight * 4, 0);

		/// 高さの降順でソート（パッキング効率向上）
		auto sorted = m_images;
		std::sort(sorted.begin(), sorted.end(),
			[](const NamedImage& a, const NamedImage& b)
			{
				return a.texture.height > b.texture.height;
			});

		/// シンプルな行ベースパッキング
		std::uint32_t currentX = 0;
		std::uint32_t currentY = 0;
		std::uint32_t rowHeight = 0;

		for (const auto& img : sorted)
		{
			/// 現在の行に収まらなければ次の行へ
			if (currentX + img.texture.width > atlasWidth)
			{
				currentX = 0;
				currentY += rowHeight;
				rowHeight = 0;
			}

			/// アトラスに収まらなければスキップ
			if (currentY + img.texture.height > atlasHeight)
			{
				continue;
			}

			/// ピクセルをコピー
			blitImage(atlas.image, img.texture, currentX, currentY);

			/// エントリを追加
			AtlasEntry entry;
			entry.name = img.name;
			entry.x = currentX;
			entry.y = currentY;
			entry.width = img.texture.width;
			entry.height = img.texture.height;
			atlas.entries.push_back(entry);

			currentX += img.texture.width;
			rowHeight = std::max(rowHeight, img.texture.height);
		}

		return atlas;
	}

	/// @brief アトラスメタデータをテキスト形式で生成する
	/// @param atlas アトラスデータ
	/// @return メタデータ文字列
	[[nodiscard]] static std::string generateMetadata(const TextureAtlas& atlas)
	{
		std::ostringstream oss;
		oss << "atlas_version 1\n";
		oss << "size " << atlas.atlasWidth << " " << atlas.atlasHeight << "\n";
		oss << "entries " << atlas.entries.size() << "\n";

		for (const auto& entry : atlas.entries)
		{
			oss << "sprite " << entry.name
				<< " " << entry.x << " " << entry.y
				<< " " << entry.width << " " << entry.height << "\n";
		}
		return oss.str();
	}

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
						   std::uint32_t dstX, std::uint32_t dstY)
	{
		const auto channels = std::min(dst.channels, src.channels);

		for (std::uint32_t y = 0; y < src.height; ++y)
		{
			for (std::uint32_t x = 0; x < src.width; ++x)
			{
				const auto dstPx = dstX + x;
				const auto dstPy = dstY + y;

				if (dstPx >= dst.width || dstPy >= dst.height)
				{
					continue;
				}

				const std::size_t srcIdx =
					(static_cast<std::size_t>(y) * src.width + x) * src.channels;
				const std::size_t dstIdx =
					(static_cast<std::size_t>(dstPy) * dst.width + dstPx) * dst.channels;

				for (std::uint32_t c = 0; c < channels; ++c)
				{
					dst.pixels[dstIdx + c] = src.pixels[srcIdx + c];
				}
			}
		}
	}

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
	bool generateManifest(const std::string& assetsDir)
	{
		std::error_code ec;
		if (!std::filesystem::is_directory(assetsDir, ec))
		{
			return false;
		}

		m_entries.clear();

		for (const auto& entry : std::filesystem::recursive_directory_iterator(assetsDir, ec))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}

			ManifestEntry mEntry;
			mEntry.path = std::filesystem::relative(entry.path(), assetsDir, ec).string();
			mEntry.sizeBytes = static_cast<std::uint64_t>(entry.file_size(ec));
			mEntry.type = inferAssetType(entry.path().extension().string());
			mEntry.hash = computeSimpleHash(mEntry.path, mEntry.sizeBytes);

			m_entries[mEntry.path] = std::move(mEntry);
		}

		return true;
	}

	/// @brief マニフェストを検証する
	/// @param assetsDir アセットディレクトリパス
	/// @return 不一致のパスリスト（空なら完全一致）
	[[nodiscard]] std::vector<std::string> validate(const std::string& assetsDir) const
	{
		std::vector<std::string> mismatches;

		for (const auto& [path, entry] : m_entries)
		{
			const auto fullPath = assetsDir + "/" + path;
			std::error_code ec;

			if (!std::filesystem::exists(fullPath, ec))
			{
				mismatches.push_back(path + " (missing)");
				continue;
			}

			const auto actualSize = static_cast<std::uint64_t>(
				std::filesystem::file_size(fullPath, ec));

			if (actualSize != entry.sizeBytes)
			{
				mismatches.push_back(path + " (size mismatch)");
			}
		}

		return mismatches;
	}

	/// @brief マニフェストをテキスト形式にシリアライズする
	[[nodiscard]] std::string serialize() const
	{
		std::ostringstream oss;
		oss << "manifest_version 1\n";
		oss << "entry_count " << m_entries.size() << "\n";

		for (const auto& [path, entry] : m_entries)
		{
			oss << "asset " << entry.path
				<< " " << entry.type
				<< " " << entry.sizeBytes
				<< " " << entry.hash;

			if (!entry.dependencies.empty())
			{
				oss << " deps:";
				for (std::size_t i = 0; i < entry.dependencies.size(); ++i)
				{
					if (i > 0) oss << ",";
					oss << entry.dependencies[i];
				}
			}

			oss << "\n";
		}
		return oss.str();
	}

	/// @brief テキスト形式からマニフェストをデシリアライズする
	/// @param text シリアライズされたテキスト
	void deserialize(const std::string& text)
	{
		m_entries.clear();

		std::istringstream iss(text);
		std::string line;

		while (std::getline(iss, line))
		{
			if (line.empty())
			{
				continue;
			}

			std::istringstream lineStream(line);
			std::string token;
			lineStream >> token;

			if (token == "asset")
			{
				ManifestEntry entry;
				std::string depsToken;

				lineStream >> entry.path >> entry.type >> entry.sizeBytes >> entry.hash;

				/// 依存関係のパース
				if (lineStream >> depsToken)
				{
					if (depsToken.substr(0, 5) == "deps:")
					{
						auto depStr = depsToken.substr(5);
						std::istringstream depStream(depStr);
						std::string dep;
						while (std::getline(depStream, dep, ','))
						{
							if (!dep.empty())
							{
								entry.dependencies.push_back(dep);
							}
						}
					}
				}

				m_entries[entry.path] = std::move(entry);
			}
		}
	}

	/// @brief マニフェストをクリアする
	void clear()
	{
		m_entries.clear();
	}

private:
	/// @brief 拡張子からアセット種別を推定する
	[[nodiscard]] static std::string inferAssetType(const std::string& ext)
	{
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
			ext == ".bmp" || ext == ".tga" || ext == ".dds" || ext == ".ktx")
		{
			return "texture";
		}
		if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac")
		{
			return "audio";
		}
		if (ext == ".hlsl" || ext == ".glsl" || ext == ".vert" || ext == ".frag")
		{
			return "shader";
		}
		if (ext == ".vns")
		{
			return "script";
		}
		if (ext == ".json")
		{
			return "data";
		}
		if (ext == ".ttf" || ext == ".otf")
		{
			return "font";
		}
		if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".fbx")
		{
			return "mesh";
		}
		return "unknown";
	}

	/// @brief 簡易ハッシュ（パス + サイズベース）
	[[nodiscard]] static std::string computeSimpleHash(
		const std::string& path, std::uint64_t size)
	{
		/// FNV-1a 風の簡易ハッシュ
		std::uint64_t hash = 14695981039346656037ULL;
		for (char c : path)
		{
			hash ^= static_cast<std::uint64_t>(c);
			hash *= 1099511628211ULL;
		}
		hash ^= size;
		hash *= 1099511628211ULL;

		std::ostringstream oss;
		oss << std::hex << std::setfill('0') << std::setw(16) << hash;
		return oss.str();
	}

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
	[[nodiscard]] BundleData build() const
	{
		BundleData bundle;
		bundle.header.magic = 0x4D545242;
		bundle.header.version = 1;
		bundle.header.fileCount = static_cast<std::uint32_t>(m_files.size());

		std::uint64_t currentOffset = 0;

		for (const auto& file : m_files)
		{
			BundleFileEntry entry;
			entry.path = file.path;
			entry.offset = currentOffset;
			entry.size = file.data.size();
			bundle.header.files.push_back(entry);

			currentOffset += file.data.size();
		}

		/// ペイロードを構築
		bundle.payload.reserve(static_cast<std::size_t>(currentOffset));
		for (const auto& file : m_files)
		{
			bundle.payload.insert(
				bundle.payload.end(),
				file.data.begin(),
				file.data.end());
		}

		return bundle;
	}

	/// @brief マニフェストからバンドルをビルドする（ファイルシステムから読込）
	/// @param manifest マニフェスト
	/// @param assetsDir アセットディレクトリパス
	/// @return バンドルデータ
	[[nodiscard]] BundleData buildFromManifest(
		const AssetManifest& manifest,
		const std::string& assetsDir) const
	{
		AssetBundler tempBundler;

		for (const auto& entry : manifest.allEntries())
		{
			const auto fullPath = assetsDir + "/" + entry.path;

			std::error_code ec;
			if (!std::filesystem::exists(fullPath, ec))
			{
				continue;
			}

			/// ファイルサイズを確認してダミーデータを作成
			/// （実際のファイルI/Oはエンジンのファイルシステムに委譲）
			const auto fileSize = static_cast<std::size_t>(
				std::filesystem::file_size(fullPath, ec));

			std::vector<std::uint8_t> data(fileSize, 0);
			tempBundler.addFile(entry.path, std::move(data));
		}

		return tempBundler.build();
	}

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
	[[nodiscard]] std::vector<std::string> filePaths() const
	{
		std::vector<std::string> paths;
		paths.reserve(m_bundle.header.files.size());
		for (const auto& entry : m_bundle.header.files)
		{
			paths.push_back(entry.path);
		}
		return paths;
	}

	/// @brief パスを指定してファイルデータを読み出す
	/// @param path ファイルパス（相対）
	/// @return ファイルデータ（見つからない場合は空ベクタ）
	[[nodiscard]] std::vector<std::uint8_t> readFile(const std::string& path) const
	{
		for (const auto& entry : m_bundle.header.files)
		{
			if (entry.path == path)
			{
				if (entry.offset + entry.size > m_bundle.payload.size())
				{
					return {};
				}

				return std::vector<std::uint8_t>(
					m_bundle.payload.begin() + static_cast<std::ptrdiff_t>(entry.offset),
					m_bundle.payload.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
			}
		}
		return {};
	}

	/// @brief 指定パスのファイルが存在するか
	/// @param path ファイルパス
	/// @return 存在する場合 true
	[[nodiscard]] bool hasFile(const std::string& path) const
	{
		return std::any_of(
			m_bundle.header.files.begin(),
			m_bundle.header.files.end(),
			[&path](const BundleFileEntry& e) { return e.path == path; });
	}

	/// @brief 指定パスのファイルサイズを取得する
	/// @param path ファイルパス
	/// @return ファイルサイズ（見つからない場合は0）
	[[nodiscard]] std::uint64_t fileSize(const std::string& path) const
	{
		for (const auto& entry : m_bundle.header.files)
		{
			if (entry.path == path)
			{
				return entry.size;
			}
		}
		return 0;
	}

private:
	BundleData m_bundle;
};

} // namespace mitiru::resource
