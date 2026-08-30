#pragma once

/// @file DX12Live2D.hpp
/// @brief Live2D Cubism 5 モデルを **自前の Direct3D 12** で描く完全移植レンダラ (Core-only)。
/// @details 公式 SDK に D3D12 レンダラは無い。Cubism Core C API から drawable/offscreen を読み、
///          公式 CubismRenderer_D3D11 を忠実移植しつつ DX12 最適化:
///            Stage0 基本3ブレンド・非マスク / Stage1 クリッピングマスク(高精度) /
///            Stage2 Cubism5 オフスクリーングループ + advanced blend。
///          描画はモデル RT へ行い最後に backbuffer へ blit。描画木を再帰し、interesting な
///          オフスクリーン群は専用 RT へ描いて親へ blend-mode 合成 (advanced は dest コピー+uber PS)。
///          MITIRU_HAS_CUBISM_CORE 未定義時はクラスごと無効。

#ifdef MITIRU_HAS_CUBISM_CORE

#include <Live2DCubismCore.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <stb_image.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace mitiru::render
{

class Dx12Live2D
{
	using ComPtrRes = Microsoft::WRL::ComPtr<ID3D12Resource>;
	template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	struct Cb {
		float projectMatrix[16]; float clipMatrix[16];
		float baseColor[4]; float multiplyColor[4]; float screenColor[4]; float channelFlag[4];
		float blendMode[4];   // x=colorType y=alphaType z=masked w=inverted (per-drawable advanced blend)
	};
	static constexpr UINT kCbStride = 256;
	static constexpr int  kMaskSize = 256;   // 公式 _clippingMaskBufferSize と同一 (256²)
	static constexpr int  kFrames   = 3;     // エンジンの FRAME_COUNT と一致 (動的バッファの多重化数)

	struct Drawable {
		int vtxOffset=0, vtxCount=0, idxOffset=0, idxCount=0, texIndex=0, blend=0;
		int blendModeFull=0;   // csmGetDrawableBlendModes 値 (color=&0xFF, alpha=(>>8)&0xFF)
		bool advanced=false;   // 公式 isBlendMode (per-pixel advanced blend が要るか)
		int maskCount=0; const int* masks=nullptr; bool inverted=false;
		int ownerOff=-1;   // 所属オフスクリーン (-1 = model)
		int clipCtx=-1;    // 所属クリップコンテキスト (-1 = マスク無し)。packing 時に使用。
		bool culling=false;// 片面 (= NOT double-sided) → 背面カリング (公式 Cull_Ccw)
	};
	struct OffG {
		int parentOff=-1, ownerPart=-1, blendInt=0;
		float opacity=1.0f, mult[4]={1,1,1,1}, screen[4]={0,0,0,0};
		int order=0;          // render order キー (subtree drawable の最小)
		bool interesting=false;
		int rtIndex=-1;       // m_offRT のインデックス (interesting のみ)
	};
	// クリップコンテキスト: 同一マスク集合を共有する描画オブジェクト群 (公式 CubismClippingContext)。
	// 低精度経路では 256² マスクバッファを RGBA 4ch × タイル分割で共有する (公式 SetupLayoutBounds)。
	struct ClipCtx {
		std::vector<int> maskDraws;   // マスク形状の drawable index
		std::vector<int> users;       // このマスクを使う (被クリップ) drawable index
		int channel=0;                // 0=R 1=G 2=B 3=A
		float lx=0,ly=0,lw=1,lh=1;    // レイアウト矩形 (タイル, 0..1)
		float matMask[16]={};         // モデル→マスク NDC
		float matDraw[16]={};         // モデル→マスク UV (タイル/ch)
		float boundsNdc[4]={-1,-1,1,1};// タイル NDC 矩形 (inside test 用)
	};
	static constexpr float kChannelColor[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};

public:
	~Dx12Live2D() = default;   // csmModel は Framework (live2d::Live2DModel) が所有。ここでは非所有。
	[[nodiscard]] bool ready() const noexcept { return m_ready; }

	/// @brief 公式 LAppView 相当のステージ画像を設定 (背景/歯車/閉じる)。load() 前に呼ぶ。
	void setStage(const char* bg, const char* gear, const char* close) {
		m_stageBg=(bg?bg:""); m_stageGear=(gear?gear:""); m_stageClose=(close?close:"");
		m_hasStage = !m_stageBg.empty();
	}

	/// @brief 外部所有の csmModel を描画対象に取り、GPU リソース (頂点/CB/テクスチャ/RT/PSO) を構築する。
	/// @details モデルの読み込み・パラメータ更新・csmUpdateModel は Framework 側が担う。本レンダラは
	///          毎フレーム最新の drawable メッシュを読んで D3D12 で描くだけ (masks + offscreen + advanced blend)。
	bool load(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
	          csmModel* model, const char* const* texPaths, int texCount)
	{
		if (m_ready || m_failed) return m_ready;
		if (!device || !cmdList || !model) { m_failed = true; return false; }
		m_device = device;
		m_model = model;            // 非所有: Framework が所有・更新する
		csmUpdateModel(m_model);    // トポロジ確定のため一度更新 (以後は Framework が毎フレーム更新)

		const int dc = csmGetDrawableCount(m_model);
		const int* vc=csmGetDrawableVertexCounts(m_model); const int* ic=csmGetDrawableIndexCounts(m_model);
		const int* ti=csmGetDrawableTextureIndices(m_model); const csmFlags* cf=csmGetDrawableConstantFlags(m_model);
		const unsigned short** idx=csmGetDrawableIndices(m_model);
		const int* mc=csmGetDrawableMaskCounts(m_model); const int** mks=csmGetDrawableMasks(m_model);
		const int* dbm=(const int*)csmGetDrawableBlendModes(m_model);

		m_draws.resize(dc);
		std::vector<uint16_t> allIdx;
		int vOff=0,iOff=0,masked=0;
		for (int d=0; d<dc; ++d) {
			Drawable& dr=m_draws[d];
			dr.vtxOffset=vOff; dr.vtxCount=vc[d]; dr.idxOffset=iOff; dr.idxCount=ic[d]; dr.texIndex=ti[d];
			dr.blend=(cf[d]&csmBlendAdditive)?1:(cf[d]&csmBlendMultiplicative)?2:0;
			dr.blendModeFull=dbm[d];
			// 公式 SetBlendMode: Color∈{AddCompatible(1),MultiplyCompatible(2)} は固定機能、Normal(0)+Over(0) も固定機能。
			// それ以外 (Color≧3、または Normal+非Over alpha) は per-pixel advanced blend。
			{ const int col=dbm[d]&0xFF, al=(dbm[d]>>8)&0xFF; dr.advanced=(col>2)||(col==0&&al!=0); }
			dr.maskCount=mc[d]; dr.masks=(mc[d]>0)?mks[d]:nullptr; dr.inverted=(cf[d]&csmIsInvertedMask)!=0;
			dr.culling=!(cf[d]&csmIsDoubleSided);   // 片面なら背面カリング (公式 Cull_Ccw)
			if (mc[d]>0) ++masked;
			for (int k=0;k<ic[d];++k) allIdx.push_back(idx[d][k]);
			vOff+=vc[d]; iOff+=ic[d];
		}
		m_totalVerts=vOff;
		buildTree();
		computeClips();   // クリップコンテキスト構築 + レイアウト分割 (公式 SetupLayoutBounds)

		std::fprintf(stderr, "[Live2D] model: %d drawables, %d masked, %d offscreen (%d interesting), %d verts\n",
		             dc, masked, (int)m_offs.size(), m_interestingCount, m_totalVerts);

		if (!createBuffers(device, allIdx)) { m_failed=true; return false; }
		if (!createTextures(device, cmdList, texPaths, texCount)) { m_failed=true; return false; }
		if (!createPipeline(device)) { m_failed=true; return false; }
		writeVertices();
		m_ready=true;
		std::fprintf(stderr, "[Live2D] loaded (DX12 full renderer: masks + offscreen + advanced blend)\n");
		return true;
	}

	// 現フレームの動的バッファを選択 (生ポインタを配列から差し替え)。
	void selectFrame(int f)
	{
		m_frame=f;
		m_vb=m_vbN[f].Get(); m_cb=m_cbN[f].Get(); m_offCb=m_offCbN[f].Get(); m_clipCb=m_clipCbN[f].Get(); m_spriteCb=m_spriteCbN[f].Get();
		m_vbPtr=m_vbPtrN[f]; m_cbPtr=m_cbPtrN[f]; m_offCbPtr=m_offCbPtrN[f]; m_clipCbPtr=m_clipCbPtrN[f]; m_spriteCbPtr=m_spriteCbPtrN[f];
	}

	void render(ID3D12GraphicsCommandList* cl, D3D12_CPU_DESCRIPTOR_HANDLE backRtv, int viewW, int viewH, int frameIndex)
	{
		if (!m_ready || !cl) return;
		selectFrame(frameIndex % kFrames);   // GPU 実行中の別フレームバッファを上書きしないよう切替
		// モデル更新 (motion/physics/csmUpdateModel) は Framework (live2d::Live2DModel::update) が
		// 既に実施済み。ここでは最新の drawable 状態を読んで描くだけ。
		// 2× スーパーサンプリング: 内部 RT を ss 倍で描画し、最後に backbuffer へ縮小 blit (縁の AA)。
		const int ssW = viewW*m_ss, ssH = viewH*m_ss;
		m_dyn=csmGetDrawableDynamicFlags(m_model); m_op=csmGetDrawableOpacities(m_model);   // clip 行列で使うので先に
		refreshOffscreen();   // オフスクリーン不透明度/ブレンド/色を毎フレーム更新
		writeVertices(); computeMvp(ssW, ssH);
		computeClipMatrices();   // 低精度パッキング用の per-frame mask/draw 行列 (writeConstants より前)
		writeConstants();
		if (!ensureRTs(ssW, ssH)) return;
		writeOffscreenCBs();

		m_cl=cl; m_vw=ssW; m_vh=ssH;
		cl->SetGraphicsRootSignature(m_rootSig.Get());
		ID3D12DescriptorHeap* heaps[]={m_srvHeap.Get()}; cl->SetDescriptorHeaps(1, heaps);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		D3D12_VERTEX_BUFFER_VIEW vbv={m_vb->GetGPUVirtualAddress(),(UINT)(m_totalVerts*16),16};
		D3D12_INDEX_BUFFER_VIEW ibv={m_ib->GetGPUVirtualAddress(),m_ibBytes,DXGI_FORMAT_R16_UINT};
		cl->IASetVertexBuffers(0,1,&vbv); cl->IASetIndexBuffer(&ibv);
		cl->SetGraphicsRootDescriptorTable(2, gpu(m_maskSrvIdx));

		renderMaskBuffer();   // 低精度パッキング: マスクバッファ (256²) を一度だけ生成 (renderChildren より前)

		// ── モデル RT へ全体描画 ──
		barrier(m_modelRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		setRT(m_modelRtv, ssW, ssH);
		const float clr[4]={0,0,0,0}; cl->ClearRenderTargetView(m_modelRtv, clr, 0, nullptr);
		drawObjectLoop();   // 公式 DrawObjectLoop (フラット走査 + オフスクリーン連鎖)
		barrier(m_modelRT.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		// ── backbuffer: ステージ (背景/歯車/閉じる) を先に描き、その上へモデルを合成 (公式の描画順) ──
		setRT(backRtv, viewW, viewH);
		drawStage(viewW, viewH);                                   // 背景・ボタン (モデルより背面)
		// ── モデル RT を backbuffer へ blit (premult over) ──
		cl->SetPipelineState(m_psoBlit.Get());
		cl->SetGraphicsRootDescriptorTable(1, gpu(m_modelSrvIdx));
		cl->SetGraphicsRootConstantBufferView(0, m_offCb->GetGPUVirtualAddress());  // blit CB = slot 0 (baseColor=1)
		cl->DrawInstanced(3,1,0,0);
	}

private:
	// ── 描画木 ──
	void buildTree()
	{
		const int dc=(int)m_draws.size();
		const int osc=csmGetOffscreenCount(m_model);
		const int pc=csmGetPartCount(m_model);
		const int* dPart=csmGetDrawableParentPartIndices(m_model);
		const int* pParent=csmGetPartParentPartIndices(m_model);
		const int* pOff=csmGetPartOffscreenIndices(m_model);
		const int* oOwner=csmGetOffscreenOwnerIndices(m_model);
		const int* oBlend=csmGetOffscreenBlendModes(m_model);
		const float* oOp=csmGetOffscreenOpacities(m_model);
		const csmVector4* oMul=csmGetOffscreenMultiplyColors(m_model);
		const csmVector4* oScr=csmGetOffscreenScreenColors(m_model);
		const int* ro=csmGetRenderOrders(m_model);

		// part → owning offscreen (自身/祖先パートが所有する最内オフスクリーン)
		auto partOff=[&](int part)->int{ int p=part,guard=0; while(p>=0&&guard++<4096){ if(pOff[p]>=0) return pOff[p]; p=pParent[p]; } return -1; };
		auto isAncestorPart=[&](int a,int b)->bool{ int p=b,guard=0; while(p>=0&&guard++<4096){ if(p==a) return true; p=pParent[p]; } return false; };

		// drawable → owning offscreen
		for (int d=0; d<dc; ++d) m_draws[d].ownerOff = (dPart[d]>=0)? partOff(dPart[d]) : -1;

		m_offs.resize(osc);
		for (int o=0;o<osc;++o) {
			OffG& g=m_offs[o];
			g.ownerPart=oOwner[o]; g.blendInt=oBlend[o]; g.opacity=oOp[o];
			g.mult[0]=oMul[o].X; g.mult[1]=oMul[o].Y; g.mult[2]=oMul[o].Z; g.mult[3]=oMul[o].W;
			g.screen[0]=oScr[o].X; g.screen[1]=oScr[o].Y; g.screen[2]=oScr[o].Z; g.screen[3]=oScr[o].W;
			// 親オフスクリーン: owner part の親パートから上って最初に所有するオフスクリーン
			g.parentOff = (oOwner[o]>=0 && pParent[oOwner[o]]>=0) ? partOff(pParent[oOwner[o]]) : -1;
			// 公式: 全オフスクリーングループが自前 RT を持ち、flatten→シェーダ合成される (inline 化はしない)
			g.interesting = true; g.rtIndex = o;
		}
		m_interestingCount = osc;
		(void)isAncestorPart; (void)ro;

		// 公式 DrawObjectLoop / FlushOffscreenChainForDrawable 用に part 階層を保持 (静的トポロジ)。
		m_dPart.assign(dPart, dPart+dc);
		m_pParent.assign(pParent, pParent+pc);
		m_oOwner.assign(oOwner, oOwner+osc);
	}

	// オフスクリーンの不透明度・ブレンド・乗算/スクリーン色はモーション/ポーズで毎フレーム変わる
	// (所有パート opacity 等)。ロード時固定だと Tap 中に合成が崩れるので毎フレーム読み直す。
	void refreshOffscreen()
	{
		const int osc=(int)m_offs.size(); if (osc==0) return;
		const int* oBlend=csmGetOffscreenBlendModes(m_model);
		const float* oOp=csmGetOffscreenOpacities(m_model);
		const csmVector4* oMul=csmGetOffscreenMultiplyColors(m_model);
		const csmVector4* oScr=csmGetOffscreenScreenColors(m_model);
		for (int o=0;o<osc;++o){ OffG& g=m_offs[o];
			g.blendInt=oBlend[o]; g.opacity=oOp[o];
			g.mult[0]=oMul[o].X; g.mult[1]=oMul[o].Y; g.mult[2]=oMul[o].Z; g.mult[3]=oMul[o].W;
			g.screen[0]=oScr[o].X; g.screen[1]=oScr[o].Y; g.screen[2]=oScr[o].Z; g.screen[3]=oScr[o].W; }
	}

	// ── 公式 CubismRenderer_D3D11::DrawObjectLoop の忠実移植 ──
	// drawable + offscreen を csmGetRenderOrders (= total 個のスロット) で1本にソートし、
	// _currentOffscreen 連鎖でフラットに描く。私の独自再帰 + min-child 近似 (モーション中の重なり順
	// 崩れの根因) を廃止。
	void drawObjectLoop()
	{
		const int dc=(int)m_draws.size(); const int osc=(int)m_offs.size(); const int total=dc+osc;
		const int* ro=csmGetRenderOrders(m_model);   // total 個 (前半=drawable, 後半=offscreen) のスロット
		m_sortedIdx.assign(total,-1); m_sortedType.assign(total,0);
		for (int i=0;i<total;++i){ const int order=ro[i]; if (order<0||order>=total) continue;
			if (i<dc){ m_sortedIdx[order]=i;      m_sortedType[order]=0; }   // drawable
			else     { m_sortedIdx[order]=i-dc;   m_sortedType[order]=1; } } // offscreen
		m_curOff=-1;   // モデル RT が初期ターゲット
		for (int i=0;i<total;++i){ const int idx=m_sortedIdx[i]; if (idx<0) continue;
			if (m_sortedType[i]==0) drawObjDrawable(idx); else addOffscreen(idx); }
		while (m_curOff>=0) finalizeOffscreen();   // 残った連鎖を flush
	}
	D3D12_CPU_DESCRIPTOR_HANDLE curRtv() const { return (m_curOff>=0)? m_offRtv[m_curOff] : m_modelRtv; }
	ID3D12Resource* curRes() const { return (m_curOff>=0)? m_offRT[m_curOff].Get() : m_modelRT.Get(); }
	// 公式 AddOffscreen: 兄弟/叔父オフスクリーンを flush してから o の RT を開く (clear)
	void addOffscreen(int o)
	{
		while (m_curOff>=0 && m_curOff!=o && m_curOff!=m_offs[o].parentOff) finalizeOffscreen();
		barrier(m_offRT[o].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		setRT(m_offRtv[o], m_vw, m_vh);
		const float z[4]={0,0,0,0}; m_cl->ClearRenderTargetView(m_offRtv[o], z, 0, nullptr);
		m_curOff=o;
	}
	// 公式 DrawOffscreen: 現オフスクリーンを終了し、親ターゲットへ blend mode 合成して pop
	void finalizeOffscreen()
	{
		const int o=m_curOff; if (o<0) return;
		barrier(m_offRT[o].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_curOff=m_offs[o].parentOff;   // 先に親へ pop (ターゲットを親 RT へ)
		setRT(curRtv(), m_vw, m_vh);
		compositeOffscreen(o, curRtv(), curRes());
	}
	// 公式 DrawDrawable: 可視判定 → このドロウアブルが属さないオフスクリーンを flush → 描画
	void drawObjDrawable(int d)
	{
		if (!(m_dyn[d]&csmIsVisible)) return;
		flushOffscreenChainForDrawable(d);
		if (m_op[d]<=0.0f || m_draws[d].idxCount==0) return;
		drawDrawable(d, curRtv());
	}
	// 公式 FlushOffscreenChainForDrawable: drawable の part 親を辿り、現オフスクリーンの owner part を
	// 通らない (= 外側) なら finalize して pop。通る (= 内側) なら停止。
	void flushOffscreenChainForDrawable(int d)
	{
		while (m_curOff>=0) {
			const int ownerPart=m_oOwner[m_curOff];
			int p=m_dPart[d]; bool outside=true; int guard=0;
			while (p>=0 && guard++<4096) { if (ownerPart!=p) p=m_pParent[p]; else { outside=false; break; } }
			if (outside) finalizeOffscreen(); else break;
		}
	}

	// ── クリップコンテキスト構築 (公式 CubismClippingManager 移植) ──
	void computeClips()
	{
		m_clips.clear(); m_packMasks=false;
		for (int d=0; d<(int)m_draws.size(); ++d) {
			if (m_draws[d].maskCount<=0) continue;
			int found=-1;
			for (int c=0; c<(int)m_clips.size(); ++c) {
				if ((int)m_clips[c].maskDraws.size()!=m_draws[d].maskCount) continue;
				bool same=true; for (int k=0;k<m_draws[d].maskCount;++k) if (m_clips[c].maskDraws[k]!=m_draws[d].masks[k]) { same=false; break; }
				if (same) { found=c; break; }
			}
			if (found<0) { ClipCtx cc; cc.maskDraws.assign(m_draws[d].masks, m_draws[d].masks+m_draws[d].maskCount); found=(int)m_clips.size(); m_clips.push_back(std::move(cc)); }
			m_clips[found].users.push_back(d); m_draws[d].clipCtx=found;
		}
		const int n=(int)m_clips.size();
		// マスクは常に高精度経路 (per-drawable、各マスクを 256² フルバッファへ描く)。これは公式の
		// UseHighPrecisionMask と同等で、被クリップ境界を正しく出す。低精度パッキング (RGBA 4ch×タイル
		// 分割) は多マスクモデル (例: Mao=16コンテキスト→128²タイル) で目などの境界が粗く崩れ、
		// 安定性も劣るため使用しない。setupLayoutBounds の実装は残置 (将来の最適化用)。
		m_packMasks = false;
		(void)n;
		// if (m_interestingCount==0 && n>0 && n<=36) { m_packMasks=true; setupLayoutBounds(n); }
	}
	// 公式 SetupLayoutBounds: 256² を RGBA 4ch × (1/2/4/9 分割) に割り当て (RenderTexture 1枚前提)
	void setupLayoutBounds(int usingClipCount)
	{
		const int CH=4; const int divCount=usingClipCount/CH, modCount=usingClipCount%CH; int cur=0;
		for (int ch=0; ch<CH; ++ch) {
			const int layoutCount=divCount + (ch<modCount?1:0);
			if (layoutCount==1) { ClipCtx& c=m_clips[cur++]; c.channel=ch; c.lx=0;c.ly=0;c.lw=1;c.lh=1; }
			else if (layoutCount==2) { for (int i=0;i<2;++i){ ClipCtx& c=m_clips[cur++]; c.channel=ch; c.lx=(i%2)*0.5f;c.ly=0;c.lw=0.5f;c.lh=1; } }
			else if (layoutCount>=3 && layoutCount<=4) { for (int i=0;i<layoutCount;++i){ ClipCtx& c=m_clips[cur++]; c.channel=ch; c.lx=(i%2)*0.5f;c.ly=(i/2)*0.5f;c.lw=0.5f;c.lh=0.5f; } }
			else if (layoutCount>=5) { for (int i=0;i<layoutCount;++i){ ClipCtx& c=m_clips[cur++]; c.channel=ch; c.lx=(i%3)/3.0f;c.ly=(i/3)/3.0f;c.lw=1.0f/3.0f;c.lh=1.0f/3.0f; } }
		}
	}
	// per-frame: 各コンテキストの被クリップ bbox から mask/draw 行列を公式 SetupMatrixForHighPrecision で算出
	void computeClipMatrices()
	{
		if (!m_packMasks) return;
		const csmVector2** pos=csmGetDrawableVertexPositions(m_model);
		csmVector2 sizePx{}, originPx{}; float ppu=1.0f; csmReadCanvasInfo(m_model,&sizePx,&originPx,&ppu);
		const float maskPx=(float)kMaskSize, MARGIN=0.05f;
		for (auto& c : m_clips) {
			float mnx=1e30f,mny=1e30f,mxx=-1e30f,mxy=-1e30f; bool any=false;
			auto acc=[&](int u,bool vis){ if (vis && !(m_dyn[u]&csmIsVisible)) return; const csmVector2* p=pos[u];
				for (int v=0;v<m_draws[u].vtxCount;++v){ mnx=p[v].X<mnx?p[v].X:mnx; mxx=p[v].X>mxx?p[v].X:mxx; mny=p[v].Y<mny?p[v].Y:mny; mxy=p[v].Y>mxy?p[v].Y:mxy; any=true; } };
			for (int u : c.users) acc(u,true);
			if (!any) for (int u : c.users) acc(u,false);   // 全 user 不可視時は可視判定を外す
			if (!any) { std::memset(c.matMask,0,64); std::memset(c.matDraw,0,64); continue; }
			float bx=mnx,by=mny,bw=(mxx-mnx>1e-5f)?(mxx-mnx):1e-5f,bh=(mxy-mny>1e-5f)?(mxy-mny):1e-5f;
			const float physW=c.lw*maskPx, physH=c.lh*maskPx; float scaleX,scaleY;
			if (bw*ppu>physW) { bx-=bw*MARGIN; bw+=bw*MARGIN*2; scaleX=c.lw/bw; } else { scaleX=ppu/physW; }
			if (bh*ppu>physH) { by-=bh*MARGIN; bh+=bh*MARGIN*2; scaleY=c.lh/bh; } else { scaleY=ppu/physH; }
			float* fm=c.matMask; std::memset(fm,0,64);   // mask NDC = 2*scale*(p-b) + 2*layout - 1
			fm[0]=2*scaleX; fm[3]=-2*scaleX*bx+2*c.lx-1; fm[5]=2*scaleY; fm[7]=-2*scaleY*by+2*c.ly-1; fm[10]=1; fm[15]=1;
			float* fd=c.matDraw; std::memset(fd,0,64);    // draw: mu.x=scale*(p-b)+layout, clipPos.y=-scaleY*(Y-by)-layoutY (PS が mu.y=1+mu.y)
			fd[0]=scaleX; fd[3]=-scaleX*bx+c.lx; fd[5]=-scaleY; fd[7]=scaleY*by-c.ly; fd[10]=1; fd[15]=1;
			c.boundsNdc[0]=c.lx*2-1; c.boundsNdc[1]=c.ly*2-1; c.boundsNdc[2]=(c.lx+c.lw)*2-1; c.boundsNdc[3]=(c.ly+c.lh)*2-1;
		}
	}
	// packing 経路: マスクバッファ (256²) を一度だけ生成 (全コンテキストを ch/タイルへ)。renderChildren より前。
	void renderMaskBuffer()
	{
		if (!m_packMasks || m_clips.empty()) return;
		barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		setRT(m_maskRtv, kMaskSize, kMaskSize);
		const float w[4]={1,1,1,1}; m_cl->ClearRenderTargetView(m_maskRtv, w, 0, nullptr);
		m_cl->SetPipelineState(m_psoMaskSetup.Get());
		for (int c=0;c<(int)m_clips.size();++c) {
			ClipCtx& cc=m_clips[c];
			Cb* cb=(Cb*)(m_clipCbPtr+(size_t)c*kCbStride); std::memset(cb,0,sizeof(Cb));
			std::memcpy(cb->projectMatrix, cc.matMask, 64);
			cb->baseColor[0]=cc.boundsNdc[0]; cb->baseColor[1]=cc.boundsNdc[1]; cb->baseColor[2]=cc.boundsNdc[2]; cb->baseColor[3]=cc.boundsNdc[3];
			std::memcpy(cb->channelFlag, kChannelColor[cc.channel], 16);
			m_cl->SetGraphicsRootConstantBufferView(0, m_clipCb->GetGPUVirtualAddress()+(UINT64)c*kCbStride);
			for (int md : cc.maskDraws) { const Drawable& mk=m_draws[md];
				m_cl->SetGraphicsRootDescriptorTable(1, gpu(mk.texIndex));
				m_cl->DrawIndexedInstanced((UINT)mk.idxCount,1,(UINT)mk.idxOffset,mk.vtxOffset,0); }
		}
		barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}

	// 公式 ExecuteDrawForDrawable の advanced 経路: dest をコピーして per-pixel で 18色×5alpha 合成。
	void drawDrawableAdvanced(int d, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
	{
		const Drawable& dr=m_draws[d];
		const int dc=(int)m_draws.size();
		// 1) マスク生成 (masked のみ、高精度 256²)
		if (dr.maskCount>0) {
			barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
			setRT(m_maskRtv, kMaskSize, kMaskSize);
			const float w[4]={1,1,1,1}; m_cl->ClearRenderTargetView(m_maskRtv, w, 0, nullptr);
			m_cl->SetPipelineState(m_psoMaskSetup.Get());
			m_cl->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress()+(UINT64)(dc+d)*kCbStride);
			for (int k=0;k<dr.maskCount;++k){ const Drawable& mk=m_draws[dr.masks[k]];
				m_cl->SetGraphicsRootDescriptorTable(1, gpu(mk.texIndex));
				m_cl->DrawIndexedInstanced((UINT)mk.idxCount,1,(UINT)mk.idxOffset,mk.vtxOffset,0); }
			barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		// 2) 現 RT を scratch へコピー (公式 SetTextureViewForDrawable の CopyRenderTarget = blend dest)
		ID3D12Resource* src=curRes();
		barrier(src, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
		barrier(m_scratchRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
		m_cl->CopyResource(m_scratchRT.Get(), src);
		barrier(m_scratchRT.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		barrier(src, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		// 3) 合成ターゲットへ描画。t0=texture、t1=mask、t2=dest copy。REPLACE ステート (シェーダが全合成)。
		setRT(rtv, m_vw, m_vh);
		m_cl->SetGraphicsRootDescriptorTable(1, gpu(dr.texIndex));
		m_cl->SetGraphicsRootDescriptorTable(2, gpu(m_maskSrvIdx));
		m_cl->SetGraphicsRootDescriptorTable(3, gpu(m_scratchSrvIdx));
		m_cl->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress()+(UINT64)d*kCbStride);
		m_cl->SetPipelineState(dr.culling ? m_psoDrawAdvCull.Get() : m_psoDrawAdv.Get());
		m_cl->DrawIndexedInstanced((UINT)dr.idxCount,1,(UINT)dr.idxOffset,dr.vtxOffset,0);
	}

	void drawDrawable(int d, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
	{
		const Drawable& dr=m_draws[d];
		const int dc=(int)m_draws.size();
		if (dr.advanced) { drawDrawableAdvanced(d, rtv); return; }
		auto maskedPso=[&](const Drawable& x)->ID3D12PipelineState*{
			if (x.inverted) return (x.culling?m_psoMaskedInvCull:m_psoMaskedInv).Get();
			return (x.culling?m_psoMaskedCull:m_psoMasked).Get(); };
		if (dr.maskCount>0) {
			if (m_packMasks && dr.clipCtx>=0) {
				// 低精度パッキング: マスクは renderMaskBuffer で生成済み。バインドして描くだけ。
				m_cl->SetGraphicsRootDescriptorTable(2, gpu(m_maskSrvIdx));
				m_cl->SetPipelineState(maskedPso(dr));
			} else {
				// 高精度 (per-drawable): マスクバッファを都度生成 (公式: blend mode 有効時)
				barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
				setRT(m_maskRtv, kMaskSize, kMaskSize);
				const float w[4]={1,1,1,1}; m_cl->ClearRenderTargetView(m_maskRtv, w, 0, nullptr);
				m_cl->SetPipelineState(m_psoMaskSetup.Get());
				m_cl->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress()+(UINT64)(dc+d)*kCbStride);
				for (int k=0;k<dr.maskCount;++k){ const Drawable& mk=m_draws[dr.masks[k]];
					m_cl->SetGraphicsRootDescriptorTable(1, gpu(mk.texIndex));
					m_cl->DrawIndexedInstanced((UINT)mk.idxCount,1,(UINT)mk.idxOffset,mk.vtxOffset,0); }
				barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				setRT(rtv, m_vw, m_vh);
				m_cl->SetGraphicsRootDescriptorTable(2, gpu(m_maskSrvIdx));
				m_cl->SetPipelineState(maskedPso(dr));
			}
		} else {
			m_cl->SetPipelineState(dr.culling ? m_psoCull[dr.blend].Get() : m_pso[dr.blend].Get());
		}
		m_cl->SetGraphicsRootDescriptorTable(1, gpu(dr.texIndex));
		m_cl->SetGraphicsRootConstantBufferView(0, m_cb->GetGPUVirtualAddress()+(UINT64)d*kCbStride);
		m_cl->DrawIndexedInstanced((UINT)dr.idxCount,1,(UINT)dr.idxOffset,dr.vtxOffset,0);
	}

	// オフスクリーン o の RT を rtv (=実リソース dstRes) へ blend mode で合成
	void compositeOffscreen(int o, D3D12_CPU_DESCRIPTOR_HANDLE rtv, ID3D12Resource* dstRes)
	{
		OffG& g=m_offs[o];
		const int color=g.blendInt&0xFF, alpha=(g.blendInt>>8)&0xFF;
		// 公式: 非advanced = Alpha=Over(0) かつ Color∈{Normal(0),AddCompatible(1),MultiplyCompatible(2)} のみ。
		//        Add(3)/Multiply(6) 等は advanced (per-pixel シェーダ) で処理する。
		const bool advanced = !((alpha==0) && (color==0||color==1||color==2));
		const D3D12_GPU_VIRTUAL_ADDRESS cb=m_offCb->GetGPUVirtualAddress()+(UINT64)(1+o)*kCbStride;
		cl()->SetGraphicsRootConstantBufferView(0, cb);
		cl()->SetGraphicsRootDescriptorTable(1, gpu(m_offSrvBase+g.rtIndex));   // t0 = offscreen RT
		if (advanced && dstRes) {
			// dest (rtv の現内容 = dstRes) を scratch へコピーして t1 に bind。入れ子 (親=別オフスクリーン RT)
			// でも dstRes を直接コピーするので正しく advanced blend できる。
			barrier(dstRes, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
			barrier(m_scratchRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
			cl()->CopyResource(m_scratchRT.Get(), dstRes);
			barrier(m_scratchRT.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			barrier(dstRes, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
			setRT(rtv, m_vw, m_vh);
			cl()->SetGraphicsRootDescriptorTable(2, gpu(m_scratchSrvIdx));   // t1 = dest copy (advanced PS が blend dest に使う)
			cl()->SetPipelineState(m_psoAdv.Get());
			cl()->DrawInstanced(3,1,0,0);
			cl()->SetGraphicsRootDescriptorTable(2, gpu(m_maskSrvIdx));
			return;
		}
		int b = (color==1)?1 : (color==2)?2 : 0;
		// ── オフスクリーン自体のマスク (公式 _offscreenClippingManager) : basic 合成をクリップ ──
		const int* omc=csmGetOffscreenMaskCounts(m_model);
		float bx,by,bw,bh;
		if (!advanced && omc[o]>0 && offscreenBounds(o,bx,by,bw,bh)) {
			const int** om=csmGetOffscreenMasks(m_model); const int* ml=om[o]; const int mc=omc[o];
			const int osc=(int)m_offs.size();
			float fm[16],fd[16]; maskMatricesBounds(bx,by,bw,bh,fm,fd);
			// マスク生成 CB (slot 1+osc+o): projectMatrix=fm, baseColor=フル矩形, channelFlag=R
			Cb* mcb=(Cb*)(m_offCbPtr+(size_t)(1+osc+o)*kCbStride); std::memset(mcb,0,sizeof(Cb));
			std::memcpy(mcb->projectMatrix,fm,64);
			mcb->baseColor[0]=-1; mcb->baseColor[1]=-1; mcb->baseColor[2]=1; mcb->baseColor[3]=1; mcb->channelFlag[0]=1.0f;
			// マスク形状を mask buffer へ描く (高精度)
			barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
			setRT(m_maskRtv, kMaskSize, kMaskSize);
			const float w[4]={1,1,1,1}; m_cl->ClearRenderTargetView(m_maskRtv, w, 0, nullptr);
			m_cl->SetPipelineState(m_psoMaskSetup.Get());
			m_cl->SetGraphicsRootConstantBufferView(0, m_offCb->GetGPUVirtualAddress()+(UINT64)(1+osc+o)*kCbStride);
			for (int k=0;k<mc;++k){ const Drawable& mk=m_draws[ml[k]];
				m_cl->SetGraphicsRootDescriptorTable(1, gpu(mk.texIndex));
				m_cl->DrawIndexedInstanced((UINT)mk.idxCount,1,(UINT)mk.idxOffset,mk.vtxOffset,0); }
			barrier(m_maskTex.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			// fd_offscreen = fd ∘ mvp^-1 (フルスクリーン NDC → model → mask UV) を合成 CB の clipMatrix へ
			const float sx=m_mvp[0],sy=m_mvp[5],tx=m_mvp[3],ty=m_mvp[7]; float fdo[16]; std::memset(fdo,0,64);
			fdo[0]=fd[0]/sx; fdo[3]=fd[3]-fd[0]*tx/sx; fdo[5]=fd[5]/sy; fdo[7]=fd[7]-fd[5]*ty/sy; fdo[10]=1; fdo[15]=1;
			Cb* ccb=(Cb*)(m_offCbPtr+(size_t)(1+o)*kCbStride); std::memcpy(ccb->clipMatrix,fdo,64);
			setRT(rtv, m_vw, m_vh);
			cl()->SetGraphicsRootConstantBufferView(0, cb);                       // 合成 CB (slot 1+o)
			cl()->SetGraphicsRootDescriptorTable(1, gpu(m_offSrvBase+g.rtIndex));  // t0 = offscreen RT
			cl()->SetGraphicsRootDescriptorTable(2, gpu(m_maskSrvIdx));            // t1 = mask
			cl()->SetPipelineState(m_psoCompMasked[b].Get());
			cl()->DrawInstanced(3,1,0,0);
			return;
		}
		// 非advanced合成: Normal/Over(0)=premult over、AddCompatible(1)=Add、MultiplyCompatible(2)=Mult
		// を premult シェーダ (PsComposite) + 固定機能ブレンドで。
		cl()->SetPipelineState(m_psoComp[b].Get());
		cl()->DrawInstanced(3,1,0,0);
	}

	// ── リソース ──
	bool createBuffers(ID3D12Device* device, const std::vector<uint16_t>& indices)
	{
		auto up=[&](UINT64 bytes, ComPtrRes& out)->bool{
			D3D12_HEAP_PROPERTIES hp={}; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC rd={}; rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width=bytes; rd.Height=1;
			rd.DepthOrArraySize=1; rd.MipLevels=1; rd.SampleDesc.Count=1; rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			return SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(out.ReleaseAndGetAddressOf())));
		};
		D3D12_RANGE none={0,0};
		// インデックスは静的 (毎フレーム不変) なので 1 本のみ。
		m_ibBytes=(UINT)(indices.size()*2);
		if (!m_ibBytes || !up(m_ibBytes,m_ib)) return false;
		void* p=nullptr; if (FAILED(m_ib->Map(0,&none,&p))) return false; std::memcpy(p,indices.data(),m_ibBytes); m_ib->Unmap(0,nullptr);
		// 動的バッファ (VB/各種 CB) は毎フレーム CPU 上書きするため kFrames 重化 (GPU 読み取りとの競合=ちらつき回避)。
		for (int f=0; f<kFrames; ++f) {
			if (!up((UINT64)m_totalVerts*16, m_vbN[f])) return false;
			if (FAILED(m_vbN[f]->Map(0,&none,(void**)&m_vbPtrN[f]))) return false;
			if (!up((UINT64)m_draws.size()*2*kCbStride, m_cbN[f])) return false;
			if (FAILED(m_cbN[f]->Map(0,&none,(void**)&m_cbPtrN[f]))) return false;
			// offscreen composite CB: slot0=blit, slot1+o=offscreen o
			// slot0=blit, slot[1+o]=offscreen 合成 CB, slot[1+osc+o]=offscreen マスク生成 CB
			if (!up((UINT64)(1+2*m_offs.size())*kCbStride, m_offCbN[f])) return false;
			if (FAILED(m_offCbN[f]->Map(0,&none,(void**)&m_offCbPtrN[f]))) return false;
			// stage sprite CB: 3 slots (背景/歯車/閉じる)
			if (m_hasStage) {
				if (!up((UINT64)3*kCbStride, m_spriteCbN[f])) return false;
				if (FAILED(m_spriteCbN[f]->Map(0,&none,(void**)&m_spriteCbPtrN[f]))) return false;
			}
			// clip-context mask-setup CB (低精度パッキング時、コンテキスト数ぶん)
			if (m_packMasks && !m_clips.empty()) {
				if (!up((UINT64)m_clips.size()*kCbStride, m_clipCbN[f])) return false;
				if (FAILED(m_clipCbN[f]->Map(0,&none,(void**)&m_clipCbPtrN[f]))) return false;
			}
		}
		selectFrame(0);
		return true;
	}

	bool createTextures(ID3D12Device* device, ID3D12GraphicsCommandList* cl, const char* const* texPaths, int texCount)
	{
		if (texCount<=0) return false;
		// SRV heap: textures + mask + model + scratch + interesting offscreen RTs + stage sprites
		m_texCount=texCount;
		m_maskSrvIdx=texCount; m_modelSrvIdx=texCount+1; m_scratchSrvIdx=texCount+2; m_offSrvBase=texCount+3;
		m_spriteSrvBase=texCount+3+m_interestingCount;
		const int spriteN=m_hasStage?3:0;
		const int total=texCount+3+m_interestingCount+spriteN;
		D3D12_DESCRIPTOR_HEAP_DESC hd={}; hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		hd.NumDescriptors=(UINT)total; hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(m_srvHeap.GetAddressOf())))) return false;
		m_srvInc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		// RTV heap: mask + model + scratch + interesting offscreen RTs
		D3D12_DESCRIPTOR_HEAP_DESC rh={}; rh.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rh.NumDescriptors=(UINT)(3+m_interestingCount);
		if (FAILED(device->CreateDescriptorHeap(&rh,IID_PPV_ARGS(m_rtvHeap.GetAddressOf())))) return false;
		m_rtvInc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		D3D12_CPU_DESCRIPTOR_HANDLE cpu=m_srvHeap->GetCPUDescriptorHandleForHeapStart();
		for (int t=0;t<texCount;++t){
			int w=0,h=0,ch=0; unsigned char* px=stbi_load(texPaths[t],&w,&h,&ch,4);
			if (!px){ std::fprintf(stderr,"[Live2D] tex load failed: %s\n",texPaths[t]); return false; }
			ComPtrRes tex; if (!makeTexture(device,cl,w,h,px,tex)){ stbi_image_free(px); return false; } stbi_image_free(px);
			srvAt(device,tex.Get(),cpu); cpu.ptr+=m_srvInc; m_textures.push_back(tex);
		}
		// mask RT (2048², 固定)
		if (!makeRT(device,kMaskSize,kMaskSize,m_maskTex,0,m_maskRtv,m_maskSrvIdx,true)) return false;
		// ステージスプライト (背景/歯車/閉じる)。公式 LAppView 相当
		if (m_hasStage) {
			const char* sp[3]={m_stageBg.c_str(),m_stageGear.c_str(),m_stageClose.c_str()};
			for (int s=0;s<3;++s){
				int w=0,h=0,c=0; unsigned char* px=stbi_load(sp[s],&w,&h,&c,4);
				if (!px){ std::fprintf(stderr,"[Live2D] stage tex failed: %s\n",sp[s]); m_hasStage=false; break; }
				ComPtrRes tex; if(!makeTexture(device,cl,w,h,px,tex)){ stbi_image_free(px); m_hasStage=false; break; } stbi_image_free(px);
				D3D12_CPU_DESCRIPTOR_HANDLE sh=m_srvHeap->GetCPUDescriptorHandleForHeapStart(); sh.ptr+=(UINT64)(m_spriteSrvBase+s)*m_srvInc;
				srvAt(device,tex.Get(),sh); m_textures.push_back(tex); m_spriteW[s]=w; m_spriteH[s]=h;
			}
		}
		return true;
	}

	// 公式 LAppView 相当のステージスプライトを backbuffer へ描く (背景→歯車→閉じる、モデルより前)。
	void drawStage(int vw, int vh)
	{
		if (!m_hasStage || !m_spriteCbPtr) return;
		auto rectMat=[&](float cxPx,float cyPx,float wPx,float hPx,float* m){
			const float ndcL=(cxPx-wPx*0.5f)/vw*2.0f-1.0f, ndcR=(cxPx+wPx*0.5f)/vw*2.0f-1.0f;
			const float ndcT=1.0f-(cyPx-hPx*0.5f)/vh*2.0f, ndcB=1.0f-(cyPx+hPx*0.5f)/vh*2.0f;   // top-left origin
			std::memset(m,0,64); m[0]=ndcR-ndcL; m[3]=ndcL; m[5]=ndcB-ndcT; m[7]=ndcT; m[10]=1.0f; m[15]=1.0f;
		};
		const float bgH=0.95f*vh, bgW=(m_spriteH[0]>0)? m_spriteW[0]*(bgH/m_spriteH[0]) : bgH;   // 背景: 高さ95%、縦横比維持
		struct Sp{ float cx,cy,w,h; }; const Sp sps[3]={
			{ vw*0.5f, vh*0.5f, bgW, bgH },                                              // 背景: 中央
			{ vw-m_spriteW[1]*0.5f, vh-m_spriteH[1]*0.5f, (float)m_spriteW[1], (float)m_spriteH[1] }, // 歯車: 右下
			{ vw-m_spriteW[2]*0.5f, m_spriteH[2]*0.5f,    (float)m_spriteW[2], (float)m_spriteH[2] },  // 閉じる: 右上
		};
		m_cl->SetPipelineState(m_psoSprite.Get());
		for (int s=0;s<3;++s){
			Cb* cb=(Cb*)(m_spriteCbPtr+(size_t)s*kCbStride); std::memset(cb,0,sizeof(Cb));
			rectMat(sps[s].cx,sps[s].cy,sps[s].w,sps[s].h, cb->projectMatrix);
			cb->baseColor[0]=cb->baseColor[1]=cb->baseColor[2]=cb->baseColor[3]=1.0f;
			m_cl->SetGraphicsRootConstantBufferView(0, m_spriteCb->GetGPUVirtualAddress()+(UINT64)s*kCbStride);
			m_cl->SetGraphicsRootDescriptorTable(1, gpu(m_spriteSrvBase+s));
			m_cl->DrawInstanced(6,1,0,0);
		}
	}

	bool ensureRTs(int w, int h)
	{
		if (m_rtW==w && m_rtH==h && m_modelRT) return true;
		m_rtW=w; m_rtH=h;
		int rtvSlot=1; // 0=mask
		if (!makeRT(m_device,w,h,m_modelRT,rtvSlot++,m_modelRtv,m_modelSrvIdx,false)) return false;
		if (!makeRT(m_device,w,h,m_scratchRT,rtvSlot++,m_scratchRtv,m_scratchSrvIdx,false)) return false;
		m_offRT.assign(m_interestingCount,{}); m_offRtv.assign(m_interestingCount,{});
		for (auto& g : m_offs) if (g.interesting) {
			if (!makeRT(m_device,w,h,m_offRT[g.rtIndex],rtvSlot++,m_offRtv[g.rtIndex],m_offSrvBase+g.rtIndex,false)) return false;
		}
		return true;
	}

	bool makeRT(ID3D12Device* device,int w,int h,ComPtrRes& tex,int rtvSlot,
	            D3D12_CPU_DESCRIPTOR_HANDLE& rtvOut,int srvIdx,bool clearWhite)
	{
		D3D12_HEAP_PROPERTIES dh={}; dh.Type=D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC td={}; td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width=(UINT64)w; td.Height=(UINT)h; td.DepthOrArraySize=1; td.MipLevels=1;
		td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE cv={}; cv.Format=td.Format; float c=clearWhite?1.0f:0.0f; cv.Color[0]=cv.Color[1]=cv.Color[2]=cv.Color[3]=c;
		if (FAILED(device->CreateCommittedResource(&dh,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&cv,IID_PPV_ARGS(tex.ReleaseAndGetAddressOf())))) return false;
		rtvOut=m_rtvHeap->GetCPUDescriptorHandleForHeapStart(); rtvOut.ptr+=(UINT64)rtvSlot*m_rtvInc;
		device->CreateRenderTargetView(tex.Get(),nullptr,rtvOut);
		D3D12_CPU_DESCRIPTOR_HANDLE s=m_srvHeap->GetCPUDescriptorHandleForHeapStart(); s.ptr+=(UINT64)srvIdx*m_srvInc;
		srvAt(device,tex.Get(),s);
		return true;
	}
	static void srvAt(ID3D12Device* device, ID3D12Resource* r, D3D12_CPU_DESCRIPTOR_HANDLE h){
		D3D12_SHADER_RESOURCE_VIEW_DESC sv={}; sv.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels=1;
		device->CreateShaderResourceView(r,&sv,h);
	}

	bool makeTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cl, int w, int h, const unsigned char* px, ComPtrRes& out)
	{
		D3D12_HEAP_PROPERTIES dh={}; dh.Type=D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC td={}; td.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width=(UINT64)w; td.Height=(UINT)h;
		td.DepthOrArraySize=1; td.MipLevels=1; td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1;
		if (FAILED(device->CreateCommittedResource(&dh,D3D12_HEAP_FLAG_NONE,&td,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(out.ReleaseAndGetAddressOf())))) return false;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp={}; UINT64 total=0; device->GetCopyableFootprints(&td,0,1,0,&fp,nullptr,nullptr,&total);
		ComPtrRes upl; D3D12_HEAP_PROPERTIES uh={}; uh.Type=D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bd={}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=total; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1; bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(device->CreateCommittedResource(&uh,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(upl.GetAddressOf())))) return false;
		uint8_t* map=nullptr; D3D12_RANGE none={0,0}; upl->Map(0,&none,(void**)&map);
		for (int y=0;y<h;++y) std::memcpy(map+fp.Offset+(UINT64)y*fp.Footprint.RowPitch, px+(size_t)y*w*4, (size_t)w*4);
		upl->Unmap(0,nullptr);
		D3D12_TEXTURE_COPY_LOCATION dst={}; dst.pResource=out.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex=0;
		D3D12_TEXTURE_COPY_LOCATION src={}; src.pResource=upl.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint=fp;
		cl->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
		barrierOn(cl,out.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		m_uploads.push_back(upl); return true;
	}

	bool createPipeline(ID3D12Device* device);   // (下で定義)

	void writeVertices()
	{
		if (!m_vbPtr||!m_model) return;
		const csmVector2** pos=csmGetDrawableVertexPositions(m_model); const csmVector2** uv=csmGetDrawableVertexUvs(m_model);
		for (size_t d=0;d<m_draws.size();++d){ const csmVector2* p=pos[d]; const csmVector2* u=uv[d];
			float* o=m_vbPtr+(size_t)m_draws[d].vtxOffset*4;
			for (int v=0;v<m_draws[d].vtxCount;++v){ o[v*4]=p[v].X; o[v*4+1]=p[v].Y; o[v*4+2]=u[v].X; o[v*4+3]=u[v].Y; } }
	}
	void computeMvp(int vw,int vh)
	{
		if (m_mvpComputed && vw==m_mvpW && vh==m_mvpH) return;
		// 公式 LAppLive2DManager::OnUpdate と完全同一のフレーミング (全プラットフォーム共通の絵にする):
		//   modelMatrix = Scale(2/canvasH) [CubismModelMatrix の SetHeight(2)、頂点は原点中心]
		//   projection  = 横長(既定): Scale(H/W, 1) / 縦長かつ canvasW>1: SetWidth(2)+Scale(1, W/H)
		//   MVP = projection * modelMatrix、translate 0 → キャンバスを画面高に合わせ中央配置。
		csmVector2 sizePx{}, originPx{}; float ppu=1.0f;
		csmReadCanvasInfo(m_model, &sizePx, &originPx, &ppu);
		const float Cw = (ppu>0.0f)? sizePx.X/ppu : 2.0f;
		const float Ch = (ppu>0.0f)? sizePx.Y/ppu : 2.0f;
		float sx, sy;
		if (Cw>1.0f && vw<vh) { const float base=2.0f/Cw; sx=base; sy=base*((float)vw/(float)vh); }   // 縦長
		else                  { const float base=2.0f/Ch; sx=base*((float)vh/(float)vw); sy=base; }   // 横長(既定)
		std::memset(m_mvp,0,64);
		m_mvp[0]=sx; m_mvp[5]=sy; m_mvp[10]=1.0f; m_mvp[15]=1.0f;   // translate 0 (原点中心)
		m_mvpComputed=true; m_mvpW=vw; m_mvpH=vh;
	}
	void maskMatrices(int d,float fm[16],float fd[16])
	{
		const csmVector2** pos=csmGetDrawableVertexPositions(m_model); const csmVector2* p=pos[d];
		float mnx=1e30f,mny=1e30f,mxx=-1e30f,mxy=-1e30f;
		for (int v=0;v<m_draws[d].vtxCount;++v){ mnx=p[v].X<mnx?p[v].X:mnx; mxx=p[v].X>mxx?p[v].X:mxx; mny=p[v].Y<mny?p[v].Y:mny; mxy=p[v].Y>mxy?p[v].Y:mxy; }
		maskMatricesBounds(mnx,mny,mxx-mnx,mxy-mny,fm,fd);
	}
	// 与えられた model 空間 bbox からマスク生成行列 fm (model→mask NDC) とサンプリング行列 fd (model→mask UV) を作る
	void maskMatricesBounds(float bx,float by,float bw,float bh,float fm[16],float fd[16])
	{
		bx-=0.05f*bw; by-=0.05f*bh; bw*=1.1f; bh*=1.1f;
		if (bw<1e-4f)bw=1e-4f; if (bh<1e-4f)bh=1e-4f; float sX=1/bw,sY=1/bh;
		std::memset(fm,0,64); fm[0]=sX*2; fm[3]=-bx*sX*2-1; fm[5]=sY*2; fm[7]=-by*sY*2-1; fm[10]=1; fm[15]=1;
		std::memset(fd,0,64); fd[0]=sX; fd[3]=-bx*sX; fd[5]=-sY; fd[7]=by*sY; fd[10]=1; fd[15]=1;
	}
	// オフスクリーン o の subtree (内包する全 drawable) の可視 bbox を model 空間で返す
	bool offscreenBounds(int o,float& bx,float& by,float& bw,float& bh)
	{
		const csmVector2** pos=csmGetDrawableVertexPositions(m_model);
		float mnx=1e30f,mny=1e30f,mxx=-1e30f,mxy=-1e30f; bool any=false;
		for (int d=0; d<(int)m_draws.size(); ++d) {
			int x=m_draws[d].ownerOff, g=0; bool in=false; while(x>=0&&g++<4096){ if(x==o){in=true;break;} x=m_offs[x].parentOff; }
			if (!in || !(m_dyn[d]&csmIsVisible)) continue;
			const csmVector2* p=pos[d]; for (int v=0;v<m_draws[d].vtxCount;++v){ mnx=p[v].X<mnx?p[v].X:mnx; mxx=p[v].X>mxx?p[v].X:mxx; mny=p[v].Y<mny?p[v].Y:mny; mxy=p[v].Y>mxy?p[v].Y:mxy; any=true; } }
		if (!any) return false;
		bx=mnx; by=mny; bw=mxx-mnx; bh=mxy-mny; return true;
	}
	void writeConstants()
	{
		if (!m_cbPtr||!m_model) return;
		const float* op=csmGetDrawableOpacities(m_model); const csmVector4* mul=csmGetDrawableMultiplyColors(m_model); const csmVector4* scr=csmGetDrawableScreenColors(m_model);
		const int dc=(int)m_draws.size(); float fm[16],fd[16];
		for (int d=0;d<dc;++d){
			Cb* cb=(Cb*)(m_cbPtr+(size_t)d*kCbStride); std::memset(cb,0,sizeof(Cb));
			std::memcpy(cb->projectMatrix,m_mvp,64);
			cb->clipMatrix[0]=cb->clipMatrix[5]=cb->clipMatrix[10]=cb->clipMatrix[15]=1.0f;
			cb->baseColor[0]=cb->baseColor[1]=cb->baseColor[2]=1; cb->baseColor[3]=op[d];
			cb->multiplyColor[0]=mul[d].X; cb->multiplyColor[1]=mul[d].Y; cb->multiplyColor[2]=mul[d].Z; cb->multiplyColor[3]=mul[d].W;
			cb->screenColor[0]=scr[d].X; cb->screenColor[1]=scr[d].Y; cb->screenColor[2]=scr[d].Z; cb->screenColor[3]=scr[d].W;
			if (m_draws[d].advanced){   // per-drawable advanced blend: color/alpha タイプ + masked/inverted を渡す
				const int bf=m_draws[d].blendModeFull;
				cb->blendMode[0]=(float)(bf&0xFF); cb->blendMode[1]=(float)((bf>>8)&0xFF);
				cb->blendMode[2]=(m_draws[d].maskCount>0)?1.0f:0.0f; cb->blendMode[3]=m_draws[d].inverted?1.0f:0.0f;
			}
			if (m_draws[d].maskCount>0){
				if (m_packMasks && m_draws[d].clipCtx>=0){
					// 低精度パッキング: コンテキストの draw 行列 + チャンネルを使う (マスクは別 pre-pass で生成)
					const ClipCtx& cc=m_clips[m_draws[d].clipCtx];
					std::memcpy(cb->clipMatrix, cc.matDraw, 64);
					std::memcpy(cb->channelFlag, kChannelColor[cc.channel], 16);
				} else {
					// 高精度 (per-drawable): mask 行列 + setup CB を都度作る
					maskMatrices(d,fm,fd); std::memcpy(cb->clipMatrix,fd,64); cb->channelFlag[0]=1.0f;
					Cb* sc=(Cb*)(m_cbPtr+(size_t)(dc+d)*kCbStride); std::memset(sc,0,sizeof(Cb)); std::memcpy(sc->projectMatrix,fm,64);
					sc->baseColor[0]=-1; sc->baseColor[1]=-1; sc->baseColor[2]=1; sc->baseColor[3]=1; sc->channelFlag[0]=1.0f;
				}
			}
		}
	}
	void writeOffscreenCBs()
	{
		// slot0 = blit (baseColor=1, blendType=0)
		Cb* b=(Cb*)(m_offCbPtr); std::memset(b,0,sizeof(Cb)); b->baseColor[0]=b->baseColor[1]=b->baseColor[2]=b->baseColor[3]=1.0f;
		for (size_t o=0;o<m_offs.size();++o){ OffG& g=m_offs[o]; Cb* c=(Cb*)(m_offCbPtr+(size_t)(1+o)*kCbStride); std::memset(c,0,sizeof(Cb));
			float op=g.opacity; c->baseColor[0]=c->baseColor[1]=c->baseColor[2]=c->baseColor[3]=op;
			std::memcpy(c->multiplyColor,g.mult,16); std::memcpy(c->screenColor,g.screen,16);
			c->channelFlag[0]=(float)(g.blendInt&0xFF); c->channelFlag[1]=(float)((g.blendInt>>8)&0xFF); }
	}

	// helpers
	ID3D12GraphicsCommandList* cl(){ return m_cl; }
	D3D12_GPU_DESCRIPTOR_HANDLE gpu(int idx){ D3D12_GPU_DESCRIPTOR_HANDLE h=m_srvHeap->GetGPUDescriptorHandleForHeapStart(); h.ptr+=(UINT64)idx*m_srvInc; return h; }
	void setRT(D3D12_CPU_DESCRIPTOR_HANDLE rtv,int w,int h){ m_cl->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
		D3D12_VIEWPORT vp={0,0,(float)w,(float)h,0,1}; D3D12_RECT sc={0,0,w,h}; m_cl->RSSetViewports(1,&vp); m_cl->RSSetScissorRects(1,&sc); }
	static void barrierOn(ID3D12GraphicsCommandList* cl,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){
		D3D12_RESOURCE_BARRIER bar={}; bar.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		bar.Transition.pResource=r; bar.Transition.StateBefore=a; bar.Transition.StateAfter=b; bar.Transition.Subresource=0;
		cl->ResourceBarrier(1,&bar);
	}
	void barrier(ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){ barrierOn(m_cl,r,a,b); }

	// 状態
	ID3D12Device* m_device=nullptr;
	csmModel* m_model=nullptr;   // 非所有: Framework (live2d::Live2DModel) が所有・更新する
	bool m_ready=false,m_failed=false;
	std::vector<Drawable> m_draws; std::vector<OffG> m_offs;
	// 公式 DrawObjectLoop の状態
	std::vector<int> m_sortedIdx, m_sortedType;   // render order スロット → (object index, type)
	std::vector<int> m_dPart, m_pParent, m_oOwner; // part 階層 (drawable親part / part親part / offscreen owner part)
	int m_curOff=-1;                               // 現在のオフスクリーン (-1 = モデル RT)
	int m_totalVerts=0, m_interestingCount=0, m_texCount=0;
	ComPtrRes m_ib; UINT m_ibBytes=0;
	// 動的バッファは kFrames 重化 (毎フレーム CPU 上書き ⇄ GPU 読み取りの競合=ちらつき回避)。
	// m_vb/m_cb 等は「現フレーム」の生ポインタで、selectFrame() が配列から差し替える。
	ComPtrRes m_vbN[kFrames],m_cbN[kFrames],m_offCbN[kFrames],m_clipCbN[kFrames],m_spriteCbN[kFrames];
	float* m_vbPtrN[kFrames]={}; uint8_t* m_cbPtrN[kFrames]={},*m_offCbPtrN[kFrames]={},*m_clipCbPtrN[kFrames]={},*m_spriteCbPtrN[kFrames]={};
	ID3D12Resource *m_vb=nullptr,*m_cb=nullptr,*m_offCb=nullptr,*m_clipCb=nullptr,*m_spriteCb=nullptr;
	float* m_vbPtr=nullptr; uint8_t* m_cbPtr=nullptr,*m_offCbPtr=nullptr,*m_clipCbPtr=nullptr,*m_spriteCbPtr=nullptr;
	int m_frame=0;
	std::vector<ClipCtx> m_clips; bool m_packMasks=false;   // 低精度クリップパッキング (公式 CubismClippingManager)
	std::vector<ComPtrRes> m_textures,m_uploads;
	ComPtr<ID3D12DescriptorHeap> m_srvHeap,m_rtvHeap; UINT m_srvInc=0,m_rtvInc=0;
	int m_maskSrvIdx=0,m_modelSrvIdx=0,m_scratchSrvIdx=0,m_offSrvBase=0;
	ComPtrRes m_maskTex,m_modelRT,m_scratchRT; std::vector<ComPtrRes> m_offRT;
	D3D12_CPU_DESCRIPTOR_HANDLE m_maskRtv{},m_modelRtv{},m_scratchRtv{}; std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_offRtv;
	int m_rtW=0,m_rtH=0;
	int m_ss=1;   // 公式はネイティブ解像度描画 (=1)。2 にすると 2× SSAA で縁を AA する拡張。
	ComPtr<ID3D12RootSignature> m_rootSig;
	ComPtr<ID3D12PipelineState> m_pso[3],m_psoCull[3],m_psoMaskSetup,m_psoMasked,m_psoMaskedInv,m_psoMaskedCull,m_psoMaskedInvCull,m_psoComp[3],m_psoCompMasked[3],m_psoAdv,m_psoDrawAdv,m_psoDrawAdvCull,m_psoBlit,m_psoSprite;
	float m_mvp[16]={};
	bool m_mvpComputed=false; int m_mvpW=0,m_mvpH=0;   // 枠 (フレーミング) を一度確定して固定
	// ステージスプライト (公式 LAppView 相当: 背景/歯車/閉じる)
	std::string m_stageBg,m_stageGear,m_stageClose; bool m_hasStage=false;
	int m_spriteSrvBase=0, m_spriteW[3]={0,0,0}, m_spriteH[3]={0,0,0};
	// per-frame
	ID3D12GraphicsCommandList* m_cl=nullptr; int m_vw=0,m_vh=0;
	const csmFlags* m_dyn=nullptr; const float* m_op=nullptr;
};

}  // namespace mitiru::render

#include <mitiru/render/dx12/DX12Live2DPipeline.inl>

#endif
