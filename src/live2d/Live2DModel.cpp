/// @file Live2DModel.cpp
/// @brief Cubism Framework を用いた Live2D モデルの完全ロード + 毎フレーム更新の実装。
/// @details 公式 Cubism Native サンプル (LAppModel) と同等の挙動を Framework で実現する:
///            model3.json 一括ロード → moc / テクスチャ / モーション(Idle/TapBody) / physics /
///            目パチ / 呼吸 / ドラッグ追従。毎フレーム LoadParameters → motion → SaveParameters →
///            eyeblink(!motionUpdated) → drag(look) → breath → physics → csmUpdateModel の順で更新。
///          描画はしない (描画は dx12/DX12Live2D.hpp が coreModel() の csmModel を読んで D3D12 で行う)。
///
///          Cubism Core は Framework 内で `Live2D::Cubism::Core` 名前空間にラップされる一方、
///          レンダラ側はグローバルな <Live2DCubismCore.h> を include する。両者を同一 TU に混ぜると
///          pragma once により一方の宣言が失われるため、本ファイル (Framework 側) を独立 TU とし、
///          公開ヘッダ Live2DModel.hpp には Cubism 型を一切出さない (pimpl)。
///          非GL Framework スタティックライブラリにコンパイルされる。

#ifdef MITIRU_HAS_CUBISM_FRAMEWORK

#include <mitiru/render/live2d/Live2DModel.hpp>

#include <CubismFramework.hpp>
#include <CubismDefaultParameterId.hpp>
#include <CubismModelSettingJson.hpp>
#include <ICubismAllocator.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismMoc.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <Motion/CubismMotionQueueManager.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismPose.hpp>
#include <Math/CubismTargetPoint.hpp>
#include <Type/csmString.hpp>
#include <Type/csmMap.hpp>
#include <Type/csmVector.hpp>
#include <Utils/CubismString.hpp>
#include <Rendering/CubismRenderer.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace Csm = Live2D::Cubism::Framework;

// ─────────────────────────────────────────────────────────────────────────
// 1) アロケータ / Framework 起動 / レンダラ無し用スタブ
// ─────────────────────────────────────────────────────────────────────────
namespace
{
class MitiruAllocator : public Csm::ICubismAllocator
{
public:
	void* Allocate(const Csm::csmSizeType size) override { return std::malloc(size); }
	void  Deallocate(void* memory) override { std::free(memory); }
	void* AllocateAligned(const Csm::csmSizeType size, const Csm::csmUint32 alignment) override
	{
		return _aligned_malloc(size, alignment);   // 引数順は (size, alignment)
	}
	void  DeallocateAligned(void* alignedMemory) override { _aligned_free(alignedMemory); }
};

MitiruAllocator g_allocator;
bool            g_started = false;

void cubismLog(const char* message) { std::fprintf(stderr, "[Cubism] %s", message); }

void ensureFrameworkStarted()
{
	if (g_started) { return; }
	static Csm::CubismFramework::Option option;
	option.LogFunction  = cubismLog;
	option.LoggingLevel = Csm::CubismFramework::Option::LogLevel_Warning;
	Csm::CubismFramework::StartUp(&g_allocator, &option);
	Csm::CubismFramework::Initialize();
	g_started = true;
	std::fprintf(stderr, "[Live2D] Cubism Framework started\n");
}

// ファイル全体をバイト列で読む (呼び出し側で freeBytes する)。Create 系は内部で aligned copy を
// 作るため入力バッファのアライメントは不要。
Csm::csmByte* readFileBytes(const std::string& path, Csm::csmSizeInt* outSize)
{
	std::ifstream f(path.c_str(), std::ios::binary | std::ios::ate);
	if (!f) { *outSize = 0; return nullptr; }
	const std::streamsize sz = f.tellg();
	f.seekg(0);
	auto* buf = new Csm::csmByte[static_cast<size_t>(sz)];
	if (!f.read(reinterpret_cast<char*>(buf), sz)) { delete[] buf; *outSize = 0; return nullptr; }
	*outSize = static_cast<Csm::csmSizeInt>(sz);
	return buf;
}
void freeBytes(Csm::csmByte* p) { delete[] p; }

// モーション優先度 / グループ名 (公式サンプル LAppDefine 相当)
constexpr Csm::csmInt32 kPriorityIdle   = 1;
constexpr Csm::csmInt32 kPriorityNormal = 2;
constexpr Csm::csmInt32 kPriorityForce  = 3;
constexpr const char*   kGroupIdle      = "Idle";
constexpr const char*   kGroupTapBody   = "TapBody";
}  // namespace

