#pragma once

/// @file HostAudioOutput.hpp
/// @brief 出力機器の選択を F1 メニューにつなぎ、選んだ名前を audio_device.json に残す
/// @details 機器の一覧と選びは、F1 メニューと、ゲームの選ぶ画面 (HostView の audioDeviceNames と Hud::audioDevice、ABI v62) が使う。
///          一覧の名前は元の名前のまま持ち、ゲームに見せる形への変換は module/detail/HostEnvironment62.hpp が受け持つ。

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <mitiru/audio/MiniaudioEngine.hpp>
#include <mitiru/audio/OutputDeviceSetting.hpp>
#include <mitiru/debug/ConsoleOut.hpp>

namespace mitiru::host
{

class HostAudioOutput
{
public:
	/// @param file 保存先。空の場合は保存しない（ゲーム名が決まっていない起動時）
	explicit HostAudioOutput(std::filesystem::path file) : m_file(std::move(file)) {}

	/// 前回選んだ機器名。engine の作成時に渡す
	[[nodiscard]] std::string savedName() const { return audio::loadOutputDeviceName(m_file); }

	void attach(audio::MiniaudioEngine* engine) noexcept { m_engine = engine; }

	/// @brief F1 メニューの選択肢。メニューを開いている間は毎フレーム呼ばれるため、一覧の数え直しは 3 秒に 1 回に絞る
	[[nodiscard]] std::vector<std::string> choices(int& selected)
	{
		selected = 0;
		if (m_engine == nullptr) { return {}; }
		const auto now = std::chrono::steady_clock::now();
		if (now - m_refreshedAt >= std::chrono::seconds(3))
		{
			m_engine->refreshOutputDevices();
			m_refreshedAt = now;
		}
		m_shown = m_engine->outputDevices();
		m_shownPreferred = m_engine->outputDevice();
		const audio::OutputDeviceChoices c = audio::outputDeviceChoices(m_shown, m_shownPreferred);
		selected = c.selected;
		return c.labels;
	}

	/// @brief ゲームに見せる機器の名前。selected は選んでいる機器の添字で、OS の既定のまま、または一覧に無いときは -1
	[[nodiscard]] std::vector<std::string> deviceNames(int& selected) const
	{
		selected = -1;
		std::vector<std::string> names;
		if (m_engine == nullptr) { return names; }
		const std::string preferred = m_engine->outputDevice();
		for (const audio::detail::OutputDeviceInfo& d : m_engine->outputDevices())
		{
			if (!preferred.empty() && d.name == preferred && selected < 0) { selected = static_cast<int>(names.size()); }
			names.push_back(d.name);
		}
		return names;
	}

	/// @brief 今選んでいる機器の名前。OS の既定のままなら空
	[[nodiscard]] std::string current() const { return m_engine != nullptr ? m_engine->outputDevice() : std::string(); }

	/// @brief 機器の一覧を数え直させる (結果は次の update で deviceNames に出る)
	void refreshDevices()
	{
		if (m_engine != nullptr) { m_engine->refreshOutputDevices(); }
	}

	/// @brief 名前の機器へ切り替え、audio_device.json に残す。空は OS の既定。F1 メニューの choose と同じ保存
	void chooseNamed(const std::string& name)
	{
		if (m_engine == nullptr) { return; }
		m_engine->setOutputDevice(name);
		std::string error;
		if (!audio::saveOutputDeviceName(m_file, name, &error))
		{
			console::noticef("出力機器の選択を保存できませんでした (%s)", error.c_str());
		}
	}

	void choose(int index)
	{
		if (m_engine == nullptr) { return; }
		// index は直近に表示した一覧の行番号。一覧は裏で数え直されて並びが変わるため、表示時の一覧から名前に戻す
		chooseNamed(audio::outputDeviceNameAt(m_shown, m_shownPreferred, index));
	}

private:
	std::filesystem::path                 m_file;
	audio::MiniaudioEngine*               m_engine = nullptr;
	std::vector<audio::detail::OutputDeviceInfo> m_shown;   ///< 最後にメニューへ見せた一覧
	std::string                           m_shownPreferred;
	std::chrono::steady_clock::time_point m_refreshedAt{};
};

}  // namespace mitiru::host
