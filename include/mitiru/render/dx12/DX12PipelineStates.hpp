#pragma once

/// @file DX12PipelineStates.hpp
/// @brief Renderer3D_DX12のPSO生成・リソース生成メソッド群
/// @details このファイルはRenderer3D_DX12クラス定義内でインクルードされる。
///          単独でインクルードしないこと。

// NOTE: このファイルはRenderer3D_DX12のprivateセクション内で
//       #include される設計のため、#pragma once以外のガードは不要。
//
// Detail-include hub for Renderer3D_DX12 pipeline state member fns.
// Included from inside the body of class Renderer3D_DX12 at Renderer3D_DX12.hpp.
// Each sub-file is a .inl-style class-body chunk, NOT a standalone header.

// NOLINTBEGIN(build/include)
#include <mitiru/render/dx12/detail/DX12PipelineStates_Setup.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_ForwardRender.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Overlay.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_PostProcess.inl>
#include <mitiru/render/dx12/detail/DX12PipelineStates_Shadow.inl>
// NOLINTEND(build/include)
