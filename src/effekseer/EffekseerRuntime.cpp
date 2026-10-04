// render/dx12/EffekseerRuntime.hpp の実装。Effekseer のヘッダはこの翻訳単位にだけ入る。

#include "mitiru/render/dx12/EffekseerRuntime.hpp"

#include <mitiru/debug/ConsoleOut.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include <Effekseer.h>

#include "EffekseerRendererDX12/EffekseerRenderer/EffekseerRendererDX12.Renderer.h"
#include "EffekseerRendererLLGI/Common.h"
#include "EffekseerRendererCommon/TextureLoader.h"
#include "EffekseerRendererLLGI/EffekseerRendererLLGI.RendererImplemented.h"
#include "3rdParty/LLGI/src/DX12/LLGI.BaseDX12.h"

namespace mitiru::render::fx
{
namespace
{

constexpr int kMaxInstances = 8000;
constexpr int kMaxSquares = 8000;
constexpr float kFramesPerSec = 60.0f;   // Effekseer の時間の単位 (60fps 基準のフレーム)
constexpr std::size_t kRequestReserve = 256;
constexpr std::uint64_t kHashBasis = 14695981039346656037ull;
constexpr std::uint64_t kHashPrime = 1099511628211ull;

/// @brief Create は MSAA の sample 数を受け取らないので、LLGI の render pass のキーを差し替える
void useSampleCount(const EffekseerRenderer::RendererRef& renderer, const EffekseerTarget& t)
{
	auto impl = renderer.DownCast<EffekseerRendererLLGI::RendererImplemented>();
	LLGI::RenderPassPipelineStateKey key;
	key.RenderTargetFormats.resize(1);
	key.RenderTargetFormats.at(0) = LLGI::ConvertFormat(t.colorFormat);
	key.DepthFormat = LLGI::ConvertFormat(t.depthFormat);
	key.SamplingCount = t.sampleCount;
	impl->ChangeRenderPassPipelineState(key);
}

[[nodiscard]] std::u16string toUtf16(const char* utf8)
{
	std::u16string out(std::strlen(utf8) + 1, u'\0');
	const int32_t n = Effekseer::ConvertUtf8ToUtf16(out.data(), static_cast<int32_t>(out.size()), utf8);
	out.resize(static_cast<std::size_t>(n > 0 ? n : 0));
	return out;
}

/// @brief FNV-1a 64。文字列の後ろに区切りを 1 つ混ぜ、("ab","c") と ("a","bc") を別にする
[[nodiscard]] std::uint64_t hashMix(std::uint64_t h, const char* s) noexcept
{
	for (; s != nullptr && *s != '\0'; ++s) h = (h ^ static_cast<std::uint8_t>(*s)) * kHashPrime;
	return (h ^ 0xFFu) * kHashPrime;
}

/// @brief 同じ id には同じ乱数列を使う (同じ経過秒なら同じ姿になる)
[[nodiscard]] int32_t seedOf(std::uint64_t id) noexcept
{
	return static_cast<int32_t>((id ^ (id >> 32)) & 0x7FFFFFFFu);
}

/// @brief このフレームに積まれた 1 本。毎フレーム積むので確保しない形 (固定長の path) にしてある
struct Request
{
	std::uint64_t base = 0;       ///< path と key
	std::uint64_t id = 0;         ///< base と「このフレームで同じ base が何本目か」
	std::uint64_t pathHash = 0;
	std::array<char, 260> path{};
	sgc::Vec3f position;
	float rotYDeg = 0.0f;
	float scale = 1.0f;
	float frame = 0.0f;
	int pass = 0;
};

struct Live
{
	Effekseer::Handle handle = -1;
	float frame = 0.0f;
	bool used = false;
	std::uint32_t passMask = 0;   ///< このフレームに出す描く先の集合
};

class EffekseerRuntimeImpl final : public EffekseerRuntime
{
public:
	[[nodiscard]] bool init(ID3D12Device* device, ID3D12CommandQueue* queue, const EffekseerTarget& t, std::string& error)
	{
		m_device = EffekseerRendererDX12::CreateGraphicsDevice(device, queue, t.framesInFlight);
		if (m_device == nullptr) { error = "Effekseer の DX12 デバイスを作れない"; return false; }
		DXGI_FORMAT color = t.colorFormat;
		m_renderer = EffekseerRendererDX12::Create(m_device, &color, 1, t.depthFormat, false, kMaxSquares);
		if (m_renderer == nullptr) { error = "EffekseerRendererDX12 を作れない"; return false; }
		if (t.sampleCount > 1) useSampleCount(m_renderer, t);
		m_pool = EffekseerRenderer::CreateSingleFrameMemoryPool(m_renderer->GetGraphicsDevice());
		m_commands[0] = EffekseerRenderer::CreateCommandList(m_renderer->GetGraphicsDevice(), m_pool);
		m_manager = Effekseer::Manager::Create(kMaxInstances);
		m_manager->SetSpriteRenderer(m_renderer->CreateSpriteRenderer());
		m_manager->SetRibbonRenderer(m_renderer->CreateRibbonRenderer());
		m_manager->SetRingRenderer(m_renderer->CreateRingRenderer());
		m_manager->SetTrackRenderer(m_renderer->CreateTrackRenderer());
		m_manager->SetModelRenderer(m_renderer->CreateModelRenderer());
		// 主パスの色は線形の HDR なので、テクスチャは sRGB として読み、エフェクトに書いた色も線形にして足す
		m_renderer->SetMaintainGammaColorInLinearColorSpace(true);
		m_manager->SetTextureLoader(EffekseerRenderer::CreateTextureLoader(
			m_renderer->GetGraphicsDevice(), nullptr, Effekseer::ColorSpaceType::Linear));
		m_manager->SetModelLoader(m_renderer->CreateModelLoader());
		m_manager->SetMaterialLoader(m_renderer->CreateMaterialLoader());
		m_manager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());
		m_requests.reserve(kRequestReserve);
		return true;
	}

