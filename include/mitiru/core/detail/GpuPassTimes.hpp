// mitiru::Engine の detail ヘッダー。直接 include せず、core/Engine.hpp 経由で include する
#pragma once

/// @file GpuPassTimes.hpp
/// @brief perf のツール窓に出す、パスごとの GPU 時間 (Renderer3D_DX12 の Dx12GpuTimer が測った値)。
/// @details 後処理の各パスはメインのコマンドリストの中で測るので、main の内訳として part = true を付ける。
///          測っていないパス (0 ms) は出さない。3D の描画が無い game では null を返し、節ごと出さない。

#include <array>
#include <utility>

#include <nlohmann/json.hpp>

#include <mitiru/render/IRenderer3D.hpp>
#include <mitiru/render/PostEffectSettings.hpp>
#ifdef _WIN32
#include <mitiru/render/Renderer3D_DX12.hpp>
#endif

namespace mitiru::detail
{

[[nodiscard]] inline nlohmann::json gpuPassTimesJson(const render::IRenderer3D* renderer)
{
#ifdef _WIN32
	const auto* dx12 = dynamic_cast<const render::Renderer3D_DX12*>(renderer);
	if (dx12 == nullptr) { return nullptr; }
	const auto frame = dx12->gpuFrameTimes();
	if (!(frame.totalMs > 0.0)) { return nullptr; }
	using P = render::PostGpuPass;
	static constexpr std::array<std::pair<P, const char*>, 10> kParts = { {
		{ P::Sky, "sky" }, { P::VolumetricFog, "fog" }, { P::AerialComposite, "atmosphere" },
		{ P::Velocity, "velocity" }, { P::Trails, "trails" }, { P::AmbientOcclusion, "ao" },
		{ P::AntiAliasing, "aa" }, { P::MotionBlur, "motionBlur" }, { P::UpscaleInputs, "upscaleInputs" },
		{ P::Upscale, "upscale" },
	} };
	nlohmann::json passes = nlohmann::json::array();
	const auto add = [&passes](const char* name, double ms, bool part) {
		if (ms > 0.0) { passes.push_back({ { "name", name }, { "ms", ms }, { "part", part } }); }
	};
	add("main", frame.mainMs, false);
	for (const auto& [pass, name] : kParts) { add(name, dx12->postGpuMilliseconds(pass), true); }
	add("lights", frame.auxMs, false);
	return { { "totalMs", frame.totalMs }, { "passes", std::move(passes) } };
#else
	(void)renderer;
	return nullptr;
#endif
}

}  // namespace mitiru::detail
