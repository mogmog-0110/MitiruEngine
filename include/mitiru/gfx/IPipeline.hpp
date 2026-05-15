#pragma once

/// @file IPipeline.hpp
/// @brief パイプライン状態抽象インターフェース
/// @details レンダリングパイプライン設定（シェーダー・ブレンド・ラスタライザ等）の基底。

namespace mitiru::gfx
{

/// @brief パイプライン状態の抽象インターフェース
/// @details バックエンド固有のパイプラインステートオブジェクト（PSO）の基底。
class IPipeline
{
public:
	/// @brief 仮想デストラクタ
	virtual ~IPipeline() = default;

	/// @brief パイプラインが有効かどうかを判定する
	/// @return 正常に構築されていればtrue
	[[nodiscard]] virtual bool isValid() const noexcept = 0;

	/// @brief ルートシグネチャのネイティブポインタを取得する（D3D12用）
	/// @return ID3D12RootSignatureへのvoidポインタ（未対応バックエンドではnullptr）
	/// @details D3D12パイプラインに紐づくルートシグネチャを取得する。
	///          DX11/Null等のバックエンドではnullptrを返す。
	[[nodiscard]] virtual void* rootSignature() const { return nullptr; }
};

} // namespace mitiru::gfx
