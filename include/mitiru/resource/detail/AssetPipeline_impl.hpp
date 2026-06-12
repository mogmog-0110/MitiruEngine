#pragma once

/// @file AssetPipeline_impl.hpp
/// @brief アセット処理パイプラインの実装本体（AssetPipeline.hpp から機械的分割）

#include <mitiru/resource/AssetPipeline.hpp>

namespace mitiru::resource
{

// ---------------------------------------------------------------------------
// テクスチャ処理 (フリー関数)
// ---------------------------------------------------------------------------

/// @brief テクスチャをリサイズする（ニアレストネイバー）
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

// ---------------------------------------------------------------------------
// テクスチャアトラスビルダー
// ---------------------------------------------------------------------------

/// @brief アトラスをビルドする
inline TextureAtlas TextureAtlasBuilder::build(std::uint32_t atlasWidth,
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
inline std::string TextureAtlasBuilder::generateMetadata(const TextureAtlas& atlas)
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

/// @brief テクスチャをアトラスにコピーする
inline void TextureAtlasBuilder::blitImage(TextureData& dst, const TextureData& src,
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

// ---------------------------------------------------------------------------
// アセットマニフェスト
// ---------------------------------------------------------------------------

/// @brief 指定ディレクトリからマニフェストを自動生成する
inline bool AssetManifest::generateManifest(const std::string& assetsDir)
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
inline std::vector<std::string> AssetManifest::validate(const std::string& assetsDir) const
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
inline std::string AssetManifest::serialize() const
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
inline void AssetManifest::deserialize(const std::string& text)
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

/// @brief 拡張子からアセット種別を推定する
inline std::string AssetManifest::inferAssetType(const std::string& ext)
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
inline std::string AssetManifest::computeSimpleHash(
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

// ---------------------------------------------------------------------------
// アセットバンドラー
// ---------------------------------------------------------------------------

/// @brief バンドルをビルドする
inline BundleData AssetBundler::build() const
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
inline BundleData AssetBundler::buildFromManifest(
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

// ---------------------------------------------------------------------------
// アセットバンドルリーダー
// ---------------------------------------------------------------------------

/// @brief バンドル内のファイルパス一覧を取得する
inline std::vector<std::string> AssetBundleReader::filePaths() const
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
inline std::vector<std::uint8_t> AssetBundleReader::readFile(const std::string& path) const
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
inline bool AssetBundleReader::hasFile(const std::string& path) const
{
	return std::any_of(
		m_bundle.header.files.begin(),
		m_bundle.header.files.end(),
		[&path](const BundleFileEntry& e) { return e.path == path; });
}

/// @brief 指定パスのファイルサイズを取得する
inline std::uint64_t AssetBundleReader::fileSize(const std::string& path) const
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

} // namespace mitiru::resource
