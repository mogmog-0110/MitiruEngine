#pragma once

/// @file HostView.hpp
/// @brief host がこの PC について知っている値のうち、描画だけが読む物 (ABI v62、ADR 0099)。
/// @details NetView と同じ性質で、PC ごとに違う値 (揺れの倍率、キー配列の刻印、モニタと音の機器の名前) なので録画に乗らない。
///          update で読んで GameMemory に書くと PC ごとに進みが変わって食い違うため、読むのは draw だけにする。
///          選ぶ画面の項目の数のように update が要る物は InputSnapshot の monitorCount / audioDeviceCount に別に来る。
///          host は Screen の中の HostView を描く前に書き、Screen::hostView() と DrawContext::host がそれを指す。
///          揺れの倍率、keyMode、キーの刻印、モニタと音のデバイスの一覧は host が書く。人が見ない実行 (headless、スクリプト、撮影、再生) では一覧を 0 / 空のままにする。

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mitiru::module
{

inline constexpr int kHostListMax = 8;       ///< モニタと音の機器の名前の一覧の上限
inline constexpr int kHostNameLen = 64;      ///< 名前の byte 数 (UTF-8、終端つき)
inline constexpr int kHostKeyCount = 256;    ///< keyLabels の添字は VK
inline constexpr int kHostKeyLabelLen = 8;   ///< 刻印の byte 数 (UTF-8、終端つき)
inline constexpr std::uint8_t kHostNoSelection = 0xFF;
inline constexpr int kHostSpectrumBands = 16;  ///< v63: audioSpectrum の帯域の数 (対数の帯、約 60 Hz から 16 kHz)

struct HostView
{
	/// 利用者のアクセシビリティ設定の倍率 0..1 (1 = 揺らす)。ゲームが自分で作る揺れに掛ける。hud.shake の意図は host が掛けるので二重に掛けない
	float screenShakeScale = 1.0f;
	float cameraShakeScale = 1.0f;
	std::uint8_t keyMode = 0;                           ///< input::KeyMode。0 = 位置で束縛、1 = 文字で束縛
	std::uint8_t monitorCount = 0;
	std::uint8_t monitorSelected = kHostNoSelection;    ///< 今いるモニタの添字。分からなければ kHostNoSelection
	std::uint8_t audioDeviceCount = 0;
	std::uint8_t audioDeviceSelected = kHostNoSelection;
	std::uint8_t _pad[3]{};
	char monitorNames[kHostListMax][kHostNameLen]{};
	char audioDeviceNames[kHostListMax][kHostNameLen]{};
	/// VK ごとの刻印 (今のキー配列での字)。位置で束縛している間、HUD に出す字はこれを引く。空 = 表の名前を使う
	char keyLabels[kHostKeyCount][kHostKeyLabelLen]{};
	/// v63 (ADR 0112): 直前に出した音のスペクトラム。帯域 i は kHostSpectrumBands 本の対数の帯 (約 60 Hz から 16 kHz)、値は 0..1
	/// (-60 dB で 0、0 dBFS で 1)。描画だけが読む。音の出力は PC と機器で変わるので、update で読んで GameMemory に書かない
	float audioSpectrum[kHostSpectrumBands]{};
	/// 直前に出した音の波形の最大の振幅 0..1
	float audioPeak = 0.0f;
	/// 0 = 読めない (音の backend が無い、headless、まだ出していない)。audioSpectrum と audioPeak は 0 のまま
	std::uint32_t audioSpectrumValid = 0;
	/// 後続の追加を同じバージョンのまま入れるための予約 (必ず 0)
	std::uint32_t reserved[14]{};

	[[nodiscard]] const char* keyLabel(int vk) const noexcept
	{
		return (vk >= 0 && vk < kHostKeyCount) ? keyLabels[vk] : "";
	}
	/// 帯域 band のスペクトラムの値 0..1。範囲外と読めないときは 0
	[[nodiscard]] float spectrum(int band) const noexcept
	{
		return (audioSpectrumValid != 0 && band >= 0 && band < kHostSpectrumBands) ? audioSpectrum[band] : 0.0f;
	}
	[[nodiscard]] const char* monitorName(int i) const noexcept
	{
		return (i >= 0 && i < kHostListMax && i < monitorCount) ? monitorNames[i] : "";
	}
	[[nodiscard]] const char* audioDeviceName(int i) const noexcept
	{
		return (i >= 0 && i < kHostListMax && i < audioDeviceCount) ? audioDeviceNames[i] : "";
	}
};

static_assert(sizeof(HostView) == 3216 && offsetof(HostView, monitorNames) == 16 && offsetof(HostView, audioDeviceNames) == 528 &&
              offsetof(HostView, keyLabels) == 1040 && offsetof(HostView, audioSpectrum) == 3088 &&
              offsetof(HostView, audioPeak) == 3152 && offsetof(HostView, audioSpectrumValid) == 3156 &&
              offsetof(HostView, reserved) == 3160, "HostView wire size 固定 (v62、v63 は予約の切り出しだけ)");
static_assert(std::is_trivially_copyable_v<HostView> && std::is_standard_layout_v<HostView>);

}  // namespace mitiru::module
