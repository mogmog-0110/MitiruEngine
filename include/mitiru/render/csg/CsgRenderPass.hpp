#pragma once

/// @file CsgRenderPass.hpp
/// @brief 焼いた CSG シェーダで距離場を直接レイマーチする DX12 パス
/// @details Makina で作った立体を、**メッシュに変換せずにそのまま**ゲームの絵に混ぜる。
///          頂点バッファもインデックスバッファも無い。三角形 1 枚を出して、
///          ピクセルごとに距離場を歩き、当たった点の深度を書く。
///
///          @b なぜメッシュにしないのか：メッシュにした瞬間、
///          コライダと絵が別の定義になる。`CsgSolid` の distance / contains / raycast は
///          **描画と同じ距離場**から出ているので、二重に持たない限りずれようがない
///          （PLAN.md D-09）。
///
///          @b シェーダはここでコンパイルしない。`CsgBake` が読んだ DXIL をそのまま
///          PSO にする。実行時に DXC を持ち込まないための決定で、理由は `CsgBake.hpp` にある。
///
///          @b 呼び出し位置：`Renderer3D_DX12::beginFrame()` と `endFrame()` の間。
///          レンダラのコマンドリストとレンダーターゲットに乗る（既存の
///          「外部アクセス用API」と同じ扱いで、レンダラ本体には何も足さない）。
///
///          @b 置き方の制限：**剛体変換と一様スケールのみ**。
///          非一様スケールを掛けた距離場はもう距離ではなく、潰した軸で値が短く出るので
///          マーチが表面を突き抜ける。`draw` はそれを描かずに拒否する。
///
///          `MITIRU_HAS_MAKINA` が立っていないビルドではこのヘッダーは空になる。

#ifdef MITIRU_HAS_MAKINA

#include <mitiru/render/Camera3D.hpp>
#include <mitiru/render/csg/CsgBake.hpp>
#include <mitiru/render/csg/CsgSolid.hpp>

#include <makina/Flatten.hpp>
#include <makina/RenderMaterial.hpp>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/math/Vec4.hpp>

