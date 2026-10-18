/// @file GpuPreferenceExports.hpp
/// @brief 2 つの GPU を持つノート PC で、プレイヤーが起動する exe に高性能 GPU を使わせる変数の宣言。
/// @details NVIDIA Optimus と AMD の GPU 切り替え機構は、exe がこの名前の変数を export していると、内蔵 GPU が既定でも高性能 GPU を選ぶ。
/// 1 つの exe につき、1 つの翻訳単位だけで include する。

#pragma once

#ifdef _WIN32

extern "C"
{
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

#endif