	bool preload(const char* path) override
	{
		if (path == nullptr) return false;
		const std::size_t len = std::strlen(path);
		Request r;
		if (len >= r.path.size()) return false;
		std::memcpy(r.path.data(), path, len + 1);
		r.pathHash = hashMix(kHashBasis, path);
		return effectFor(r) != nullptr;
	}

	void draw(const char* path, const sgc::Vec3f& position, float rotYDeg, float scale, const char* key,
		float ageSec, int pass) override
	{
		if (path == nullptr || !(ageSec >= 0.0f) || pass < 0 || pass >= kMaxPasses) return;
		const std::size_t len = std::strlen(path);
		Request r;
		if (len >= r.path.size()) return;
		std::memcpy(r.path.data(), path, len + 1);
		r.pathHash = hashMix(kHashBasis, path);
		r.base = hashMix(r.pathHash, key);
		// 何本目かは描く先ごとに数える。ビューごとに同じ世界を描くと、同じ要求が描く先の数だけ来るため
		std::uint64_t nth = 0;
		for (const Request& q : m_requests) nth += (q.base == r.base && q.pass == pass) ? 1u : 0u;
		r.id = (r.base ^ nth) * kHashPrime;
		r.position = position;
		r.rotYDeg = rotYDeg;
		r.scale = scale;
		r.frame = std::floor(ageSec * kFramesPerSec);
		r.pass = pass;
		m_requests.push_back(r);
	}

	void advance() override
	{
		advanceToRequests();
		m_requests.clear();
		m_frameOpen = !m_live.empty();
		if (m_frameOpen) m_pool->NewFrame();
	}

	void render(ID3D12GraphicsCommandList* cmd, const EffectCamera& camera, int pass) override
	{
		if (!m_frameOpen || cmd == nullptr || pass < 0 || pass >= kMaxPasses) return;
		const std::uint32_t bit = 1u << pass;
		bool any = false;
		for (const auto& [id, live] : m_live) any = any || (live.passMask & bit) != 0;
		if (!any) return;
		Effekseer::RefPtr<EffekseerRenderer::CommandList>& commands = commandsFor(pass);
		if (commands == nullptr) return;

		Effekseer::Matrix44 proj, view;
		proj.PerspectiveFovRH(camera.fovYRad, camera.aspect, camera.nearZ, camera.farZ);
		view.LookAtRH({camera.eye.x, camera.eye.y, camera.eye.z}, {camera.target.x, camera.target.y, camera.target.z},
			{camera.up.x, camera.up.y, camera.up.z});
		m_renderer->SetProjectionMatrix(proj);
		m_renderer->SetCameraMatrix(view);

		EffekseerRendererDX12::BeginCommandList(commands, cmd);
		m_renderer->SetCommandList(commands);
		m_renderer->BeginRendering();
		Effekseer::Manager::DrawParameter dp;
		dp.ZNear = 0.0f;
		dp.ZFar = 1.0f;
		dp.ViewProjectionMatrix = m_renderer->GetCameraProjectionMatrix();
		for (const auto& [id, live] : m_live)
		{
			if ((live.passMask & bit) != 0 && live.handle >= 0) m_manager->DrawHandle(live.handle, dp);
		}
		m_renderer->EndRendering();
		m_renderer->SetCommandList(nullptr);
		EffekseerRendererDX12::EndCommandList(commands);
	}

