#pragma once

/// @file Siv3D.hpp
/// @brief sgc Siv3Dアダプター層のアンブレラヘッダ
///
/// sgcの型やパターンをSiv3Dで使用するためのアダプター群を
/// 一括でインクルードする。
///
/// @code
/// #include <Siv3D.hpp>
/// #include "sgc/siv3d/Siv3D.hpp"
/// @endcode

#include "sgc/siv3d/TypeConvert.hpp"
#include "sgc/siv3d/SceneAdapter.hpp"
#include "sgc/siv3d/Siv3DRenderer.hpp"
#include "sgc/siv3d/Siv3DInputProvider.hpp"
#include "sgc/siv3d/Siv3DTextRenderer.hpp"
#include "sgc/siv3d/Siv3DGamepadProvider.hpp"
