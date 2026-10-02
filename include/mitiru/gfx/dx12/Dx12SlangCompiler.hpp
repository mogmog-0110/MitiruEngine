#pragma once

/// @file Dx12SlangCompiler.hpp
/// @brief DX12 のシェーダーを Slang 経由で DXIL (SM 6.0) にする。ADR 0047 のスパイク。
/// @details -DMITIRU_WITH_SLANG=ON のときだけ compileDx12Shader から使う。Slang は DXIL を
///          作る最後の段で DXC を呼ぶので、exe の隣の slang/ と dxc/ の両方を読む。
///          既定のコンパイラは FXC / DXC のまま。結果は docs/SLANG_SPIKE.md。

#if defined(_WIN32) && defined(MITIRU_HAS_SLANG)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <mutex>
#include <string>
#include <string_view>

#include <d3dcompiler.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace mitiru::gfx
{
namespace detail
{

/// @brief "ps_5_0" の段を Slang の段へ。知らない段は SLANG_STAGE_NONE。
[[nodiscard]] inline SlangStage slangStage(std::string_view target) noexcept
{
	const auto stage = target.substr(0, target.find('_'));
	if (stage == "vs") { return SLANG_STAGE_VERTEX; }
	if (stage == "ps") { return SLANG_STAGE_FRAGMENT; }
	if (stage == "cs") { return SLANG_STAGE_COMPUTE; }
	return SLANG_STAGE_NONE;
}

/// @brief Slang の session は既定が row-major なので、FXC / DXC と同じ column-major を明示する。
[[nodiscard]] inline SlangMatrixLayoutMode slangMatrixLayout(UINT flags) noexcept
{
	return (flags & D3DCOMPILE_PACK_MATRIX_ROW_MAJOR) ? SLANG_MATRIX_LAYOUT_ROW_MAJOR
	                                                  : SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
}

struct SlangOutput
{
	bool ok = false;
	std::string code;         ///< DXIL コンテナ
	std::string diagnostics;
};

/// @brief exe の隣の slang/ から Slang を読む。見つからなければ available() が false。
class SlangRuntime
{
public:
	[[nodiscard]] static SlangRuntime& instance()
	{
		static SlangRuntime runtime;
		return runtime;
	}

	[[nodiscard]] bool available() const noexcept { return m_global != nullptr; }

	[[nodiscard]] SlangOutput compile(std::string_view source, const char* entry, const char* target, UINT flags)
	{
		const SlangStage stage = slangStage(target ? target : "");
		if (stage == SLANG_STAGE_NONE) { return {false, {}, "Slang: unsupported shader stage"}; }
		std::lock_guard lock(m_mutex);
		Slang::ComPtr<slang::ISession> session;
		if (SLANG_FAILED(createSession(flags, session.writeRef()))) { return {false, {}, "Slang: createSession failed"}; }

		// 同じ session に同名の module を読むと前の結果が返るので、名前は毎回変える。
		const std::string name = "mitiru_shader_" + std::to_string(++m_serial);
		Slang::ComPtr<slang::IBlob> diag;
		slang::IModule* module = session->loadModuleFromSourceString(
			name.c_str(), (name + ".hlsl").c_str(), std::string(source).c_str(), diag.writeRef());
		if (!module) { return failure(diag); }

		Slang::ComPtr<slang::IEntryPoint> ep;
		if (SLANG_FAILED(module->findAndCheckEntryPoint(entry ? entry : "main", stage, ep.writeRef(),
		                                                diag.writeRef())))
		{
			return failure(diag);
		}
		return link(session, module, ep);
	}

private:
	SlangRuntime()
	{
		const std::wstring dir = exeDirectory();
		const HMODULE dll = LoadLibraryExW((dir + L"slang\\slang.dll").c_str(), nullptr,
		                                   LOAD_WITH_ALTERED_SEARCH_PATH);
		if (!dll) { return; }
		using CreateFn = SlangResult (*)(const SlangGlobalSessionDesc*, slang::IGlobalSession**);
		const auto create = reinterpret_cast<CreateFn>(
			reinterpret_cast<void*>(GetProcAddress(dll, "slang_createGlobalSession2")));
		const SlangGlobalSessionDesc desc{};
		if (!create || SLANG_FAILED(create(&desc, m_global.writeRef()))) { return; }
		m_global->setDownstreamCompilerPath(SLANG_PASS_THROUGH_DXC, toUtf8(dir + L"dxc").c_str());
	}

	[[nodiscard]] SlangResult createSession(UINT flags, slang::ISession** out)
	{
		slang::TargetDesc targetDesc{};
		targetDesc.format = SLANG_DXIL;
		targetDesc.profile = m_global->findProfile("sm_6_0");
		slang::SessionDesc desc{};
		desc.targets = &targetDesc;
		desc.targetCount = 1;
		desc.defaultMatrixLayoutMode = slangMatrixLayout(flags);
		return m_global->createSession(desc, out);
	}

	[[nodiscard]] static SlangOutput link(slang::ISession* session, slang::IModule* module, slang::IEntryPoint* ep)
	{
		slang::IComponentType* parts[] = {module, ep};
		Slang::ComPtr<slang::IComponentType> composite, linked;
		Slang::ComPtr<slang::IBlob> diag, dxil;
		if (SLANG_FAILED(session->createCompositeComponentType(parts, 2, composite.writeRef(), diag.writeRef()))
		    || SLANG_FAILED(composite->link(linked.writeRef(), diag.writeRef()))
		    || SLANG_FAILED(linked->getEntryPointCode(0, 0, dxil.writeRef(), diag.writeRef())) || !dxil)
		{
			return failure(diag);
		}
		return {true, std::string(static_cast<const char*>(dxil->getBufferPointer()), dxil->getBufferSize()), {}};
	}

	[[nodiscard]] static SlangOutput failure(slang::IBlob* diag)
	{
		if (!diag || diag->getBufferSize() == 0) { return {false, {}, "Slang: compile failed"}; }
		return {false, {}, std::string(static_cast<const char*>(diag->getBufferPointer()), diag->getBufferSize())};
	}

	[[nodiscard]] static std::wstring exeDirectory()
	{
		wchar_t path[MAX_PATH] = {};
		const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
		std::wstring dir(path, len);
		dir.resize(dir.find_last_of(L"\\/") + 1);
		return dir;
	}

	[[nodiscard]] static std::string toUtf8(const std::wstring& w)
	{
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<std::size_t>(n), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
		return out;
	}

	std::mutex m_mutex;
	unsigned m_serial = 0;
	Slang::ComPtr<slang::IGlobalSession> m_global;
};

} // namespace detail
} // namespace mitiru::gfx

#endif // _WIN32 && MITIRU_HAS_SLANG
