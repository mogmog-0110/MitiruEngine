#pragma once

/// @file DawnDiskCache.hpp
/// @brief Dawn が作る物 (D3D11 / D3D12 のシェーダーの変換結果など) を、DX12 のシェーダーと同じ置き場に残す
/// @details 2 回目以降の起動で、パイプラインごとに 1 秒前後かかる FXC の変換を飛ばす (最初のフレームが全パイプラインの終わりを待つ長さ)。
///          Dawn の公開の入口 `wgpu::DawnCacheDeviceDescriptor` に読み書きの関数を渡す。置き場と無効化 (MITIRU_SHADER_CACHE=0) は
///          DX12 と同じ `ShaderDiskCache`。Dawn の鍵は中身を含む長い列なので、XXH3 の 128 bit に畳んで保存の名前にする。
///          読みは「大きさの問い合わせ」と「中身の取得」の 2 回に分けて呼ばれるので、直前に読んだ 1 件を覚えて二度読みしない。

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

#include <mitiru/gfx/dx12/Dx12ShaderDiskCache.hpp>

namespace mitiru::gfx::dawncache
{

[[nodiscard]] inline ShaderCacheKey keyOf(std::size_t keySize, const std::uint8_t* key) noexcept
{
	return makeShaderCacheKey(std::string_view(reinterpret_cast<const char*>(key), keySize), "dawn", "", 0, 0x44574E);
}

struct LastRead
{
	std::mutex mutex;
	const ShaderDiskCache* source = nullptr;
	ShaderCacheKey key{};
	std::vector<std::uint8_t> bytes;
	bool valid = false;
};

[[nodiscard]] inline LastRead& lastRead() noexcept
{
	static LastRead r;
	return r;
}

/// @brief 無ければ 0。value が空 (valueSize 0) なら大きさだけを返す
inline std::size_t load(const ShaderDiskCache& cache, std::size_t keySize, const std::uint8_t* key, std::size_t valueSize, std::uint8_t* value)
{
	const ShaderCacheKey k = keyOf(keySize, key);
	LastRead& last = lastRead();
	const std::lock_guard<std::mutex> lock(last.mutex);
	if (!last.valid || last.source != &cache || !(last.key == k))
	{
		last.valid = cache.load(k, last.bytes);
		last.source = &cache;
		last.key = k;
		if (!last.valid) { return 0; }
	}
	if (value != nullptr && valueSize >= last.bytes.size()) { std::copy(last.bytes.begin(), last.bytes.end(), value); }
	return last.bytes.size();
}

inline void store(const ShaderDiskCache& cache, std::size_t keySize, const std::uint8_t* key, std::size_t valueSize, const std::uint8_t* value)
{
	cache.store(keyOf(keySize, key), value, valueSize);
}

/// @brief 置き場が有効なら desc に読み書きを渡して true。無効 (MITIRU_SHADER_CACHE=0) なら false で何もしない。
///        C++ の SetDawnLoadCacheDataCallback はこの引数の並びを受けられないので、C の構造体に直接入れる。
///        isolationKey は Dawn の版 (cmake/dawn_version.cmake の固定したコミット) で分け、版が替わった後に古い結果を読まないようにする
///        (isolationKey が指す文字列は、デバイスを作り終えるまで生きていること)
inline bool attach(WGPUDawnCacheDeviceDescriptor& desc, std::string_view isolationKey)
{
	if (!ShaderDiskCache::process().enabled()) { return false; }
	desc = WGPU_DAWN_CACHE_DEVICE_DESCRIPTOR_INIT;
	desc.isolationKey = {isolationKey.data(), isolationKey.size()};
	desc.dawnLoadCacheDataCallbackInfo.callback = [](std::size_t keySize, const std::uint8_t* key, std::size_t valueSize,
	                                                 std::uint8_t* value, void*, void*) -> std::size_t { return load(ShaderDiskCache::process(), keySize, key, valueSize, value); };
	desc.dawnStoreCacheDataCallbackInfo.callback = [](std::size_t keySize, const std::uint8_t* key, std::size_t valueSize,
	                                                  const std::uint8_t* value, void*, void*) { store(ShaderDiskCache::process(), keySize, key, valueSize, value); };
	return true;
}

} // namespace mitiru::gfx::dawncache
