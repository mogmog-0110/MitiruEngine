#include <mitiru/ui_rml/dx12/RmlDx12Pipelines.hpp>
#include <mitiru/ui_rml/dx12/RmlDx12Shaders.hpp>

#include <mitiru/gfx/dx12/Dx12ShaderCompiler.hpp>

#include <cstring>

namespace mitiru::ui_rml::dx12
{

using Microsoft::WRL::ComPtr;

namespace
{

enum class Vs : std::uint8_t { Geometry, Fullscreen, Blur, Count };
enum class Ps : std::uint8_t { Color, Texture, Gradient, Copy, Sample, ColorMatrix, BlendMask, Blur, DropShadow, Count };
enum class Target : std::uint8_t { Layer, Post };
enum class Blend : std::uint8_t { Over, Replace, NoColor };
enum class Stencil : std::uint8_t { Off, Test, WriteReplace, WriteIncrement };

struct PsoDef
{
	RmlPso id;
	Vs vs;
	Ps ps;
	Target target;
	Blend blend;
	Stencil stencil;
};

// clang-format off
constexpr PsoDef kPsoDefs[] = {
	{ RmlPso::Color,           Vs::Geometry,   Ps::Color,       Target::Layer, Blend::Over,    Stencil::Off },
	{ RmlPso::ColorClip,       Vs::Geometry,   Ps::Color,       Target::Layer, Blend::Over,    Stencil::Test },
	{ RmlPso::Texture,         Vs::Geometry,   Ps::Texture,     Target::Layer, Blend::Over,    Stencil::Off },
	{ RmlPso::TextureClip,     Vs::Geometry,   Ps::Texture,     Target::Layer, Blend::Over,    Stencil::Test },
	{ RmlPso::Gradient,        Vs::Geometry,   Ps::Gradient,    Target::Layer, Blend::Over,    Stencil::Off },
	{ RmlPso::GradientClip,    Vs::Geometry,   Ps::Gradient,    Target::Layer, Blend::Over,    Stencil::Test },
	{ RmlPso::MaskReplace,     Vs::Geometry,   Ps::Color,       Target::Layer, Blend::NoColor, Stencil::WriteReplace },
	{ RmlPso::MaskIncrement,   Vs::Geometry,   Ps::Color,       Target::Layer, Blend::NoColor, Stencil::WriteIncrement },
	{ RmlPso::LayerOver,       Vs::Fullscreen, Ps::Copy,        Target::Layer, Blend::Over,    Stencil::Off },
	{ RmlPso::LayerOverClip,   Vs::Fullscreen, Ps::Copy,        Target::Layer, Blend::Over,    Stencil::Test },
	{ RmlPso::LayerReplace,    Vs::Fullscreen, Ps::Copy,        Target::Layer, Blend::Replace, Stencil::Off },
	{ RmlPso::LayerReplaceClip,Vs::Fullscreen, Ps::Copy,        Target::Layer, Blend::Replace, Stencil::Test },
	{ RmlPso::PostCopy,        Vs::Fullscreen, Ps::Copy,        Target::Post,  Blend::Replace, Stencil::Off },
	{ RmlPso::PostCopyOver,    Vs::Fullscreen, Ps::Copy,        Target::Post,  Blend::Over,    Stencil::Off },
	{ RmlPso::PostSample,      Vs::Fullscreen, Ps::Sample,      Target::Post,  Blend::Replace, Stencil::Off },
	{ RmlPso::PostColorMatrix, Vs::Fullscreen, Ps::ColorMatrix, Target::Post,  Blend::Replace, Stencil::Off },
	{ RmlPso::PostBlendMask,   Vs::Fullscreen, Ps::BlendMask,   Target::Post,  Blend::Replace, Stencil::Off },
	{ RmlPso::PostBlur,        Vs::Blur,       Ps::Blur,        Target::Post,  Blend::Replace, Stencil::Off },
	{ RmlPso::PostDropShadow,  Vs::Fullscreen, Ps::DropShadow,  Target::Post,  Blend::Replace, Stencil::Off },
};
// clang-format on
static_assert(std::size(kPsoDefs) == static_cast<std::size_t>(RmlPso::Count));

constexpr const char* kVsEntry[] = { "VSGeometry", "VSFullscreen", "VSBlur" };
constexpr const char* kPsEntry[] = { "PSColor", "PSTexture", "PSGradient", "PSCopy", "PSSample",
                                     "PSColorMatrix", "PSBlendMask", "PSBlur", "PSDropShadow" };

D3D12_BLEND_DESC blendDesc(Blend b)
{
	D3D12_BLEND_DESC d = {};
	auto& rt = d.RenderTarget[0];
	rt.RenderTargetWriteMask = (b == Blend::NoColor) ? 0 : D3D12_COLOR_WRITE_ENABLE_ALL;
	rt.BlendEnable = (b == Blend::Over) ? TRUE : FALSE;
	rt.SrcBlend = D3D12_BLEND_ONE;
	rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D12_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D12_BLEND_ONE;
	rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	rt.LogicOp = D3D12_LOGIC_OP_NOOP;
	return d;
}

D3D12_DEPTH_STENCIL_DESC stencilDesc(Stencil s)
{
	D3D12_DEPTH_STENCIL_DESC d = {};
	d.DepthEnable = FALSE;
	d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	d.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	d.StencilEnable = (s != Stencil::Off) ? TRUE : FALSE;
	d.StencilReadMask = 0xFF;
	d.StencilWriteMask = (s == Stencil::Test || s == Stencil::Off) ? 0 : 0xFF;
	D3D12_DEPTH_STENCILOP_DESC op = {};
	op.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	op.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	op.StencilPassOp = (s == Stencil::WriteReplace) ? D3D12_STENCIL_OP_REPLACE
		: (s == Stencil::WriteIncrement) ? D3D12_STENCIL_OP_INCR_SAT : D3D12_STENCIL_OP_KEEP;
	op.StencilFunc = (s == Stencil::Test) ? D3D12_COMPARISON_FUNC_EQUAL : D3D12_COMPARISON_FUNC_ALWAYS;
	d.FrontFace = op;
	d.BackFace = op;
	return d;
}

D3D12_RASTERIZER_DESC rasterizerDesc()
{
	D3D12_RASTERIZER_DESC r = {};
	r.FillMode = D3D12_FILL_MODE_SOLID;
	r.CullMode = D3D12_CULL_MODE_NONE;
	// RCSS の 3D 変換は z を [-1, 1] に出すことがある。D3D は [0, 1] の外を捨てるので z では切らない。
	r.DepthClipEnable = FALSE;
	return r;
}

constexpr D3D12_INPUT_ELEMENT_DESC kGeometryLayout[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

D3D12_STATIC_SAMPLER_DESC samplerDesc(UINT reg, D3D12_TEXTURE_ADDRESS_MODE mode)
{
	D3D12_STATIC_SAMPLER_DESC s = {};
	s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	s.AddressU = s.AddressV = s.AddressW = mode;
	s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	s.MaxLOD = D3D12_FLOAT32_MAX;
	s.ShaderRegister = reg;
	s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return s;
}

} // namespace

struct RmlDx12Pipelines::Blobs
{
	std::array<ComPtr<ID3DBlob>, static_cast<std::size_t>(Vs::Count)> vs;
	std::array<ComPtr<ID3DBlob>, static_cast<std::size_t>(Ps::Count)> ps;
};

bool RmlDx12Pipelines::create(ID3D12Device* device, UINT layerSamples)
{
	if (device == nullptr) { m_error = "device is null"; return false; }
	Blobs blobs;
	return createRootSignature(device) && compileShaders(blobs) && createAll(device, blobs, layerSamples);
}

bool RmlDx12Pipelines::createRootSignature(ID3D12Device* device)
{
	D3D12_DESCRIPTOR_RANGE ranges[2] = {};
	for (UINT i = 0; i < 2; ++i)
	{
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = i;
		ranges[i].OffsetInDescriptorsFromTableStart = 0;
	}
	D3D12_ROOT_PARAMETER params[3] = {};
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	for (UINT i = 0; i < 2; ++i)
	{
		params[1 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[1 + i].DescriptorTable.NumDescriptorRanges = 1;
		params[1 + i].DescriptorTable.pDescriptorRanges = &ranges[i];
		params[1 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}
	const D3D12_STATIC_SAMPLER_DESC samplers[2] = {
		samplerDesc(0, D3D12_TEXTURE_ADDRESS_MODE_WRAP), samplerDesc(1, D3D12_TEXTURE_ADDRESS_MODE_BORDER) };
	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = 3;
	desc.pParameters = params;
	desc.NumStaticSamplers = 2;
	desc.pStaticSamplers = samplers;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) ||
	    FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
	                                       IID_PPV_ARGS(m_root.ReleaseAndGetAddressOf()))))
	{
		m_error = err ? static_cast<const char*>(err->GetBufferPointer()) : "root signature creation failed";
		return false;
	}
	return true;
}

