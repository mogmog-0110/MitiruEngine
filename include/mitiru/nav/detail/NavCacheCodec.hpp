#pragma once

/// @file NavCacheCodec.hpp
/// @brief .navcache の層を格納、復元する方法。生成側と読み込み側で共通して使う
/// 層は 1 枚数 KB で、読み込み時に 1 度だけ展開して保持するため、圧縮せずそのままコピーする。DLL に圧縮器を追加しない。

#include <cstring>

#include <DetourTileCacheBuilder.h>

namespace mitiru::nav::detail
{

struct NavCacheCopyCodec final : dtTileCacheCompressor
{
	int maxCompressedSize(const int bufferSize) override { return bufferSize; }

	dtStatus compress(const unsigned char* buffer, const int bufferSize, unsigned char* compressed,
		const int maxCompressedSize, int* compressedSize) override
	{
		if (bufferSize > maxCompressedSize) return DT_FAILURE | DT_BUFFER_TOO_SMALL;
		std::memcpy(compressed, buffer, static_cast<std::size_t>(bufferSize));
		(*compressedSize) = bufferSize;
		return DT_SUCCESS;
	}

	dtStatus decompress(const unsigned char* compressed, const int compressedSize, unsigned char* buffer,
		const int maxBufferSize, int* bufferSize) override
	{
		if (compressedSize > maxBufferSize) return DT_FAILURE | DT_BUFFER_TOO_SMALL;
		std::memcpy(buffer, compressed, static_cast<std::size_t>(compressedSize));
		(*bufferSize) = compressedSize;
		return DT_SUCCESS;
	}
};

} // namespace mitiru::nav::detail
