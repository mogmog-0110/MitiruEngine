#pragma once

/// @file OutputDeviceSetting.hpp
/// @brief 出力機器の選択を audio_device.json に保存し、F1 メニューの選択肢を作る。
/// @details 機器は PC の持ち物でゲームの設定ではないため、settings.json（UserSettings）ではなく専用のファイルに置く。
/// ファイルがない、壊れている、名前が空のいずれかなら、OS の既定の機器を使う。

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <mitiru/audio/detail/MiniaudioDeviceHost.hpp>
#include <mitiru/save/AtomicFile.hpp>

namespace mitiru::audio
{

inline constexpr std::string_view kOutputDeviceDefaultLabel = "System default";

[[nodiscard]] inline std::string parseOutputDeviceName(std::string_view text)
{
	const auto json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
	if (!json.is_object()) { return {}; }
	const auto it = json.find("outputDevice");
	return (it != json.end() && it->is_string()) ? it->get<std::string>() : std::string{};
}

[[nodiscard]] inline std::string loadOutputDeviceName(const std::filesystem::path& file)
{
	if (file.empty()) { return {}; }
	const auto bytes = save::readWholeFile(file);
	if (!bytes) { return {}; }
	return parseOutputDeviceName(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
}

/// @return 書けたら true。file が空なら何もせず、遊びはメモリ上で続ける。
inline bool saveOutputDeviceName(const std::filesystem::path& file, const std::string& name, std::string* error = nullptr)
{
	if (file.empty()) { return true; }
	const std::string text = nlohmann::json{{"outputDevice", name}}.dump(2) + "\n";
	const auto* data = reinterpret_cast<const std::uint8_t*>(text.data());
	return save::writeFileAtomic(file, std::span<const std::uint8_t>(data, text.size()), /*keepBackup=*/false, error);
}

struct OutputDeviceChoices
{
	std::vector<std::string> labels;   ///< [0] は OS の既定。以降は機器の名前。メニューは '|' で区切るので名前の '|' は '/' にする
	int                      selected = 0;
};

/// @brief 選んだ名前が一覧にないときは、見えない選択のまま鳴らさないよう、末尾に「<名前> (not found)」を足して選択する。
[[nodiscard]] inline OutputDeviceChoices outputDeviceChoices(const std::vector<detail::OutputDeviceInfo>& devices,
                                                              const std::string& preferred)
{
	OutputDeviceChoices out;
	const auto clean = [](std::string s) { for (char& c : s) { if (c == '|') { c = '/'; } } return s; };
	out.labels.emplace_back(kOutputDeviceDefaultLabel);
	for (std::size_t i = 0; i < devices.size(); ++i)
	{
		out.labels.push_back(clean(devices[i].name));
		if (devices[i].name == preferred) { out.selected = static_cast<int>(i) + 1; }
	}
	if (!preferred.empty() && out.selected == 0)
	{
		out.labels.push_back(clean(preferred) + " (not found)");
		out.selected = static_cast<int>(out.labels.size()) - 1;
	}
	return out;
}

/// @brief outputDeviceChoices() の index を機器名へ戻す。0 と範囲外なら既定の機器を表す空文字列を返し、「(not found)」の行なら元の名前を返す。
[[nodiscard]] inline std::string outputDeviceNameAt(const std::vector<detail::OutputDeviceInfo>& devices,
                                                    const std::string& preferred, int index)
{
	if (index <= 0) { return {}; }
	const std::size_t i = static_cast<std::size_t>(index) - 1;
	if (i < devices.size()) { return devices[i].name; }
	return (i == devices.size()) ? preferred : std::string{};
}

}  // namespace mitiru::audio
