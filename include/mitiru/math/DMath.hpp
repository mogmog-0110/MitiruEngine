/// @file DMath.hpp
/// @brief CPU と Windows のバージョンに結果が左右されない数学関数 (float)。
///
/// `std::sin` などは UCRT が CPU の命令セット (FMA3 の有無) と Windows のバージョンで実装を選ぶので、
/// 別の PC で同じ `.mtrr` を再生すると結果が変わりうる (docs/DETERMINISM.md)。シミュレーションの
/// 状態に入る計算 (GameMemory を更新する側) では、`std::sin` の代わりにこれを使う。描画だけに
/// 使う値は std のままでよい。
///
/// 実装は openlibm (FreeBSD msun 由来、FDLIBM) の float 用で、足し算・掛け算・割り算・sqrt と整数
/// 演算だけで決まる。融合 (FMA) は MSVC で止めてあるので、/arch:AVX2 のゲームでも同じ値になる。
/// clang / gcc では融合を止めるマーカーを付けていない (未検証)。
/// 精度は 1 ulp 前後で、correctly rounded ではない。std との差は tests/mitiru/TestDMath.cpp が測る。
///
/// 丸めは最近接偶数、MXCSR の FTZ / DAZ は無し、が前提。これらを変えるライブラリを呼んだ後は、値が変わる。
///
/// double は受け付けない。double で呼ぶと FMA3 の違いが 1% 近い入力で出るので (docs/DETERMINISM.md)、
/// float にしてから呼ぶ。
#pragma once

#include <cmath>

#include <mitiru/math/detail/DMathExpLog.hpp>
#include <mitiru/math/detail/DMathPow.hpp>
#include <mitiru/math/detail/DMathTrig.hpp>

namespace mitiru::math
{

[[nodiscard]] inline float dsin(float x) { return detail::sinImpl(x); }
[[nodiscard]] inline float dcos(float x) { return detail::cosImpl(x); }
[[nodiscard]] inline float dtan(float x) { return detail::tanImpl(x); }
[[nodiscard]] inline float datan(float x) { return detail::atanImpl(x); }
[[nodiscard]] inline float datan2(float y, float x) { return detail::atan2Impl(y, x); }
[[nodiscard]] inline float dexp(float x) { return detail::expImpl(x); }
[[nodiscard]] inline float dlog(float x) { return detail::logImpl(x); }
[[nodiscard]] inline float dpow(float x, float y) { return detail::powImpl(x, y); }

/// sqrt は IEEE 754 で丸めが 1 通りに決まる (SSE の sqrtss) ので、std のままで同じ値になる
[[nodiscard]] inline float dsqrt(float x) { return std::sqrt(x); }

double dsin(double) = delete;
double dcos(double) = delete;
double dtan(double) = delete;
double datan(double) = delete;
double datan2(double, double) = delete;
double dexp(double) = delete;
double dlog(double) = delete;
double dpow(double, double) = delete;

}  // namespace mitiru::math