bool RmlDx12Pipelines::compileShaders(Blobs& b)
{
	const auto compile = [this](const char* entry, const char* target, ComPtr<ID3DBlob>& out) {
		ComPtr<ID3DBlob> err;
		if (SUCCEEDED(gfx::compileDx12Shader(kRmlShaderSource, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3,
		                                     out.ReleaseAndGetAddressOf(), err.GetAddressOf())))
		{
			return true;
		}
		m_error = std::string(entry) + ": " + (err ? static_cast<const char*>(err->GetBufferPointer()) : "compile failed");
		return false;
	};
	for (std::size_t i = 0; i < b.vs.size(); ++i)
	{
		if (!compile(kVsEntry[i], "vs_5_0", b.vs[i])) { return false; }
	}
	for (std::size_t i = 0; i < b.ps.size(); ++i)
	{
		if (!compile(kPsEntry[i], "ps_5_0", b.ps[i])) { return false; }
	}
	return true;
}

bool RmlDx12Pipelines::createAll(ID3D12Device* device, const Blobs& b, UINT layerSamples)
{
	for (const PsoDef& def : kPsoDefs)
	{
		ID3DBlob* vs = b.vs[static_cast<std::size_t>(def.vs)].Get();
		ID3DBlob* ps = b.ps[static_cast<std::size_t>(def.ps)].Get();
		const bool layer = def.target == Target::Layer;
		D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
		d.pRootSignature = m_root.Get();
		d.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		d.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		d.BlendState = blendDesc(def.blend);
		d.SampleMask = UINT_MAX;
		d.RasterizerState = rasterizerDesc();
		d.DepthStencilState = stencilDesc(def.stencil);
		if (def.vs == Vs::Geometry) { d.InputLayout = { kGeometryLayout, static_cast<UINT>(std::size(kGeometryLayout)) }; }
		d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		d.NumRenderTargets = 1;
		d.RTVFormats[0] = kRmlColorFormat;
		d.DSVFormat = layer ? kRmlStencilFormat : DXGI_FORMAT_UNKNOWN;
		d.SampleDesc.Count = layer ? layerSamples : 1;
		if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(m_pso[static_cast<std::size_t>(def.id)].ReleaseAndGetAddressOf()))))
		{
			m_error = "pipeline state creation failed (" + std::to_string(static_cast<int>(def.id)) + ")";
			return false;
		}
	}
	return true;
}

} // namespace mitiru::ui_rml::dx12
