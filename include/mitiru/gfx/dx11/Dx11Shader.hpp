#pragma once

/// @file Dx11Shader.hpp
/// @brief DirectX 11シェーダー実装
/// @details D3DCompileによるランタイムHLSLコンパイルと、
///          ID3D11VertexShader / ID3D11PixelShader の管理を行う。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

#include <mitiru/gfx/IShader.hpp>

namespace mitiru::gfx
{

/// @brief DirectX 11シェーダー実装
/// @details HLSL文字列をランタイムコンパイルし、VS/PSを保持する。
///          コンパイル済みバイトコードも保持する（InputLayout生成用）。
class Dx11Shader final : public IShader
{
public:
	/// @brief ComPtrエイリアス
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

	/// @brief 頂点シェーダーを生成するファクトリ
	/// @param device D3D11デバイス
	/// @param hlslSource HLSL文字列
	/// @param entryPoint エントリーポイント名
	/// @return 生成されたDx11Shader
	[[nodiscard]] static Dx11Shader createVertexShader(
		ID3D11Device* device,
		std::string_view hlslSource,
		std::string_view entryPoint = "VSMain")
	{
		Dx11Shader shader;
		shader.m_type = ShaderType::Vertex;

		/// HLSLをコンパイルする
		auto blob = compileHLSL(hlslSource, entryPoint, "vs_5_0");

		/// バイトコードを保存（InputLayout生成用）
		shader.m_bytecode.resize(blob->GetBufferSize());
		std::memcpy(shader.m_bytecode.data(),
		            blob->GetBufferPointer(),
		            blob->GetBufferSize());

		/// 頂点シェーダーを生成する
		HRESULT hr = device->CreateVertexShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr,
			shader.m_vertexShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Shader: CreateVertexShader failed");
		}

		return shader;
	}

	/// @brief ピクセルシェーダーを生成するファクトリ
	/// @param device D3D11デバイス
	/// @param hlslSource HLSL文字列
	/// @param entryPoint エントリーポイント名
	/// @return 生成されたDx11Shader
	[[nodiscard]] static Dx11Shader createPixelShader(
		ID3D11Device* device,
		std::string_view hlslSource,
		std::string_view entryPoint = "PSMain")
	{
		Dx11Shader shader;
		shader.m_type = ShaderType::Pixel;

		/// HLSLをコンパイルする
		auto blob = compileHLSL(hlslSource, entryPoint, "ps_5_0");

		/// ピクセルシェーダーを生成する
		HRESULT hr = device->CreatePixelShader(
			blob->GetBufferPointer(),
			blob->GetBufferSize(),
			nullptr,
			shader.m_pixelShader.GetAddressOf());
		if (FAILED(hr))
		{
			throw std::runtime_error(
				"Dx11Shader: CreatePixelShader failed");
		}

		return shader;
	}

	/// @brief シェーダー種別を取得する
	[[nodiscard]] ShaderType type() const noexcept override
	{
		return m_type;
	}

	/// @brief 頂点シェーダーを取得する
	[[nodiscard]] ID3D11VertexShader* getVertexShader() const noexcept
	{
		return m_vertexShader.Get();
	}

	/// @brief ピクセルシェーダーを取得する
	[[nodiscard]] ID3D11PixelShader* getPixelShader() const noexcept
	{
		return m_pixelShader.Get();
	}

	/// @brief コンパイル済みバイトコードを取得する（InputLayout生成用）
	/// @return バイトコードへのspan
	[[nodiscard]] const std::vector<std::uint8_t>& bytecode() const noexcept
	{
		return m_bytecode;
	}

private:
	/// @brief デフォルトコンストラクタ（ファクトリからのみ使用）
	Dx11Shader() = default;

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
			std::string errorMsg = "Dx11Shader: D3DCompile failed";
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

	ComPtr<ID3D11VertexShader> m_vertexShader;  ///< 頂点シェーダー
	ComPtr<ID3D11PixelShader> m_pixelShader;    ///< ピクセルシェーダー
	std::vector<std::uint8_t> m_bytecode;        ///< VSバイトコード
	ShaderType m_type = ShaderType::Vertex;      ///< シェーダー種別
};

} // namespace mitiru::gfx

#endif // _WIN32