// レンダラバックエンドを 1 つもコンパイルしないため、CubismFramework::Dispose() が参照する
// Rendering::CubismRenderer::StaticRelease() の実体が無くリンクできない。空実体を与える
// (Dispose を呼ばなくても CubismFramework.obj が常にリンクされ未解決になるため必須)。
namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {
void CubismRenderer::StaticRelease() {}
}}}}

// ─────────────────────────────────────────────────────────────────────────
// 2) Impl: Framework オブジェクト一式と更新ロジック
// ─────────────────────────────────────────────────────────────────────────
namespace mitiru::live2d
{
struct Live2DModel::Impl
{
	Csm::CubismModelSettingJson* setting = nullptr;
	Csm::CubismMoc*              moc      = nullptr;
	Csm::CubismModel*            model    = nullptr;
	Csm::CubismMotionManager*    motionManager = nullptr;
	Csm::CubismPhysics*          physics  = nullptr;
	Csm::CubismPose*             pose     = nullptr;
	Csm::CubismEyeBlink*         eyeBlink = nullptr;
	Csm::CubismBreath*           breath   = nullptr;
	Csm::CubismTargetPoint*      drag     = nullptr;
	Csm::csmMap<Csm::csmString, Csm::ACubismMotion*> motions;
	Csm::csmVector<Csm::CubismIdHandle> eyeBlinkIds, lipSyncIds;

	// ドラッグ追従で AddParameterValue するパラメータ ID
	Csm::CubismIdHandle idAngleX = nullptr, idAngleY = nullptr, idAngleZ = nullptr;
	Csm::CubismIdHandle idBodyX = nullptr, idEyeX = nullptr, idEyeY = nullptr;

	std::string modelDir;
	std::string idleGroup, tapGroup;   // 実在するグループ名 (Idle/TapBody 無ければ先頭グループ)

	std::chrono::steady_clock::time_point lastTime;
	bool hasLast = false;

	float deltaSeconds()
	{
		const auto now = std::chrono::steady_clock::now();
		if (!hasLast) { lastTime = now; hasLast = true; return 1.0f / 60.0f; }
		float d = std::chrono::duration<float>(now - lastTime).count();
		lastTime = now;
		if (d > 0.1f) { d = 0.1f; }   // スパイク抑制
		if (d < 0.0f) { d = 0.0f; }
		return d;
	}

	Csm::CubismMotionQueueEntryHandle startMotion(const char* group, Csm::csmInt32 no, Csm::csmInt32 priority)
	{
		if (priority == kPriorityForce) { motionManager->SetReservePriority(priority); }
		else if (!motionManager->ReserveMotion(priority)) { return Csm::InvalidMotionQueueEntryHandleValue; }

		const Csm::csmString name = Csm::Utils::CubismString::GetFormatedString("%s_%d", group, no);
		auto* motion = static_cast<Csm::CubismMotion*>(motions[name.GetRawString()]);
		bool autoDelete = false;
		if (motion == nullptr)
		{
			Csm::csmSizeInt size = 0;
			Csm::csmByte* buf = readFileBytes(modelDir + setting->GetMotionFileName(group, no), &size);
			if (buf == nullptr) { return Csm::InvalidMotionQueueEntryHandleValue; }
			motion = Csm::CubismMotion::Create(buf, size);
			if (motion != nullptr) { motion->SetEffectIds(eyeBlinkIds, lipSyncIds); autoDelete = true; }
			freeBytes(buf);
			if (motion == nullptr) { return Csm::InvalidMotionQueueEntryHandleValue; }
		}
		return motionManager->StartMotionPriority(motion, autoDelete, priority);
	}

