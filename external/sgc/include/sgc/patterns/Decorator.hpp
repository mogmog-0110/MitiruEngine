#pragma once

/// @file Decorator.hpp
/// @brief デコレータパターン基底 — 機能を動的に追加する
///
/// 既存オブジェクトをラップして、動的に振る舞いを拡張する。
/// 武器のエンチャント・ログ出力デコレーション・バフ/デバフ等に使用する。
///
/// @code
/// struct IWeapon {
///     virtual ~IWeapon() = default;
///     virtual int damage() const = 0;
/// };
///
/// struct Sword : IWeapon {
///     int damage() const override { return 10; }
/// };
///
/// struct FireEnchant : sgc::Decorator<IWeapon>, IWeapon {
///     using sgc::Decorator<IWeapon>::Decorator;
///     int damage() const override { return wrapped().damage() + 5; }
/// };
///
/// auto sword = std::make_shared<Sword>();
/// FireEnchant enchanted(sword);
/// int dmg = enchanted.damage();  // 15
/// @endcode

#include <memory>
#include <stdexcept>

namespace sgc
{

/// @brief デコレータパターン基底クラス
/// @tparam T ラップ対象のインタフェース型
///
/// shared_ptrでラップ対象を保持し、wrapped()でアクセスを提供する。
/// デコレータのチェーン（多重装飾）にも対応する。
template <typename T>
class Decorator
{
public:
	/// @brief ラップ対象を指定して構築する
	/// @param wrapped ラップ対象のshared_ptr
	/// @throw std::invalid_argument wrappedがnullptrの場合
	explicit Decorator(std::shared_ptr<T> wrapped)
		: m_wrapped(std::move(wrapped))
	{
		if (!m_wrapped)
		{
			throw std::invalid_argument("Decorator: wrapped object is null");
		}
	}

	/// @brief デストラクタ
	virtual ~Decorator() = default;

	/// @brief コピーコンストラクタ
	Decorator(const Decorator&) = default;

	/// @brief コピー代入演算子
	Decorator& operator=(const Decorator&) = default;

	/// @brief ムーブコンストラクタ
	Decorator(Decorator&&) noexcept = default;

	/// @brief ムーブ代入演算子
	Decorator& operator=(Decorator&&) noexcept = default;

	/// @brief ラップ対象への参照を取得する
	/// @return ラップ対象の参照
	[[nodiscard]] T& wrapped() { return *m_wrapped; }

	/// @brief ラップ対象へのconst参照を取得する
	/// @return ラップ対象のconst参照
	[[nodiscard]] const T& wrapped() const { return *m_wrapped; }

	/// @brief ラップ対象のshared_ptrを取得する
	/// @return shared_ptr
	[[nodiscard]] std::shared_ptr<T> wrappedPtr() const noexcept { return m_wrapped; }

protected:
	std::shared_ptr<T> m_wrapped;  ///< ラップ対象
};

} // namespace sgc
