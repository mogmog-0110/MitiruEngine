#pragma once

/// @file Dx12ShaderCompiler.hpp
/// @brief DX12 のシェーダーを HLSL 文字列からコンパイルする。
/// @details 既定は FXC (SM 5.x)。-DMITIRU_WITH_DXC=ON でビルドし、実行ファイルの隣の
///          dxc/ に dxcompiler.dll と dxil.dll があれば DXC (SM 6.0) を使う。
///          シェーダーのソースはどちらでも通る書き方に揃えてある。
///          -DMITIRU_WITH_SLANG=ON (ADR 0047 のスパイク) では Slang 経由の DXIL になる。

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <d3dcompiler.h>
#include <wrl/client.h>

#ifdef MITIRU_HAS_DXC
#include <dxcapi.h>
#endif

#include <mitiru/debug/WarnOnce.hpp>
#include <mitiru/gfx/ShaderCompileCount.hpp>
#include <mitiru/gfx/dx12/Dx12ShaderDiskCache.hpp>
#include <mitiru/gfx/dx12/Dx12SlangCompiler.hpp>

#pragma comment(lib, "d3dcompiler.lib")

namespace mitiru::gfx
{

/// @brief 実際にコンパイルを受け持つコンパイラ
enum class Dx12ShaderCompilerKind
{
	Fxc,  ///< d3dcompiler_47 (SM 5.x の DXBC)
	Dxc,  ///< DirectX Shader Compiler (SM 6.x の DXIL)
	Slang,  ///< Slang → DXC (SM 6.0 の DXIL)。MITIRU_WITH_SLANG のスパイク専用
};

namespace detail
{

[[nodiscard]] inline HRESULT copyToBlob(const void* data, std::size_t size, ID3DBlob** out)
{
	if (!out) { return S_OK; }
	Microsoft::WRL::ComPtr<ID3DBlob> blob;
	const HRESULT hr = D3DCreateBlob(size, blob.GetAddressOf());
	if (FAILED(hr)) { return hr; }
	if (size > 0) { std::memcpy(blob->GetBufferPointer(), data, size); }
	return blob.CopyTo(out);
}

/// @brief "vs_5_0" → "vs_6_0"。段 (vs / ps / cs) はそのまま、モデルだけ 6.0 に上げる。
[[nodiscard]] inline std::wstring dxcTarget(const char* target)
{
	const std::string_view t(target ? target : "");
	const auto stage = t.substr(0, t.find('_'));
	return std::wstring(stage.begin(), stage.end()) + L"_6_0";
}

#ifdef MITIRU_HAS_DXC

/// @brief D3DCOMPILE_* を DXC の引数へ写す
inline void appendDxcFlags(UINT flags, std::vector<LPCWSTR>& args)
{
	if (flags & D3DCOMPILE_DEBUG) { args.push_back(L"-Zi"); args.push_back(L"-Qembed_debug"); }
	if (flags & D3DCOMPILE_SKIP_OPTIMIZATION) { args.push_back(L"-Od"); }
	if (flags & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR) { args.push_back(L"-Zpr"); }
	if (flags & D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR) { args.push_back(L"-Zpc"); }
	if (flags & D3DCOMPILE_IEEE_STRICTNESS) { args.push_back(L"-Gis"); }
	if (flags & D3DCOMPILE_WARNINGS_ARE_ERRORS) { args.push_back(L"-WX"); }
}

/// @brief exe の隣の dxc/ から DXC を読む。見つからなければ available() が false。
class DxcRuntime
{
public:
	[[nodiscard]] static DxcRuntime& instance()
	{
		static DxcRuntime runtime;
		return runtime;
	}

	[[nodiscard]] bool available() const noexcept { return m_compiler != nullptr; }

	[[nodiscard]] HRESULT compile(std::string_view source, const char* entry, const char* target,
	                              UINT flags, ID3DBlob** code, ID3DBlob** errors)
	{
		const std::string entryA(entry ? entry : "main");
		const std::wstring entryW(entryA.begin(), entryA.end());
		const std::wstring targetW = dxcTarget(target);
		// HLSL 2021 は && || の短絡やベクトル条件の扱いが変わり、FXC と同じソースを通せない。
		std::vector<LPCWSTR> args = {L"-E", entryW.c_str(), L"-T", targetW.c_str(), L"-HV", L"2018"};
		appendDxcFlags(flags, args);

		const DxcBuffer buffer{source.data(), source.size(), DXC_CP_UTF8};
		Microsoft::WRL::ComPtr<IDxcResult> result;
		std::lock_guard lock(m_mutex);
		HRESULT hr = m_compiler->Compile(&buffer, args.data(), static_cast<UINT32>(args.size()),
		                                 nullptr, IID_PPV_ARGS(result.GetAddressOf()));
		if (FAILED(hr)) { return hr; }
		copyErrors(result.Get(), errors);
		if (FAILED(result->GetStatus(&hr)) || FAILED(hr)) { return FAILED(hr) ? hr : E_FAIL; }

		Microsoft::WRL::ComPtr<IDxcBlob> object;
		hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(object.GetAddressOf()), nullptr);
		if (FAILED(hr) || !object) { return FAILED(hr) ? hr : E_FAIL; }
		return copyToBlob(object->GetBufferPointer(), object->GetBufferSize(), code);
	}

private:
	DxcRuntime()
	{
		const std::wstring dir = dxcDirectory();
		// dxcompiler.dll は後から名前だけで dxil.dll を読む。先に絶対パスで読んでおかないと、
		// exe の隣や PATH にある古い dxil.dll が選ばれて署名の版が食い違う。
		if (!LoadLibraryExW((dir + L"dxil.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) { return; }
		const HMODULE dxc = LoadLibraryExW((dir + L"dxcompiler.dll").c_str(), nullptr,
		                                   LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!dxc) { return; }
		const auto create = reinterpret_cast<DxcCreateInstanceProc>(
			reinterpret_cast<void*>(GetProcAddress(dxc, "DxcCreateInstance")));
		if (!create) { return; }
		Microsoft::WRL::ComPtr<IDxcCompiler3> compiler;
		if (SUCCEEDED(create(CLSID_DxcCompiler, IID_PPV_ARGS(compiler.GetAddressOf()))))
		{
			m_compiler = compiler;
		}
	}

	[[nodiscard]] static std::wstring dxcDirectory()
	{
		wchar_t path[MAX_PATH] = {};
		const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path, len);
		dir.resize(dir.find_last_of(L"\\/") + 1);
		return dir + L"dxc\\";
	}

	static void copyErrors(IDxcResult* result, ID3DBlob** errors)
	{
		Microsoft::WRL::ComPtr<IDxcBlobUtf8> text;
		if (errors && SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(text.GetAddressOf()), nullptr))
		    && text && text->GetStringLength() > 0)
		{
			(void)copyToBlob(text->GetStringPointer(), text->GetStringLength() + 1, errors);
		}
	}

