#pragma once

/// @file Angle.hpp
/// @brief 度とラジアンを区別する `mitiru::Deg` と `mitiru::Rad` の宣言。
/// @details 描画関数には度を受けるものとラジアンを受けるものがあり、`float` では単位を取り違えてもコンパイルが通る。
/// Screen / Canvas の角度を受ける関数は Deg か Rad を受け、互いに暗黙変換できる。
/// `float` からは `Deg{30}` / `Rad{0.5f}` のように明示して作り、`float` へは `.degrees()` / `.radians()` で取り出す。

namespace mitiru
{

inline constexpr float kAnglePi = 3.14159265358979323846f;

struct Rad;

struct Deg
{
	float value = 0.0f;

	constexpr Deg() noexcept = default;
	constexpr explicit Deg(float degrees) noexcept : value(degrees) {}
	constexpr Deg(Rad r) noexcept;

	[[nodiscard]] constexpr float degrees() const noexcept { return value; }
	[[nodiscard]] constexpr float radians() const noexcept { return value * (kAnglePi / 180.0f); }

	friend constexpr Deg operator+(Deg a, Deg b) noexcept { return Deg{a.value + b.value}; }
	friend constexpr Deg operator-(Deg a, Deg b) noexcept { return Deg{a.value - b.value}; }
	friend constexpr Deg operator-(Deg a) noexcept { return Deg{-a.value}; }
	friend constexpr Deg operator*(Deg a, float k) noexcept { return Deg{a.value * k}; }
	friend constexpr Deg operator*(float k, Deg a) noexcept { return Deg{a.value * k}; }
	friend constexpr Deg operator/(Deg a, float k) noexcept { return Deg{a.value / k}; }
	friend constexpr bool operator==(Deg a, Deg b) noexcept { return a.value == b.value; }
	friend constexpr bool operator<(Deg a, Deg b) noexcept { return a.value < b.value; }
};

struct Rad
{
	float value = 0.0f;

	constexpr Rad() noexcept = default;
	constexpr explicit Rad(float radians) noexcept : value(radians) {}
	constexpr Rad(Deg d) noexcept : value(d.radians()) {}

	[[nodiscard]] constexpr float radians() const noexcept { return value; }
	[[nodiscard]] constexpr float degrees() const noexcept { return value * (180.0f / kAnglePi); }

	friend constexpr Rad operator+(Rad a, Rad b) noexcept { return Rad{a.value + b.value}; }
	friend constexpr Rad operator-(Rad a, Rad b) noexcept { return Rad{a.value - b.value}; }
	friend constexpr Rad operator-(Rad a) noexcept { return Rad{-a.value}; }
	friend constexpr Rad operator*(Rad a, float k) noexcept { return Rad{a.value * k}; }
	friend constexpr Rad operator*(float k, Rad a) noexcept { return Rad{a.value * k}; }
	friend constexpr Rad operator/(Rad a, float k) noexcept { return Rad{a.value / k}; }
	friend constexpr bool operator==(Rad a, Rad b) noexcept { return a.value == b.value; }
	friend constexpr bool operator<(Rad a, Rad b) noexcept { return a.value < b.value; }
};

constexpr Deg::Deg(Rad r) noexcept : value(r.degrees()) {}

}  // namespace mitiru

/// `float` で角度を受ける古い版に付ける。今のゲームを警告つきでビルドできるよう、削除せず残す (ADR 0072)。
#define MITIRU_DEPRECATED_RADIANS \
	[[deprecated("この float の角度はラジアン。mitiru::Rad{1.57f} か mitiru::Deg{90} で渡す (Deg と Rad は互いに変換される)")]]
#define MITIRU_DEPRECATED_DEGREES \
	[[deprecated("この float の角度は度。mitiru::Deg{90} か mitiru::Rad{1.57f} で渡す (Deg と Rad は互いに変換される)")]]
