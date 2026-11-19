#pragma once
/// @file ChipDevice.hpp
/// @brief チップの口 層 1: レジスタの読み書きとサンプル生成だけを持つ Device
/// @details 音符やチャンネル割り当てなど音楽の知識は持たない (それは ChipDriver の仕事)。
///          VGM の再生も、この層の write() だけで書ける。チップを足すときは、このクラスを
///          継承して write() と generate() を実装する。

#include <cstdint>
#include <string_view>
#include <vector>

namespace mitiru_mml
{

/// @brief チップのエミュレーター 1 個を包む層
/// @details 時間はチップ自身のネイティブレートで進む。ネイティブレートの違いは
///          呼び出し側 (シーケンサー) が吸収するので、Device の中では変換しない。
class ChipDevice
{
public:
	virtual ~ChipDevice() = default;

	/// @brief チップの名前 (ログとテスト用)
	[[nodiscard]] virtual std::string_view name() const noexcept = 0;

	/// @brief generate() が返すフレームのレート (Hz)。クロックから決まる
	[[nodiscard]] virtual std::uint32_t nativeRate() const noexcept = 0;

	/// @brief チップを電源投入直後の状態に戻す
	virtual void reset() = 0;

	/// @brief レジスタへ書き込む
	/// @param port チップ内のポート番号 (YM2608 なら 0 と 1。ポートが 1 つのチップは 0)
	/// @param reg レジスタアドレス
	/// @param value 書き込む値
	virtual void write(std::uint32_t port, std::uint32_t reg, std::uint32_t value) = 0;

	/// @brief ネイティブレートで frames フレームを生成する
	/// @param out L,R,L,R の順の float。±1.0 が 16bit の満点。frames * 2 要素ぶんの領域が要る
	/// @details 1 フレームずつ仮想関数を呼ぶと遅いので、塊で呼ぶこと。チップの出力が
	///          1 要素 (OPL) や 3 要素 (OPNA) でも、チップごとの混ぜ方をここで済ませて
	///          ステレオ 2 本にして返す。
	virtual void generate(float* out, std::uint32_t frames) = 0;

	/// @brief 内部状態をバイト列に保存する
	/// @return 保存に対応しないチップは空のベクタ
	[[nodiscard]] virtual std::vector<std::uint8_t> saveState()
	{
		return {};
	}

	/// @brief saveState() の結果から状態を戻す
	/// @return 戻せたら true
	virtual bool loadState(const std::vector<std::uint8_t>& state)
	{
		static_cast<void>(state);
		return false;
	}
};

} // namespace mitiru_mml
