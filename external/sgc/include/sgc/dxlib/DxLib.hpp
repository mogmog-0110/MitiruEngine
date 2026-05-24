#pragma once

/// @file DxLib.hpp
/// @brief sgc DxLibアダプター層のアンブレラヘッダ
///
/// sgcの型やパターンをDxLibで使用するためのアダプター群を
/// 一括でインクルードする。
///
/// @note DxLib.hに依存するため、DxLib環境でのみ使用可能。
///
/// @code
/// #include <DxLib.h>
/// #include "sgc/dxlib/DxLib.hpp"
/// @endcode

#include "sgc/dxlib/TypeConvert.hpp"
#include "sgc/dxlib/DxLibRenderer.hpp"
#include "sgc/dxlib/DxLibInputProvider.hpp"
#include "sgc/dxlib/DxLibTextRenderer.hpp"
#include "sgc/dxlib/DxLibSceneAdapter.hpp"
