#pragma once

/// @file GpuParticleSystem.hpp
/// @brief GPU加速パーティクルシステム（バックエンド非依存インターフェース）
/// @details コンピュートシェーダーによるGPUシミュレーションとインスタンス描画を行う
///          パーティクルシステムの抽象インターフェース。最大100Kパーティクルをサポート。
///
/// @code
/// auto particles = mitiru::effects::createGpuParticleSystem(device);
/// particles->setEmitter(emitter);
/// // 毎フレーム:
/// particles->emit(position, velocity, lifetime, color, size);
/// particles->update(dt);
/// particles->render(camera);
/// @endcode

#include <cstdint>
#include <memory>

#include <sgc/math/Mat4.hpp>
#include <sgc/math/Vec3.hpp>
#include <sgc/types/Color.hpp>

#include <mitiru/effects/ParticleEmitter.hpp>

namespace mitiru::render
{
class Camera3D;
} // namespace mitiru::render

namespace mitiru::gfx
{
class IDevice;
} // namespace mitiru::gfx

namespace mitiru::effects
{

/// @brief GPUパーティクル属性
/// @details GPUバッファに格納される各パーティクルのデータ。
///          コンピュートシェーダーとの互換性のため16バイトアラインされた構造体。
struct GpuParticle
{
	float posX = 0;      ///< 位置X
	float posY = 0;      ///< 位置Y
	float posZ = 0;      ///< 位置Z
	float velX = 0;      ///< 速度X
	float velY = 0;      ///< 速度Y
	float velZ = 0;      ///< 速度Z
	float accelX = 0;    ///< 加速度X
	float accelY = 0;    ///< 加速度Y
	float accelZ = 0;    ///< 加速度Z
	float colorR = 1;    ///< 色R
	float colorG = 1;    ///< 色G
	float colorB = 1;    ///< 色B
	float colorA = 1;    ///< 色A
	float size = 0.1f;   ///< サイズ
	float lifetime = 2;  ///< 総寿命（秒）
	float age = 0;       ///< 経過時間（秒）
};

/// @brief パーティクルシミュレーション定数
/// @details コンピュートシェーダーに渡す定数バッファの内容。
struct ParticleSimConstants
{
	float deltaTime = 0;                    ///< フレームデルタ
	float gravityX = 0;                     ///< 重力X
	float gravityY = -9.81f;                ///< 重力Y
	float gravityZ = 0;                     ///< 重力Z
	float drag = 0.1f;                      ///< 空気抵抗
	std::uint32_t maxParticles = 0;         ///< 最大パーティクル数
	std::uint32_t activeParticles = 0;      ///< アクティブパーティクル数
	float padding = 0;                      ///< 16バイトアライメント用
};

/// @brief パーティクル描画定数
/// @details 頂点シェーダーに渡す定数バッファの内容。
struct ParticleRenderConstants
{
	sgc::Mat4f viewProjection;    ///< ビュー射影行列
	sgc::Vec3f cameraRight;      ///< カメラ右方向（ビルボード用）
	float padding0 = 0;
	sgc::Vec3f cameraUp;         ///< カメラ上方向（ビルボード用）
	float padding1 = 0;
};

/// @brief 最大パーティクル数
static constexpr std::uint32_t MAX_GPU_PARTICLES = 100'000;

/// @brief GPUパーティクルシステムインターフェース
/// @details バックエンド（DX11/DX12）に依存しない抽象インターフェース。
///          コンピュートシェーダーによるシミュレーションとインスタンス描画を提供する。
class IGpuParticleSystem
{
public:
	/// @brief デストラクタ
	virtual ~IGpuParticleSystem() = default;

	/// @brief エミッター設定を適用する
	/// @param emitter エミッター設定
	virtual void setEmitter(const ParticleEmitter& emitter) = 0;

	/// @brief エミッター設定を取得する
	/// @return 現在のエミッター設定への参照
	[[nodiscard]] virtual const ParticleEmitter& emitter() const noexcept = 0;

	/// @brief 個別パーティクルを放出する
	/// @param position 放出位置
	/// @param velocity 初期速度
	/// @param lifetime 寿命（秒）
	/// @param color 初期色
	/// @param size 初期サイズ
	virtual void emit(const sgc::Vec3f& position,
	                  const sgc::Vec3f& velocity,
	                  float lifetime,
	                  const sgc::Colorf& color,
	                  float size) = 0;

	/// @brief エミッター設定に基づいてパーティクルを自動放出する
	/// @param dt デルタタイム（秒）
	virtual void emitFromEmitter(float dt) = 0;

	/// @brief バースト放出を行う
	/// @param count 放出数
	virtual void burst(std::uint32_t count) = 0;

	/// @brief GPUシミュレーションを実行する（重力・抵抗・寿命減衰）
	/// @param dt デルタタイム（秒）
	virtual void update(float dt) = 0;

	/// @brief パーティクルをインスタンス描画する
	/// @param camera 描画に使用するカメラ
	virtual void render(const mitiru::render::Camera3D& camera) = 0;

	/// @brief アクティブパーティクル数を取得する
	/// @return 現在生存中のパーティクル数
	[[nodiscard]] virtual std::uint32_t activeCount() const noexcept = 0;

	/// @brief 最大パーティクル数を取得する
	/// @return サポートする最大パーティクル数
	[[nodiscard]] virtual std::uint32_t maxCount() const noexcept = 0;

	/// @brief 全パーティクルをクリアする
	virtual void clear() = 0;

	/// @brief システムが有効かどうかを判定する
	[[nodiscard]] virtual bool isValid() const noexcept = 0;

protected:
	IGpuParticleSystem() = default;

	/// コピー禁止
	IGpuParticleSystem(const IGpuParticleSystem&) = delete;
	IGpuParticleSystem& operator=(const IGpuParticleSystem&) = delete;

	/// ムーブ禁止
	IGpuParticleSystem(IGpuParticleSystem&&) = delete;
	IGpuParticleSystem& operator=(IGpuParticleSystem&&) = delete;
};

} // namespace mitiru::effects
