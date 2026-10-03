#pragma once

/// @file SoundPreloads.hpp
/// @brief 音のファイルを先に展開しておく。初めて鳴らすフレームで展開の待ちが出ないようにする
/// @details miniaudio の resource manager は、同じパスの展開済みデータを参照が残る間だけ共有する。ここで鳴らさない
///          ma_sound を 1 つ持てば、展開は resource manager のスレッドで進み、後の再生は展開済みを使う。
///          手放すと参照が 1 つ減り、再生中の音が無ければデータも消える。ma_engine より先に壊す。

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <mitiru/audio/detail/MiniaudioInclude.hpp>

namespace mitiru::audio
{

class SoundPreloads
{
public:
	SoundPreloads() = default;
	SoundPreloads(const SoundPreloads&) = delete;
	SoundPreloads& operator=(const SoundPreloads&) = delete;
	~SoundPreloads() { clear(); }

	/// @return 展開を頼んだか、もう頼んであれば true。ファイルを開けなければ false
	bool preload(ma_engine* engine, const std::string& path)
	{
		if (engine == nullptr || path.empty()) { return false; }
		if (m_sounds.find(path) != m_sounds.end()) { return true; }
		// 展開は後から別のスレッドで失敗するので、無いファイルはここで断る
		std::error_code ec;
		if (!std::filesystem::is_regular_file(std::filesystem::path(path), ec)) { return false; }
		auto sound = std::make_unique<ma_sound>();
		const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC | MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
		if (ma_sound_init_from_file(engine, path.c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS)
		{
			return false;
		}
		m_sounds.emplace(path, std::move(sound));
		return true;
	}

	/// @return 持っていれば手放して true
	bool release(std::string_view path)
	{
		const auto it = m_sounds.find(path);
		if (it == m_sounds.end()) { return false; }
		ma_sound_uninit(it->second.get());
		m_sounds.erase(it);
		return true;
	}

	/// @brief まだ展開の終わっていない数
	[[nodiscard]] std::size_t pending() const
	{
		std::size_t n = 0;
		for (const auto& [path, sound] : m_sounds)
		{
			auto* source = static_cast<ma_resource_manager_data_source*>(ma_sound_get_data_source(sound.get()));
			n += (source != nullptr && ma_resource_manager_data_source_result(source) == MA_BUSY) ? 1 : 0;
		}
		return n;
	}

	[[nodiscard]] std::size_t size() const noexcept { return m_sounds.size(); }

	void clear()
	{
		for (auto& [path, sound] : m_sounds) { ma_sound_uninit(sound.get()); }
		m_sounds.clear();
	}

private:
	std::map<std::string, std::unique_ptr<ma_sound>, std::less<>> m_sounds;
};

} // namespace mitiru::audio