	Csm::CubismMotionQueueEntryHandle startRandomMotion(const char* group, Csm::csmInt32 priority)
	{
		if (group == nullptr || group[0] == '\0') { return Csm::InvalidMotionQueueEntryHandleValue; }
		const Csm::csmInt32 count = setting->GetMotionCount(group);
		if (count <= 0) { return Csm::InvalidMotionQueueEntryHandleValue; }
		const Csm::csmInt32 no = std::rand() % count;
		return startMotion(group, no, priority);
	}

	void update(float dragX, float dragY)
	{
		const float dt = deltaSeconds();
		bool motionUpdated = false;

		model->LoadParameters();                                   // 前回保存した状態を復元
		if (motionManager->IsFinished())
		{
			startRandomMotion(idleGroup.c_str(), kPriorityIdle);   // 何も再生していなければ idle をループ
		}
		else
		{
			motionUpdated = motionManager->UpdateMotion(model, dt);
		}
		model->SaveParameters();                                   // motion 適用後を保存

		// エフェクト (CubismUpdateOrder: EyeBlink 200 → Look 400 → Breath 500 → Physics 600)
		if (eyeBlink != nullptr && !motionUpdated)                 // motion が目を駆動中は自動目パチ抑制
		{
			eyeBlink->UpdateParameters(model, dt);
		}

		drag->Set(dragX, dragY);                                   // 注視ターゲット
		drag->Update(dt);
		const float dx = drag->GetX();
		const float dy = drag->GetY();
		model->AddParameterValue(idAngleX, dx * 30.0f);            // 公式 CubismLook と同係数
		model->AddParameterValue(idAngleY, dy * 30.0f);
		model->AddParameterValue(idAngleZ, dx * dy * -30.0f);
		model->AddParameterValue(idBodyX,  dx * 10.0f);
		model->AddParameterValue(idEyeX,   dx);
		model->AddParameterValue(idEyeY,   dy);

		if (breath  != nullptr) { breath->UpdateParameters(model, dt); }
		if (physics != nullptr) { physics->Evaluate(model, dt); }
		if (pose    != nullptr) { pose->UpdateParameters(model, dt); }   // パーツ表示切替 (例: 腕の前後)

		model->Update();                                           // csmUpdateModel — drawable mesh 更新
	}