#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mitiru::render::csg
{

/// @brief 立体をどう置き、どう照らすか
struct CsgDrawDesc
{
	sgc::Vec3f position{ 0.0f, 0.0f, 0.0f };  ///< ワールド位置
	float rotationYDeg = 0.0f;                 ///< Y 軸まわりの回転（度）
	float scale = 1.0f;                        ///< 一様スケール
	sgc::Vec3f baseColor{ 0.62f, 0.64f, 0.68f };
	float ambient = 0.16f;
	/// @brief 深度に足す下駄
	/// @details 既定は 0。同じ面をメッシュと共有する置き方をしたときだけ触る。
	float depthBias = 0.0f;
};

/// @brief 距離場を 1 パスで描く
///
/// 1 立体 = 1 PSO。焼いたシェーダはシーンごとに違うコードなので、これは避けられない。
/// 使い回すのは PSO ではなくルートシグネチャの方である。
class CsgRenderPass
{
	template <typename T>
	using ComPtr = Microsoft::WRL::ComPtr<T>;

public:
	/// @brief 描き込む先の形。レンダラが使っているものと一致していなければならない
	/// @details 値を渡してもらう形にしてあるのは、ここで定数として持つと
	///          レンダラ側が MSAA 数やフォーマットを変えたときに**黙って PSO 生成が失敗する**
	///          だけになるからである。呼び手が実際に使っている値を渡す。
	struct TargetFormat
	{
		DXGI_FORMAT color = DXGI_FORMAT_R16G16B16A16_FLOAT;
		DXGI_FORMAT depth = DXGI_FORMAT_D32_FLOAT;
		UINT sampleCount = 4;
	};

	CsgRenderPass() = default;

	CsgRenderPass(const CsgRenderPass&) = delete;
	CsgRenderPass& operator=(const CsgRenderPass&) = delete;

	/// @brief 焼いたシェーダから PSO を作る
	/// @param device D3D12 デバイス
	/// @param solid 立体。@p bake がこのシーンから焼かれたものかの照合に使う
	/// @param bake 読み込み済みの bake
	/// @param target 描き込む先の形
	/// @return 作れなかったときは false。理由は @ref error
	[[nodiscard]] bool initialize(ID3D12Device* device, const CsgSolid& solid,
	                              const CsgBake& bake, const TargetFormat& target)
	{
		m_ready = false;
		if (device == nullptr)
		{
			return fail("no D3D12 device");
		}
		if (!solid.valid())
		{
			return fail("the solid did not load: " + solid.error());
		}
		if (!bake.valid())
		{
			return fail("the bake did not load: " + bake.error());
		}
		// 古い .cso は問題なく PSO になり、問題なく描画され、形だけが違う。
		// 落ちないのが最悪なので、ここで止める。
		if (!bake.matches(solid))
		{
			return fail("this bake was not made from this scene (hash " + bake.sceneHash() +
			            " vs " + solid.sourceHash() + ") - re-run makina_bake");
		}

		m_boxMin = solid.boundsMin();
		m_boxMax = solid.boundsMax();
		m_live = bake.live();
		m_programNodes = bake.programNodes();
		if (m_live && m_programNodes <= 0)
		{
			return fail("a live bake names no program size - re-run makina_bake --live");
		}

		if (!createRootSignature(device))
		{
			return false;
		}
		if (!createPipeline(device, bake, target))
		{
			return false;
		}
		if (!createConstantBuffers(device))
		{
			return false;
		}
		// 焼き込みシェーダにも 1 件は束ねる: t0 を宣言していないシェーダに未束縛の
		// ルート SRV があっても構わないが、live には毎フレームの姿を置く場所が要る。
		if (!m_program.create(device, static_cast<std::size_t>(std::max(m_programNodes, 1)) *
		                                  sizeof(makina::EvalNode)))
		{
			return fail("could not create the program buffer");
		}
		if (!createMaterials(device, solid))
		{
			return false;
		}

		m_ready = true;
		m_error.clear();
		return true;
	}

	[[nodiscard]] bool ready() const noexcept { return m_ready; }
	[[nodiscard]] const std::string& error() const noexcept { return m_error; }

	/// @brief 1 立体を描く
	/// @param cmd レンダラのコマンドリスト（レンダーターゲットは設定済みであること）
	/// @param camera ワールド空間のカメラ
	/// @param lightDir 正規化済みの、光の**進む**向き
	/// @param desc 置き方
	/// @param viewportWidth ビューポート幅（px）。シザーを詰めるのに使う
	/// @param viewportHeight ビューポート高さ（px）
	/// @param program 今フレームの姿 (CsgSolid::programAt)。live に焼いたシェーダだけが
	///        読む。焼き込みシェーダに渡しても無視されるだけだが、live なのに無いときと
	///        ノード数が焼いたときと違うときは拒否する — 別の木の数値を読ませると
	///        落ちずに違う形が出る
	/// @return 描いたか。拒否した理由は @ref error
	[[nodiscard]] bool draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera,
	                        const sgc::Vec3f& lightDir, const CsgDrawDesc& desc,
	                        int viewportWidth, int viewportHeight,
	                        const makina::EvalProgram* program = nullptr)
	{
		if (!m_ready || cmd == nullptr)
		{
			return false;
		}
		if (!(desc.scale > 0.0f))
		{
			return fail("scale must be positive; a zero or negative one is not a placement");
		}
		if (m_live)
		{
			if (program == nullptr)
			{
				return fail("a live bake needs the frame's program (CsgSolid::programAt)");
			}
			if (static_cast<int>(program->nodes.size()) != m_programNodes)
			{
				return fail("the program has " + std::to_string(program->nodes.size()) +
				            " nodes but the live shader was baked for " +
				            std::to_string(m_programNodes) + " - a different tree, re-bake");
			}
		}

		const sgc::Mat4f objectToWorld = placement(desc);
		const sgc::Mat4f worldToObject = inversePlacement(desc);

		FrameCb frame{};
		fillCamera(frame, camera, lightDir);
		frame.materialCount = m_materialCount;
		frame.pigmentCount = m_pigmentCount;
		frame.lightCount = m_lightCount;
		D3D12_GPU_VIRTUAL_ADDRESS programAddr = m_program.address();
		if (m_live)
		{
			frame.programCount = static_cast<std::uint32_t>(program->nodes.size());
			programAddr = m_program.write(program->nodes.data(),
			                              program->nodes.size() * sizeof(makina::EvalNode));
		}

		EngineCb engine{};
		std::memcpy(engine.worldToObject, worldToObject.m, sizeof(engine.worldToObject));
		std::memcpy(engine.objectToWorld, objectToWorld.m, sizeof(engine.objectToWorld));
		const sgc::Mat4f viewProj = camera.viewProjectionMatrixZO();
		std::memcpy(engine.viewProj, viewProj.m, sizeof(engine.viewProj));
		engine.boxMin[0] = m_boxMin.x;
		engine.boxMin[1] = m_boxMin.y;
		engine.boxMin[2] = m_boxMin.z;
		engine.objectScale = desc.scale;
		engine.boxMax[0] = m_boxMax.x;
		engine.boxMax[1] = m_boxMax.y;
		engine.boxMax[2] = m_boxMax.z;
		engine.depthBias = desc.depthBias;
		engine.baseColor[0] = desc.baseColor.x;
		engine.baseColor[1] = desc.baseColor.y;
		engine.baseColor[2] = desc.baseColor.z;
		engine.ambient = desc.ambient;

		const auto frameAddr = m_frameCb.write(&frame, sizeof(frame));
		const auto engineAddr = m_engineCb.write(&engine, sizeof(engine));

		// 立体が画面のどこに写るかで矩形を詰める。
		//
		// 全面三角形のままだと、画面の 1/10 しか占めない小道具でも**全画素**が
		// ピクセルシェーダに入る。箱の外は 1 回のスラブ判定で discard されるが、
		// SV_Depth を書くパスは early-Z が効かないので、その 1 回は必ず走る。
		D3D12_RECT scissor{};
		if (!screenRect(camera, objectToWorld, viewportWidth, viewportHeight, scissor))
		{
			return true;   // 画面に一切かからない。描かないのが正しい結果である
		}
		cmd->RSSetScissorRects(1, &scissor);

		cmd->SetGraphicsRootSignature(m_rootSignature.Get());
		cmd->SetPipelineState(m_pso.Get());
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->SetGraphicsRootConstantBufferView(0, frameAddr);
		cmd->SetGraphicsRootConstantBufferView(1, engineAddr);
		cmd->SetGraphicsRootShaderResourceView(2, m_materials->GetGPUVirtualAddress());
		cmd->SetGraphicsRootShaderResourceView(3, m_pigments->GetGPUVirtualAddress());
		cmd->SetGraphicsRootShaderResourceView(4, programAddr);
		cmd->SetGraphicsRootShaderResourceView(5, m_lights->GetGPUVirtualAddress());
		cmd->DrawInstanced(3, 1, 0, 0);

		// シザーは呼び手のものに戻す。詰めたまま返すと、この後に積まれた描画が
		// 小道具の矩形に切り取られる — 原因が遠すぎて追えない類の不具合になる。
		const D3D12_RECT full{ 0, 0, static_cast<LONG>(viewportWidth),
			                   static_cast<LONG>(viewportHeight) };
		cmd->RSSetScissorRects(1, &full);
		return true;
	}

private:
	/// makina の `scene_prelude.hlsl` の cbuffer Params と一致していなければならない。
	struct alignas(256) FrameCb
	{
		float eye[3];      float tanHalfFov;
		float forward[3];  float aspect;
		float right[3];    std::uint32_t nodeCount;
		float up[3];       std::uint32_t maxSteps;
		float lightDir[3]; float stepScale;
		float farDist;     std::uint32_t enableAo; std::uint32_t debugMode; float groundY;
		float center[3];   float sceneRadius;
		std::uint32_t programCount; std::uint32_t materialCount;
		std::uint32_t pigmentCount; std::uint32_t povMatch;
		// gLightCount / gCameraKind / gCameraAngle: makina が光源と広角カメラを足したときに
		// 増えた 16 バイト。ここが無いと selMin から後ろが 16 バイトずれ、落ちずに違う絵になる
		// (最初はまさにそうなっていて、新しく焼いた立体だけが PSO を作れなかった)。
		std::uint32_t lightCount;   std::uint32_t cameraKind;
		float cameraAngle;          std::uint32_t padA;
		float selMin[3];   float selValid;
		float selMax[3];   float pad2;
	};

	/// makina の `scene_engine.hlsl` の cbuffer EngineParams と一致していなければならない。
	struct alignas(256) EngineCb
	{
		float worldToObject[4][4];
		float objectToWorld[4][4];
		float viewProj[4][4];
		float boxMin[3];    float objectScale;
		float boxMax[3];    float depthBias;
		float baseColor[3]; float ambient;
	};

	/// @brief 毎フレーム書き換える定数の置き場
	/// @details フレームが 3 枚飛んでいる間、GPU はまだ前のフレームの中身を読んでいる。
	///          1 本を上書きすると、その 2 枚が混ざった絵になる。
	class Ring
	{
	public:
		[[nodiscard]] bool create(ID3D12Device* device, std::size_t bytes)
		{
			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC desc{};
			desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			desc.Width = bytes * kSlots;
			desc.Height = 1;
			desc.DepthOrArraySize = 1;
			desc.MipLevels = 1;
			desc.Format = DXGI_FORMAT_UNKNOWN;
			desc.SampleDesc.Count = 1;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

			if (FAILED(device->CreateCommittedResource(
				    &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
				    nullptr, IID_PPV_ARGS(m_buffer.GetAddressOf()))))
			{
				return false;
			}
			D3D12_RANGE noRead{ 0, 0 };
			if (FAILED(m_buffer->Map(0, &noRead, &m_mapped)))
			{
				return false;
			}
			m_stride = bytes;
			return true;
		}

		/// @brief 現在のスロットの先頭。何も書かずに束ねるだけのとき用
		[[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS address() const
		{
			return m_buffer ? m_buffer->GetGPUVirtualAddress() + m_at * m_stride : 0;
		}

		D3D12_GPU_VIRTUAL_ADDRESS write(const void* data, std::size_t bytes)
		{
			const std::size_t offset = m_at * m_stride;
			std::memcpy(static_cast<std::uint8_t*>(m_mapped) + offset, data, bytes);
			m_at = (m_at + 1) % kSlots;
			return m_buffer->GetGPUVirtualAddress() + offset;
		}

	private:
		/// レンダラのトリプルバッファに合わせてある。
		static constexpr std::size_t kSlots = 3;

		ComPtr<ID3D12Resource> m_buffer;
		void* m_mapped = nullptr;
		std::size_t m_stride = 0;
		std::size_t m_at = 0;
	};

	bool fail(const std::string& why)
	{
		m_error = why;
		return false;
	}

	bool createRootSignature(ID3D12Device* device)
	{
		// ルート CBV 2 本 + マテリアル表 + プログラム。焼いたシェーダは評価プログラムを
		// **コードとして**持っているが、live に焼いたもの (D-15) は葉の数値を t0 から読む。
		// t1 のマテリアルはどちらも要る — どの面がどのマテリアルかは焼いたコードが返すが、
		// その中身は実行時のデータである。
		D3D12_ROOT_PARAMETER params[6]{};
		for (int i = 0; i < 2; ++i)
		{
			params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
			params[i].Descriptor.ShaderRegister = static_cast<UINT>(i);
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		for (int i = 2; i < 4; ++i)
		{
			// t1 マテリアル、t2 ピグメント。
			params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
			params[i].Descriptor.ShaderRegister = static_cast<UINT>(i - 1);
			params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		}
		// t0 プログラム (live のときだけ読まれる。焼き込みシェーダは束ねられても見ない)。
		params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		params[4].Descriptor.ShaderRegister = 0;
		params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		// t3 光源表 (scene_lights.hlsl)。シェーダが宣言している資源が束ねられていないと
		// PSO は作れない — 光源の無い立体でも 1 件置く。
		params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		params[5].Descriptor.ShaderRegister = 3;
		params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.NumParameters = 6;
		desc.pParameters = params;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob> blob;
		ComPtr<ID3DBlob> errorBlob;
		if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
		                                       blob.GetAddressOf(), errorBlob.GetAddressOf())))
		{
			const char* msg = errorBlob ? static_cast<const char*>(errorBlob->GetBufferPointer())
			                            : "(no message)";
			return fail(std::string("could not serialize the root signature: ") + msg);
		}
		if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
		                                       IID_PPV_ARGS(m_rootSignature.GetAddressOf()))))
		{
			return fail("could not create the root signature");
		}
		return true;
	}

	bool createPipeline(ID3D12Device* device, const CsgBake& bake, const TargetFormat& target)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
		pd.pRootSignature = m_rootSignature.Get();
		pd.VS = { bake.vertexShader().data(), bake.vertexShader().size() };
		pd.PS = { bake.pixelShader().data(), bake.pixelShader().size() };
		pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		// 全面三角形なので裏表が無い。カリングを残すと、巻き順の都合だけで
		// 何も出ないという分かりにくい失敗になる。
		pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		// レンダラ本体と同じ向き（クリアが 1.0 の LESS）。合わせないと、
		// 手前にあるメッシュに小道具が上書きされる。
		pd.DepthStencilState.DepthEnable = TRUE;
		pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		pd.SampleMask = UINT_MAX;
		pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pd.NumRenderTargets = 1;
		pd.RTVFormats[0] = target.color;
		pd.DSVFormat = target.depth;
		pd.SampleDesc.Count = target.sampleCount;

		if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(m_pso.GetAddressOf()))))
		{
			return fail("could not create the pipeline state - the baked shader and the render "
			            "target may disagree about format or sample count");
		}
		return true;
	}

	/// @brief マテリアル表を 1 度だけ載せる
	/// @details 立体は読み込んだあと変わらないので、毎フレームの更新は要らない。
	///          マテリアルが 1 つも無いシーンでも 1 件は置く — シェーダが t1 を宣言しているのに
	///          何も束ねないのは「空の表」ではなく未定義動作である。
	bool createMaterials(ID3D12Device* device, const CsgSolid& solid)
	{
		std::vector<makina::GpuMaterial> mats = makina::gpuMaterials(solid.scene());
		m_materialCount = solid.scene().materials.count;
		if (mats.empty())
		{
			mats.push_back(makina::defaultGpuMaterial());
		}
		// From the flattened program: a pattern lives in the space of the solid wearing it, so
		// the table is one entry per (pattern, place) pair rather than a copy of the scene's.
		std::vector<makina::GpuPigment> pigs = makina::flatten(solid.scene()).pigments;
		m_pigmentCount = static_cast<std::uint32_t>(pigs.size());
		if (pigs.empty())
		{
			pigs.push_back(makina::GpuPigment{});
		}
		if (!upload(device, mats.data(), mats.size() * sizeof(makina::GpuMaterial), m_materials))
		{
			return fail("could not create the material buffer");
		}
		if (!upload(device, pigs.data(), pigs.size() * sizeof(makina::GpuPigment), m_pigments))
		{
			return fail("could not create the pigment buffer");
		}
		// シーンの光源。makina の Light はそのまま GPU の MkLight (scene_lights.hlsl)。無ければ
		// 既定の 1 件を置き、gLightCount 0 でシェーダに「レンダラの光で」と言わせる —
		// ビューポートと同じ扱い。
		std::vector<makina::Light> lights;
		for (std::uint32_t i = 0; i < solid.scene().lights.count; ++i)
		{
			lights.push_back(solid.scene().lights[i]);
		}
		m_lightCount = static_cast<std::uint32_t>(lights.size());
		if (lights.empty())
		{
			lights.push_back(makina::Light{});
		}
		if (!upload(device, lights.data(), lights.size() * sizeof(makina::Light), m_lights))
		{
			return fail("could not create the light buffer");
		}
		return true;
	}

	/// @brief 読み込み時に 1 度だけ載せる小さな表を作る
	static bool upload(ID3D12Device* device, const void* data, std::size_t bytes,
	                   ComPtr<ID3D12Resource>& out)
	{
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = bytes;   // 表は小さいので、ページ境界に揃える意味がない
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_UNKNOWN;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		                                           IID_PPV_ARGS(out.GetAddressOf()))))
		{
			return false;
		}
		void* mapped = nullptr;
		D3D12_RANGE noRead{ 0, 0 };
		if (FAILED(out->Map(0, &noRead, &mapped)))
		{
			return false;
		}
		std::memcpy(mapped, data, bytes);
		out->Unmap(0, nullptr);
		return true;
	}

	bool createConstantBuffers(ID3D12Device* device)
	{
		if (!m_frameCb.create(device, sizeof(FrameCb)) ||
		    !m_engineCb.create(device, sizeof(EngineCb)))
		{
			return fail("could not create the constant buffers");
		}
		return true;
	}

	/// makina の全シェーダが共有するカメラの持ち方に合わせて詰める。
	static void fillCamera(FrameCb& cb, const Camera3D& camera, const sgc::Vec3f& lightDir)
	{
		const sgc::Vec3f eye = camera.position();
		sgc::Vec3f forward{ camera.target().x - eye.x, camera.target().y - eye.y,
			                camera.target().z - eye.z };
		normalize(forward);
		sgc::Vec3f right = cross(forward, camera.up());
		normalize(right);
		sgc::Vec3f up = cross(right, forward);
		normalize(up);

		cb.eye[0] = eye.x; cb.eye[1] = eye.y; cb.eye[2] = eye.z;
		cb.forward[0] = forward.x; cb.forward[1] = forward.y; cb.forward[2] = forward.z;
		cb.right[0] = right.x; cb.right[1] = right.y; cb.right[2] = right.z;
		cb.up[0] = up.x; cb.up[1] = up.y; cb.up[2] = up.z;
		cb.lightDir[0] = lightDir.x; cb.lightDir[1] = lightDir.y; cb.lightDir[2] = lightDir.z;

		// fov() はラジアン（既定 1.0472 = 60 度）。度だと思って変換すると扇が 100 倍以上
		// 狭くなり、小道具が画面を埋め尽くす — それでも「何か描けている」ので、
		// 絵を眺めるだけでは間違いに見えない。
		cb.tanHalfFov = std::tan(camera.fov() * 0.5f);
		cb.aspect = camera.aspectRatio();
		cb.maxSteps = 192u;
		cb.stepScale = 0.85f;
		cb.enableAo = 1u;
		// マーチは箱の中だけを歩くので、ここは使われない。ゼロのまま渡すと
		// 共有のシェーディングが 0 除算に落ちるので、遠クリップを入れておく。
		cb.farDist = camera.farClip();
	}

	/// 回転（Y）とスケールと平行移動。行優先。
	static sgc::Mat4f placement(const CsgDrawDesc& d)
	{
		const float r = d.rotationYDeg * 3.14159265358979f / 180.0f;
		const float c = std::cos(r) * d.scale;
		const float s = std::sin(r) * d.scale;
		return sgc::Mat4f{ c,    0.0f,     s,    d.position.x,
			               0.0f, d.scale,  0.0f, d.position.y,
			               -s,   0.0f,     c,    d.position.z,
			               0.0f, 0.0f,     0.0f, 1.0f };
	}

	/// @brief 上の逆
	/// @details 一般の逆行列を求めない。剛体変換と一様スケールしか受け付けないので、
	///          回転は転置、スケールは逆数で厳密に出る。一般解を通すと、
	///          **この限定された形でしか正しくない**ことがコードから読めなくなる。
	static sgc::Mat4f inversePlacement(const CsgDrawDesc& d)
	{
		const float r = d.rotationYDeg * 3.14159265358979f / 180.0f;
		const float inv = 1.0f / d.scale;
		const float c = std::cos(r) * inv;
		const float s = std::sin(r) * inv;
		// R^T / scale を平行移動に当てる。
		const float tx = -(c * d.position.x - s * d.position.z);
		const float ty = -inv * d.position.y;
		const float tz = -(s * d.position.x + c * d.position.z);
		return sgc::Mat4f{ c,    0.0f, -s,   tx,
			               0.0f, inv,  0.0f, ty,
			               s,    0.0f, c,    tz,
			               0.0f, 0.0f, 0.0f, 1.0f };
	}

	static void normalize(sgc::Vec3f& v)
	{
		const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len > 1e-12f)
		{
			v.x /= len;
			v.y /= len;
			v.z /= len;
		}
	}

	static sgc::Vec3f cross(const sgc::Vec3f& a, const sgc::Vec3f& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	/// @brief 箱の 8 隅を射影して、画面上の矩形を出す
	/// @return 画面にかかっていれば true
	/// @details 隅が 1 つでもカメラの後ろにあるときは全画面を返す。
	///          後ろの点の射影は反転して出るので、そのまま最小最大に混ぜると
	///          **手前にあるはずの小道具が消える**。詰め損なうのは遅いだけで済む。
	bool screenRect(const Camera3D& camera, const sgc::Mat4f& objectToWorld, int width, int height,
	                D3D12_RECT& out) const
	{
		const sgc::Mat4f viewProj = camera.viewProjectionMatrixZO();
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		bool behind = false;

		for (int i = 0; i < 8; ++i)
		{
			const sgc::Vec3f local{ (i & 1) ? m_boxMax.x : m_boxMin.x,
				                    (i & 2) ? m_boxMax.y : m_boxMin.y,
				                    (i & 4) ? m_boxMax.z : m_boxMin.z };
			const sgc::Vec4f world = objectToWorld * sgc::Vec4f{ local.x, local.y, local.z, 1.0f };
			const sgc::Vec4f clip = viewProj * world;
			if (clip.w <= 1e-6f)
			{
				behind = true;
				break;
			}
			const float ndcX = clip.x / clip.w;
			const float ndcY = clip.y / clip.w;
			minX = std::min(minX, ndcX);
			maxX = std::max(maxX, ndcX);
			minY = std::min(minY, ndcY);
			maxY = std::max(maxY, ndcY);
		}

		if (behind)
		{
			out = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
			return true;
		}
		if (minX > 1.0f || maxX < -1.0f || minY > 1.0f || maxY < -1.0f)
		{
			return false;
		}

		// NDC から画素へ。y は上下が逆。1 画素の余白は、隅がちょうど境界に乗ったときに
		// 端が欠けるのを防ぐ。
		const auto toPxX = [width](float ndc) {
			return static_cast<LONG>((ndc * 0.5f + 0.5f) * static_cast<float>(width));
		};
		const auto toPxY = [height](float ndc) {
			return static_cast<LONG>((0.5f - ndc * 0.5f) * static_cast<float>(height));
		};
		out.left = std::max<LONG>(0, toPxX(minX) - 1);
		out.right = std::min<LONG>(width, toPxX(maxX) + 1);
		out.top = std::max<LONG>(0, toPxY(maxY) - 1);
		out.bottom = std::min<LONG>(height, toPxY(minY) + 1);
		return out.right > out.left && out.bottom > out.top;
	}

	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12PipelineState> m_pso;
	ComPtr<ID3D12Resource> m_materials;
	ComPtr<ID3D12Resource> m_pigments;
	ComPtr<ID3D12Resource> m_lights;
	std::uint32_t m_materialCount = 0;
	std::uint32_t m_pigmentCount = 0;
	std::uint32_t m_lightCount = 0;
	Ring m_frameCb;
	/// live の姿 (D-15)。毎フレーム載せ替えるので定数と同じ 3 スロットのリング
	Ring m_program;
	bool m_live = false;
	int m_programNodes = 0;
	Ring m_engineCb;
	sgc::Vec3f m_boxMin{};
	sgc::Vec3f m_boxMax{};
	bool m_ready = false;
	std::string m_error;
};

} // namespace mitiru::render::csg

#endif // MITIRU_HAS_MAKINA
