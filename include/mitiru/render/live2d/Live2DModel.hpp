#pragma once

/// @file Live2DModel.hpp
/// @brief Cubism Framework による Live2D モデルのロード + 毎フレーム更新 (motion/physics/effects)。
/// @details 公式 Cubism Native サンプル (LAppModel) と同等。model3.json を一括ロードし、
///          idle/tap モーション再生・物理・目パチ・呼吸・ドラッグ追従を駆動する。描画は行わない
///          (描画は dx12/DX12Live2D.hpp が coreModel() の csmModel を読んで自前 D3D12 で行う)。
///
///          Framework は Cubism Core を `Live2D::Cubism::Core` 名前空間にラップし、レンダラ側は
///          グローバルな <Live2DCubismCore.h> を include する。両者を同一 TU に混ぜると pragma once
///          で一方の宣言が消えるため、本クラスは pimpl とし Cubism 型をヘッダに一切露出しない。
///          実装は src/live2d/Live2DModel.cpp (非GL Framework ライブラリ)。

#ifdef MITIRU_HAS_CUBISM_FRAMEWORK

#include <string>
#include <vector>

namespace mitiru::live2d
{

/// @brief Framework 駆動の Live2D モデル (ロード + 更新のみ。描画はしない)。
class Live2DModel
{
public:
	Live2DModel() = default;
	~Live2DModel();
	Live2DModel(const Live2DModel&) = delete;
	Live2DModel& operator=(const Live2DModel&) = delete;

	/// @brief model3.json を読み、moc/テクスチャ/モーション/物理/エフェクトを構築する。失敗時 false。
	bool load(const char* model3jsonPath);

	/// @brief モデルの全リソースを解放する (切替時の再構築前に呼ぶ。安全に再 load 可能)。
	void unload();

	/// @brief 1 フレーム更新する: motion 再生 + 目パチ/呼吸 + ドラッグ追従 + physics + csmUpdateModel。
	/// @param dragX,dragY 注視先 (∈[-1,1])。頭/目/体がここへ滑らかに追従する。dt は内部で実時間計測。
	void update(float dragX, float dragY);

	/// @brief タップ操作: TapBody (無ければ idle) グループのランダムモーションを再生する。
	void tap();

	[[nodiscard]] bool ready() const noexcept { return m_ready; }

	/// @brief 描画用の Cubism Core モデル。レンダラ側で `::csmModel*` に reinterpret_cast して使う。
	/// @return 未ロードなら nullptr。
	[[nodiscard]] void* coreModel() const noexcept;

	/// @brief model3.json の Textures[] を解決したテクスチャパス数。
	[[nodiscard]] int textureCount() const noexcept { return static_cast<int>(m_texPaths.size()); }
	/// @brief i 番目のテクスチャパス (modelDir 相対で解決済み)。
	[[nodiscard]] const char* texturePath(int i) const noexcept { return m_texPaths[static_cast<size_t>(i)].c_str(); }

private:
	struct Impl;
	Impl* m_impl = nullptr;
	bool  m_ready = false;
	std::vector<std::string> m_texPaths;
};

}  // namespace mitiru::live2d

#endif  // MITIRU_HAS_CUBISM_FRAMEWORK