	~EffekseerRuntimeImpl() override
	{
		if (m_manager != nullptr) m_manager->StopAllEffects();
		m_live.clear();
		m_effects.clear();
		m_manager.Reset();
		for (auto& c : m_commands) c.Reset();
		m_pool.Reset();
		m_renderer.Reset();
		m_device.Reset();
	}

private:
	/// @brief LLGI のコマンドリストは Begin ごとにフレームの区画を進めるので、描く先ごとに別のものを使う
	[[nodiscard]] Effekseer::RefPtr<EffekseerRenderer::CommandList>& commandsFor(int pass)
	{
		auto& c = m_commands[static_cast<std::size_t>(pass)];
		if (c == nullptr) c = EffekseerRenderer::CreateCommandList(m_renderer->GetGraphicsDevice(), m_pool);
		return c;
	}

	[[nodiscard]] Effekseer::EffectRef effectFor(const Request& r)
	{
		const auto it = m_effects.find(r.pathHash);
		if (it != m_effects.end()) return it->second;
		const std::u16string u16 = toUtf16(r.path.data());
		Effekseer::EffectRef e = Effekseer::Effect::Create(m_manager, u16.c_str());
		if (e == nullptr)
		{
			console::noticef("エフェクト %s を読めません。パスと、Effekseer で書き出したファイルかを確かめてください。", r.path.data());
		}
		m_effects.emplace(r.pathHash, e);   // 読めなかったものも覚え、毎フレーム開き直さない
		return e;
	}

	/// @brief 積まれた要求ごとに、続きなら差分だけ、時刻が戻ったか初めてなら作り直して頭から進める
	void advanceToRequests()
	{
		for (auto& [id, live] : m_live)
		{
			live.used = false;
			live.passMask = 0;
		}
		for (const Request& r : m_requests) advanceOne(r);
		for (auto it = m_live.begin(); it != m_live.end();)
		{
			if (it->second.used) { ++it; continue; }
			m_manager->StopEffect(it->second.handle);
			it = m_live.erase(it);
		}
	}

	void advanceOne(const Request& r)
	{
		Live& live = m_live[r.id];
		live.passMask |= 1u << r.pass;
		if (live.used) return;   // 同じエフェクトを別の描く先にも積んだ。進めるのは 1 回だけ
		live.used = true;
		if (live.handle < 0 || r.frame < live.frame)
		{
			const Effekseer::EffectRef effect = effectFor(r);
			if (effect == nullptr) return;
			if (live.handle >= 0) m_manager->StopEffect(live.handle);
			live.handle = m_manager->Play(effect, r.position.x, r.position.y, r.position.z);
			m_manager->SetRandomSeed(live.handle, seedOf(r.id));
			live.frame = 0.0f;
		}
		m_manager->SetLocation(live.handle, r.position.x, r.position.y, r.position.z);
		m_manager->SetRotation(live.handle, 0.0f, r.rotYDeg * 3.14159265f / 180.0f, 0.0f);
		m_manager->SetScale(live.handle, r.scale, r.scale, r.scale);
		// 1 フレームずつ、毎回 BeginUpdate / EndUpdate で区切って進める。毎フレーム 1 つ進めた場合と
		// 同じ手順になるので、頭から一気に進めても、少しずつ進めても、同じ経過秒なら同じ姿になる
		for (; live.frame < r.frame; live.frame += 1.0f)
		{
			if (!m_manager->Exists(live.handle)) break;   // 尺を過ぎて終わった
			m_manager->BeginUpdate();
			m_manager->UpdateHandle(live.handle, 1.0f);
			m_manager->EndUpdate();
		}
		live.frame = r.frame;
	}

	Effekseer::Backend::GraphicsDeviceRef m_device;
	EffekseerRenderer::RendererRef m_renderer;
	Effekseer::RefPtr<EffekseerRenderer::SingleFrameMemoryPool> m_pool;
	std::array<Effekseer::RefPtr<EffekseerRenderer::CommandList>, kMaxPasses> m_commands;
	bool m_frameOpen = false;
	Effekseer::ManagerRef m_manager;
	std::unordered_map<std::uint64_t, Effekseer::EffectRef> m_effects;
	std::unordered_map<std::uint64_t, Live> m_live;
	std::vector<Request> m_requests;
};

} // namespace

std::unique_ptr<EffekseerRuntime> EffekseerRuntime::create(ID3D12Device* device, ID3D12CommandQueue* queue,
	const EffekseerTarget& target, std::string& error)
{
	if (device == nullptr || queue == nullptr) { error = "DX12 デバイスが無い"; return nullptr; }
	auto rt = std::make_unique<EffekseerRuntimeImpl>();
	if (!rt->init(device, queue, target, error)) return nullptr;
	return rt;
}

} // namespace mitiru::render::fx
