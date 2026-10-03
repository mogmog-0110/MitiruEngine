// Renderer3D_DX12 のクラス本体の断片 (Renderer3D_DX12.hpp から DX12ViewsSetup.hpp の後に include)。ゲーム DLL の副ビュー (ABI v49)
//
// DLL は 0..kMaxView3DSlots-1 の slot を選んで毎フレーム作りを渡す。ここが slot ごとに副ビューの番号と作りを覚え、
// 作りが変わった時だけ作り直す。番号は host の中だけで使い、DLL へは返さない (手放した番号を DLL が持ち続けない)。

public:

bool setViewSlot(int slot, const View3DPod& pod) override
{
	if (!validViewSlot(slot)) { return false; }
	ViewSlot& s = m_viewSlots[static_cast<std::size_t>(slot)];
	if (s.id != 0 && viewAt(s.id) != nullptr && std::memcmp(&s.pod, &pod, sizeof(View3DPod)) == 0) { return true; }
	if (m_activeView != nullptr) { return false; }
	if (s.id != 0) { releaseView(s.id); }
	s.id = createView(viewDescOf(pod));
	s.pod = pod;
	return s.id != 0;
}

/// @brief ゲーム DLL の作り (View3DPod) を副ビューの設定に写す。bit2..5 は主ビューの後処理をこのビューだけ止める (v50)
[[nodiscard]] static View3DDesc viewDescOf(const View3DPod& pod) noexcept
{
	View3DDesc desc;
	desc.width = pod.width;
	desc.height = pod.height;
	desc.shadows = (pod.flags & kView3DShadows) != 0;
	desc.skybox = (pod.flags & kView3DSky) != 0;
	desc.temporal = (pod.flags & kView3DNoTemporal) == 0;
	desc.ambientOcclusion = (pod.flags & kView3DNoAmbientOcclusion) == 0;
	desc.bloom = (pod.flags & kView3DNoBloom) == 0;
	desc.antiAlias = (pod.flags & kView3DNoAntiAlias) == 0;
	desc.shadowMapSize = pod.shadowMapSize;
	desc.clearColor = sgc::Colorf{pod.clear[0], pod.clear[1], pod.clear[2], pod.clear[3]};
	return desc;
}

/// @brief camera の縦横比は使わず、副ビューの大きさから決める (DLL は副ビューの大きさを持たない)
bool beginViewSlot(int slot, const Camera3D& camera) override
{
	const int id = viewSlotId(slot);
	const View3D* v = viewAt(id);
	if (v == nullptr) { return false; }
	const float aspect = static_cast<float>(v->desc.width) / static_cast<float>(v->desc.height);
	const Camera3D cam(camera.position(), camera.target(), camera.up(), camera.fov(), aspect, camera.nearClip(),
	                   camera.farClip());
	return beginView(id, cam);
}

void endViewSlot() override { endView(); }

void compositeViewSlot(int slot, float u, float v, float w, float h) override
{
	const int id = viewSlotId(slot);
	if (id == 0 || !m_frameActive || !(w > 0.0f) || !(h > 0.0f)) { return; }
	m_viewComposites.push_back({id, u, v, w, h, true});
}

void drawMeshWithViewSlot(const Mesh& mesh, const sgc::Mat4f& world, const Material& material, int slot) override
{
	drawMeshWithView(mesh, world, material, viewSlotId(slot));
}

void releaseViewSlot(int slot) override
{
	if (!validViewSlot(slot)) { return; }
	ViewSlot& s = m_viewSlots[static_cast<std::size_t>(slot)];
	if (s.id == 0 || viewAt(s.id) == m_activeView) { return; }
	releaseView(s.id);
	s = ViewSlot{};
}

/// @brief slot の出力の資源 (RGBA8 の TYPELESS、sRGB の値)。UI が `view3d:N` の画像として貼る。無ければ resource が nullptr
struct ViewSlotImage
{
	ID3D12Resource* resource = nullptr;
	int width = 0;
	int height = 0;
};

[[nodiscard]] ViewSlotImage viewSlotImage(int slot) const noexcept
{
	const View3D* v = viewAt(viewSlotId(slot));
	if (v == nullptr || !v->ldr) { return {}; }
	return {v->ldr.Get(), v->desc.width, v->desc.height};
}

private:

struct ViewSlot
{
	int id = 0;
	View3DPod pod{};
};

std::array<ViewSlot, kMaxView3DSlots> m_viewSlots{};

[[nodiscard]] static bool validViewSlot(int slot) noexcept { return slot >= 0 && slot < kMaxView3DSlots; }

[[nodiscard]] int viewSlotId(int slot) const noexcept
{
	return validViewSlot(slot) ? m_viewSlots[static_cast<std::size_t>(slot)].id : 0;
}