	std::mutex m_mutex;
	Microsoft::WRL::ComPtr<IDxcCompiler3> m_compiler;
};

#endif // MITIRU_HAS_DXC

} // namespace detail

/// @brief このプロセスで DX12 シェーダーをコンパイルするコンパイラ
[[nodiscard]] inline Dx12ShaderCompilerKind activeDx12ShaderCompiler()
{
#ifdef MITIRU_HAS_SLANG
	if (detail::SlangRuntime::instance().available()) { return Dx12ShaderCompilerKind::Slang; }
	debug::verboseOnce("gfx.slang.missing",
		"MITIRU_WITH_SLANG=ON ですが実行ファイルの隣の slang/ に slang.dll が見つからないので、シェーダーを Slang 以外でコンパイルします。"
		"python tools/fetch_slang.py を実行してからビルドし直すと slang/ に置かれます。");
#endif
#ifdef MITIRU_HAS_DXC
	if (detail::DxcRuntime::instance().available()) { return Dx12ShaderCompilerKind::Dxc; }
	debug::warnOnce("gfx.dxc.missing",
		"実行ファイルの隣の dxc/ に dxcompiler.dll と dxil.dll が見つからないので、シェーダーを古い FXC でコンパイルします。"
		"python tools/fetch_dxc.py を実行してからビルドし直してください。");
#endif
	return Dx12ShaderCompilerKind::Fxc;
}

namespace detail
{

[[nodiscard]] inline HRESULT compileDx12ShaderUncached(std::string_view source, const char* entry,
                                                       const char* target, UINT flags,
                                                       ID3DBlob** code, ID3DBlob** errors)
{
#ifdef MITIRU_HAS_SLANG
	if (activeDx12ShaderCompiler() == Dx12ShaderCompilerKind::Slang)
	{
		const auto out = detail::SlangRuntime::instance().compile(source, entry, target, flags);
		if (errors && !out.diagnostics.empty())
		{
			(void)detail::copyToBlob(out.diagnostics.c_str(), out.diagnostics.size() + 1, errors);
		}
		return out.ok ? detail::copyToBlob(out.code.data(), out.code.size(), code) : E_FAIL;
	}
#endif
#ifdef MITIRU_HAS_DXC
	if (activeDx12ShaderCompiler() == Dx12ShaderCompilerKind::Dxc)
	{
		return detail::DxcRuntime::instance().compile(source, entry, target, flags, code, errors);
	}
#endif
	return D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
	                  entry, target, flags, 0, code, errors);
}

} // namespace detail

/// @brief D3DCompile の DX12 版。target は SM 5 表記 ("ps_5_0") で渡し、DXC 時は 6.0 に上がる。
/// @param flags D3DCOMPILE_* (DXC 時は対応する引数へ写す)
/// @details 結果は ShaderDiskCache に残り、同じソースは次の起動からコンパイルしない
///          (残した結果から返すときは、警告の errors は返らない)。
[[nodiscard]] inline HRESULT compileDx12Shader(std::string_view source, const char* entry,
                                               const char* target, UINT flags,
                                               ID3DBlob** code, ID3DBlob** errors = nullptr)
{
	const ShaderDiskCache& cache = ShaderDiskCache::process();
	const ShaderCacheKey key = makeShaderCacheKey(source, entry ? entry : "", target ? target : "", flags,
	                                              static_cast<std::uint32_t>(activeDx12ShaderCompiler()));
	std::vector<std::uint8_t> bytes;
	if (code != nullptr && cache.load(key, bytes))
	{
		return detail::copyToBlob(bytes.data(), bytes.size(), code);
	}
	const HRESULT hr = detail::compileDx12ShaderUncached(source, entry, target, flags, code, errors);
	noteShaderCompiled();
	if (SUCCEEDED(hr) && code != nullptr && *code != nullptr)
	{
		cache.store(key, (*code)->GetBufferPointer(), (*code)->GetBufferSize());
	}
	return hr;
}

} // namespace mitiru::gfx

#endif // _WIN32