	void releaseAll()
	{
		for (auto it = motions.Begin(); it != motions.End(); ++it)
		{
			if (it->Second != nullptr) { Csm::ACubismMotion::Delete(it->Second); }
		}
		motions.Clear();
		if (motionManager != nullptr) { CSM_DELETE(motionManager); motionManager = nullptr; }
		if (drag != nullptr)          { CSM_DELETE(drag);          drag = nullptr; }
		if (breath != nullptr)        { Csm::CubismBreath::Delete(breath);     breath = nullptr; }
		if (eyeBlink != nullptr)      { Csm::CubismEyeBlink::Delete(eyeBlink); eyeBlink = nullptr; }
		if (pose != nullptr)          { Csm::CubismPose::Delete(pose);         pose = nullptr; }
		if (physics != nullptr)       { Csm::CubismPhysics::Delete(physics);   physics = nullptr; }
		if (model != nullptr && moc != nullptr) { moc->DeleteModel(model); model = nullptr; }
		if (moc != nullptr)           { Csm::CubismMoc::Delete(moc); moc = nullptr; }
		if (setting != nullptr)       { CSM_DELETE(setting); setting = nullptr; }
	}
};

// ─────────────────────────────────────────────────────────────────────────
// 3) Live2DModel 公開メソッド
// ─────────────────────────────────────────────────────────────────────────
Live2DModel::~Live2DModel() { unload(); }

void Live2DModel::unload()
{
	if (m_impl != nullptr) { m_impl->releaseAll(); delete m_impl; m_impl = nullptr; }
	m_texPaths.clear();
	m_ready = false;
}

bool Live2DModel::load(const char* model3jsonPath)
{
	using namespace Csm;
	using namespace Csm::DefaultParameterId;

	unload();
	if (model3jsonPath == nullptr) { return false; }
	ensureFrameworkStarted();

	auto* impl = new Impl();

	// モデルディレクトリ (末尾 '/' 付き)
	std::string path(model3jsonPath);
	const size_t slash = path.find_last_of("/\\");
	impl->modelDir = (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);

	// (a) model3.json → setting
	csmSizeInt size = 0;
	csmByte* buf = readFileBytes(path, &size);
	if (buf == nullptr) { std::fprintf(stderr, "[Live2D] model3.json open failed: %s\n", model3jsonPath); delete impl; return false; }
	impl->setting = CSM_NEW CubismModelSettingJson(buf, size);
	freeBytes(buf);

	// (b) moc → CubismMoc → CubismModel
	buf = readFileBytes(impl->modelDir + impl->setting->GetModelFileName(), &size);
	if (buf == nullptr) { std::fprintf(stderr, "[Live2D] moc open failed\n"); impl->releaseAll(); delete impl; return false; }
	impl->moc = CubismMoc::Create(buf, size, false);
	freeBytes(buf);
	if (impl->moc == nullptr) { std::fprintf(stderr, "[Live2D] CubismMoc::Create failed\n"); impl->releaseAll(); delete impl; return false; }
	impl->model = impl->moc->CreateModel();
	if (impl->model == nullptr) { std::fprintf(stderr, "[Live2D] CreateModel failed\n"); impl->releaseAll(); delete impl; return false; }
	impl->model->SaveParameters();

	// (c) physics
	if (std::strcmp(impl->setting->GetPhysicsFileName(), "") != 0)
	{
		buf = readFileBytes(impl->modelDir + impl->setting->GetPhysicsFileName(), &size);
		if (buf != nullptr) { impl->physics = CubismPhysics::Create(buf, size); freeBytes(buf); }
	}

	// (c2) pose (パーツ表示制御: これが無いと腕などの代替パーツが重複表示される)
	if (std::strcmp(impl->setting->GetPoseFileName(), "") != 0)
	{
		buf = readFileBytes(impl->modelDir + impl->setting->GetPoseFileName(), &size);
		if (buf != nullptr) { impl->pose = CubismPose::Create(buf, size); freeBytes(buf); }
	}

	// (d) 目パチ / リップシンク パラメータ群
	for (csmInt32 i = 0; i < impl->setting->GetEyeBlinkParameterCount(); ++i)
	{
		impl->eyeBlinkIds.PushBack(impl->setting->GetEyeBlinkParameterId(i));
	}
	for (csmInt32 i = 0; i < impl->setting->GetLipSyncParameterCount(); ++i)
	{
		impl->lipSyncIds.PushBack(impl->setting->GetLipSyncParameterId(i));
	}
	if (impl->setting->GetEyeBlinkParameterCount() > 0)
	{
		impl->eyeBlink = CubismEyeBlink::Create(impl->setting);
	}

	// (e) 呼吸 (公式サンプルと同一パラメータ)
	impl->breath = CubismBreath::Create();
	{
		CubismIdManager* idm = CubismFramework::GetIdManager();
		csmVector<CubismBreath::BreathParameterData> bp;
		bp.PushBack(CubismBreath::BreathParameterData(idm->GetId(ParamAngleX),     0.0f, 15.0f,  6.5345f, 0.5f));
		bp.PushBack(CubismBreath::BreathParameterData(idm->GetId(ParamAngleY),     0.0f,  8.0f,  3.5345f, 0.5f));
		bp.PushBack(CubismBreath::BreathParameterData(idm->GetId(ParamAngleZ),     0.0f, 10.0f,  5.5345f, 0.5f));
		bp.PushBack(CubismBreath::BreathParameterData(idm->GetId(ParamBodyAngleX), 0.0f,  4.0f, 15.5345f, 0.5f));
		bp.PushBack(CubismBreath::BreathParameterData(idm->GetId(ParamBreath),     0.5f,  0.5f,  3.2345f, 0.5f));
		impl->breath->SetParameters(bp);
	}

	// (f) ドラッグ追従 / モーションマネージャ + AddParameterValue 用 ID
	impl->drag          = CSM_NEW CubismTargetPoint();
	impl->motionManager = CSM_NEW CubismMotionManager();
	{
		CubismIdManager* idm = CubismFramework::GetIdManager();
		impl->idAngleX = idm->GetId(ParamAngleX); impl->idAngleY = idm->GetId(ParamAngleY); impl->idAngleZ = idm->GetId(ParamAngleZ);
		impl->idBodyX  = idm->GetId(ParamBodyAngleX);
		impl->idEyeX   = idm->GetId(ParamEyeBallX); impl->idEyeY = idm->GetId(ParamEyeBallY);
	}

	// (g) 全モーショングループをプリロード
	for (csmInt32 g = 0; g < impl->setting->GetMotionGroupCount(); ++g)
	{
		const csmChar* group = impl->setting->GetMotionGroupName(g);
		const csmInt32 count = impl->setting->GetMotionCount(group);
		for (csmInt32 i = 0; i < count; ++i)
		{
			csmByte* mbuf = readFileBytes(impl->modelDir + impl->setting->GetMotionFileName(group, i), &size);
			if (mbuf == nullptr) { continue; }
			auto* motion = CubismMotion::Create(mbuf, size);
			freeBytes(mbuf);
			if (motion == nullptr) { continue; }
			const csmFloat32 fi = impl->setting->GetMotionFadeInTimeValue(group, i);  if (fi >= 0.0f) { motion->SetFadeInTime(fi); }
			const csmFloat32 fo = impl->setting->GetMotionFadeOutTimeValue(group, i); if (fo >= 0.0f) { motion->SetFadeOutTime(fo); }
			motion->SetEffectIds(impl->eyeBlinkIds, impl->lipSyncIds);
			const csmString name = Utils::CubismString::GetFormatedString("%s_%d", group, i);
			impl->motions[name.GetRawString()] = motion;
		}
	}
	impl->motionManager->StopAllMotions();

	// (h) idle / tap グループ名を決定 (Idle/TapBody 無ければ先頭グループにフォールバック)
	auto hasGroup = [&](const char* name) { return impl->setting->GetMotionCount(name) > 0; };
	impl->idleGroup = hasGroup(kGroupIdle) ? kGroupIdle
	                : (impl->setting->GetMotionGroupCount() > 0 ? impl->setting->GetMotionGroupName(0) : "");
	impl->tapGroup  = hasGroup(kGroupTapBody) ? kGroupTapBody : impl->idleGroup;

	// テクスチャパス (model3.json の Textures[] を modelDir 相対で解決)
	for (csmInt32 i = 0; i < impl->setting->GetTextureCount(); ++i)
	{
		m_texPaths.push_back(impl->modelDir + impl->setting->GetTextureFileName(i));
	}

	std::fprintf(stderr, "[Live2D] loaded model3.json: %s (idle='%s' tap='%s', %d textures, physics=%s)\n",
	             model3jsonPath, impl->idleGroup.c_str(), impl->tapGroup.c_str(),
	             (int)m_texPaths.size(), impl->physics ? "yes" : "no");

	m_impl = impl;
	m_ready = true;
	return true;
}

void Live2DModel::update(float dragX, float dragY)
{
	if (m_impl != nullptr && m_ready) { m_impl->update(dragX, dragY); }
}

void Live2DModel::tap()
{
	if (m_impl != nullptr && m_ready) { m_impl->startRandomMotion(m_impl->tapGroup.c_str(), kPriorityNormal); }
}

void* Live2DModel::coreModel() const noexcept
{
	return (m_impl != nullptr && m_impl->model != nullptr) ? reinterpret_cast<void*>(m_impl->model->GetModel()) : nullptr;
}
}  // namespace mitiru::live2d

#endif  // MITIRU_HAS_CUBISM_FRAMEWORK
