#pragma once

/// @file SfxCaption.hpp
/// @brief 効果音の字幕 (音の種類、文の鍵、方向を付けるか) と、利用者が選ぶ表示の設定。
/// @details assets/audio/sounds.json の各音の "caption" がこの形になる。文は strings.json の鍵で持ち、翻訳に乗る。

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mitiru::audio::sfx
{

enum class CaptionKind : std::uint8_t
{
	Dialogue,   ///< 台詞 (話者を付けられる)
	Ambient,    ///< 環境音と効果音 (ドアが閉まる、足音)
	Music,      ///< 曲の始まり
	Alert,      ///< 危険や通知など、聞き逃すと困る音
};

[[nodiscard]] constexpr std::string_view captionKindName(CaptionKind k) noexcept
{
	switch (k)
	{
	case CaptionKind::Dialogue: return "dialogue";
	case CaptionKind::Ambient:  return "ambient";
	case CaptionKind::Music:    return "music";
	case CaptionKind::Alert:    return "alert";
	}
	return "ambient";
}

[[nodiscard]] constexpr std::optional<CaptionKind> parseCaptionKind(std::string_view name) noexcept
{
	if (name == "dialogue") { return CaptionKind::Dialogue; }
	if (name == "ambient")  { return CaptionKind::Ambient; }
	if (name == "music")    { return CaptionKind::Music; }
	if (name == "alert")    { return CaptionKind::Alert; }
	return std::nullopt;
}

/// @brief 1 つの音に付ける字幕。textKey が空なら字幕なし。
struct SfxCaption
{
	std::string textKey;      ///< 「ドアが閉まる」などの文の鍵
	std::string speakerKey;   ///< 話者の名前の鍵 (空なら話者なし)
	CaptionKind kind = CaptionKind::Ambient;
	bool directional = false; ///< 音の来る方向 (左・右・前・後ろ) を添えるか

	[[nodiscard]] bool present() const noexcept { return !textKey.empty(); }
};

/// @brief 利用者の設定 (accessibility.captions と字幕の見た目)。種類ごとに独立して切れる。
struct CaptionPrefs
{
	bool dialogue = true;
	bool ambient = false;
	bool music = false;
	bool alert = true;
	bool speakerNames = true;
	bool directional = true;
	float scale = 1.0f;        ///< 文字の大きさの倍率 (accessibility.subtitleScale)
	float background = 0.5f;   ///< 下地の不透明度 0..1 (accessibility.subtitleBackground)

	[[nodiscard]] bool allows(CaptionKind k) const noexcept
	{
		switch (k)
		{
		case CaptionKind::Dialogue: return dialogue;
		case CaptionKind::Ambient:  return ambient;
		case CaptionKind::Music:    return music;
		case CaptionKind::Alert:    return alert;
		}
		return false;
	}

	[[nodiscard]] bool operator==(const CaptionPrefs&) const noexcept = default;
};

}  // namespace mitiru::audio::sfx
