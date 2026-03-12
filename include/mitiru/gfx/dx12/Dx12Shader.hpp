#pragma once

/// @file Dx12Shader.hpp
/// @brief DirectX 12シェーダー実装
/// @details コンパイル済みDXIL/DXBCバイトコードを保持するIShader実装。
///          D3D12ではPSO生成時にバイトコードを渡すため、シェーダーオブジェクトは
///          バイトコードのコンテナとして機能する。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/gfx/IShader.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 12シェーダー実装
/// @details コンパイル済みHLSLバイトコードを保持する。
///          D3D12ではID3D12PipelineState生成時にバイトコードを渡すため、
///          DX11のようなシェーダーオブジェクトは不要。
///
/// @code
/// auto vs = Dx12Shader::createVertexShader(hlslSource);
/// auto ps = Dx12Shader::createPixelShader(hlslSource);
/// // PSO生成時に vs.bytecode(), ps.bytecode() を使用
/// @endcode
class Dx12Shader final : public IShader
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief 頂点シェーダーを生成するファクトリ
	/// @param hlslSource HLSL文字列
	/// @param entryPoint エントリーポイント名
	/// @return 生成されたDx12Shader
	[[nodiscard]] static Dx12Shader createVertexShader(
		std::string_view hlslSource,
		std::string_view entryPoint = "VSMain")
	{
		Dx12Shader shader;
		shader.m_type = ShaderType::Vertex;

		auto blob = compileHLSL(hlslSource, entryPoint, "vs_5_0");
		shader.m_bytecode.resize(blob->GetBufferSize());
		std::memcpy(shader.m_bytecode.data(),
		            blob->GetBufferPointer(),
		            blob->GetBufferSize());

		return shader;
	}

	/// @brief ピクセルシェーダーを生成するファクトリ
	/// @param hlslSource HLSL文字列
	/// @param entryPoint エントリーポイント名
	/// @return 生成されたDx12Shader
	[[nodiscard]] static Dx12Shader createPixelShader(
		std::string_view hlslSource,
		std::string_view entryPoint = "PSMain")
	{
		Dx12Shader shader;
		shader.m_type = ShaderType::Pixel;

		auto blob = compileHLSL(hlslSource, entryPoint, "ps_5_0");
		shader.m_bytecode.resize(blob->GetBufferSize());
		std::memcpy(shader.m_bytecode.data(),
		            blob->GetBufferPointer(),
		            blob->GetBufferSize());

		return shader;
	}

	/// @brief プリコンパイル済みバイトコードから生成するファクトリ
	/// @param bytecode コンパイル済みバイトコード
	/// @param shaderType シェーダー種別
	/// @return 生成されたDx12Shader
	[[nodiscard]] static Dx12Shader createFromBytecode(
		std::span<const std::uint8_t> bytecode,
		ShaderType shaderType)
	{
		Dx12Shader shader;
		shader.m_type = shaderType;
		shader.m_bytecode.assign(bytecode.begin(), bytecode.end());
		return shader;
	}

	/// @brief シェーダー種別を取得する
	[[nodiscard]] ShaderType type() const noexcept override
	{
		return m_type;
	}

	/// @brief コンパイル済みバイトコードを取得する
	/// @return バイトコードのconst参照
	[[nodiscard]] const std::vector<std::uint8_t>& bytecode() const noexcept
	{
		return m_bytecode;
	}

	/// @brief D3D12_SHADER_BYTECODE構造体を取得する
	/// @return バイトコードとサイズのペア
	[[nodiscard]] D3D12_SHADER_BYTECODE shaderBytecode() const noexcept
	{
		return D3D12_SHADER_BYTECODE{
			m_bytecode.data(),
			m_bytecode.size()
		};
	}

private:
	/// @brief デフォルトコンストラクタ（ファクトリからのみ使用）
	Dx12Shader() = default;

	/// @brief HLSL文字列をコンパイルする
	/// @param source HLSL文字列
	/// @param entryPoint エントリーポイント名
	/// @param target コンパイルターゲット（例: "vs_5_0"）
	/// @return コンパイル済みBlob
	[[nodiscard]] static ComPtr<ID3DBlob> compileHLSL(
		std::string_view source,
		std::string_view entryPoint,
		const char* target)
	{
		ComPtr<ID3DBlob> shaderBlob;
		ComPtr<ID3DBlob> errorBlob;

		UINT compileFlags = 0;
#ifdef _DEBUG
		compileFlags |= D3DCOMPILE_DEBUG;
		compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		const std::string ep(entryPoint);
		HRESULT hr = D3DCompile(
			source.data(),
			source.size(),
			nullptr,
			nullptr,
			nullptr,
			ep.c_str(),
			target,
			compileFlags,
			0,
			shaderBlob.GetAddressOf(),
			errorBlob.GetAddressOf());

		if (FAILED(hr))
		{
			std::string errorMsg = "Dx12Shader: D3DCompile failed";
			if (errorBlob)
			{
				errorMsg += ": ";
				errorMsg += static_cast<const char*>(
					errorBlob->GetBufferPointer());
			}
			throw std::runtime_error(errorMsg);
		}

		return shaderBlob;
	}

	std::vector<std::uint8_t> m_bytecode;       ///< コンパイル済みバイトコード
	ShaderType m_type = ShaderType::Vertex;      ///< シェーダー種別
};

} // namespace mitiru::gfx

#endif // _WIN32
