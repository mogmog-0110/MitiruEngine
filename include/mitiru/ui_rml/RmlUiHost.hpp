#pragma once
// main window の UI (RML / RCSS) をエンジンのフレームの中で動かす窓口。Engine が 1 つ持つ。
// RmlUi の型をここに出さないので、Engine.hpp を include する翻訳単位は RmlUi のヘッダを読まない。
// MITIRU_WITH_RMLUI が無いビルドでは何もしない版になる (active() が常に false)。
//
// 状態の持ち主は C++ のまま。set*() は hud.set の値を data model へ写すだけで、UI から値を書き戻す
// 経路は無い。UI の操作は takeActions() で取り出し、Engine が game の action 列へ積む。

#include <mitiru/platform/win32/Win32KeyMessage.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct ID3D12Device;
struct ID3D12CommandQueue;
struct ID3D12Resource;

namespace mitiru::ui_rml
{

struct UiAction
{
	std::string name;
	std::string payloadJson;
};

/// UI へ渡すマウス。座標は論理解像度 (game の InputSnapshot と同じ値)。
struct UiPointer
{
	float x = 0.0f;
	float y = 0.0f;
	bool buttons[3] = {};   ///< 左・右・中
	float wheel = 0.0f;     ///< 窓のホイール量 (1 目盛り 120、奥が正)
};

/// パッド (InputSnapshot の gamepad* の合成)。ボタンは module::gamepad:: のビット、スティックは上が正。
struct UiPad
{
	std::uint32_t buttonsDown = 0;
	float stickX = 0.0f;
	float stickY = 0.0f;
};

/// マウスとキー以外に UI へ渡すもの。どれも InputSnapshot から作るので、replay でも同じ操作になる。
struct UiExtraInput
{
	UiPad pad;
	std::string_view imeComposition;   ///< IME で変換中の文字 (UTF-8)。選んでいる入力欄の中に出す
	int imeCursor = 0;                 ///< キャレットの位置 (imeComposition の先頭からの byte 数)
};

/// UI の <img src="view3d:N"/> が貼る、host の 3D 副ビューの出力。resource が nullptr なら描かない。
/// format は DXGI_FORMAT の値 (この header に DXGI を読ませないため整数で持つ)。
struct UiExternalImage
{
	ID3D12Resource* resource = nullptr;
	std::uint32_t format = 0;
	int width = 0;
	int height = 0;
};

/// RML の画像の src でこの頭の付いたものは、ファイルでなく 3D の副ビュー (view3d:0 〜 view3d:7) を指す。
inline constexpr std::string_view kView3DImageScheme = "view3d:";

/// slot N の今の出力を返す。副ビューは作り直すと資源が替わるので、描くたびに引き直す。
using UiExternalImageFn = UiExternalImage (*)(void* ctx, int slot);

class RmlUiHost
{
public:
	RmlUiHost();
	~RmlUiHost();
	RmlUiHost(const RmlUiHost&) = delete;
	RmlUiHost& operator=(const RmlUiHost&) = delete;

	/// @param documentPath main window に出す RML 文書 (UTF-8。例: <game>/assets/ui/main.rml)
	/// @param logicalWidth 文脈の大きさ = ゲームの論理解像度 (マウス座標もこの座標系で渡す)
	/// @return 失敗したら false と error。UI 無しで続行してよい
	bool start(ID3D12Device* device, ID3D12CommandQueue* queue, const std::string& documentPath,
	           int logicalWidth, int logicalHeight, std::string& error);
	void stop();
	[[nodiscard]] bool active() const noexcept;
	[[nodiscard]] const std::string& documentPath() const noexcept;

	// hud.set の値。キーは "view.level" のように model 名を頭に付けた完全名
	void setInt(std::string_view key, int value);
	void setFloat(std::string_view key, float value);
	void setBool(std::string_view key, bool value);
	void setText(std::string_view key, std::string_view value);

	/// マウスとキー・文字を UI へ渡す。Engine は game に渡すのと同じ InputSnapshot からマウスを作るので、
	/// --input-script と replay でも UI が同じ操作を受け、確認ダイアログの開閉なども同じ絵になる。
	void processInput(const UiPointer& pointer, std::span<const platform::Win32KeyMessage> keys);
	void processInput(const UiPointer& pointer, std::span<const platform::Win32KeyMessage> keys, const UiExtraInput& extra);

	/// 文字を受ける欄が選ばれていれば true と、その枠 (論理解像度の x, y, w, h)。host はこの間だけ IME を
	/// 窓へ戻し、変換窓をこの枠へ置く。
	[[nodiscard]] bool focusedTextField(float rect[4]) const;

	/// @param seconds UI の時計。決定論の実行ではフレーム数から作った値を渡す (replay で遷移まで同じ絵になる)
	void update(double seconds);

	/// ゲームを描き終えた描画先 (RENDER_TARGET 状態) の上に UI を重ねる。
	void render(ID3D12Resource* target, int width, int height);

	/// 文書と RCSS を読み込み直す。data model の値は残るので、読み込んだ時点から今の値が出る。
	void reloadDocument();

	/// 利用者の設定の UI の倍率。RCSS の dp 単位がこの倍率で大きくなる (px は変わらない)。
	void setUiScale(float scale);

	[[nodiscard]] std::vector<UiAction> takeActions();

	/// <img src="view3d:N"/> の出どころ (Engine が 3D レンダラの副ビューを繋ぐ)。fn が nullptr なら貼らない。
	void setExternalImageSource(UiExternalImageFn fn, void* ctx);

private:
	struct Impl;
	std::unique_ptr<Impl> m_impl;
};

#if !defined(MITIRU_HAS_RMLUI)

inline RmlUiHost::RmlUiHost() = default;
inline RmlUiHost::~RmlUiHost() = default;
inline bool RmlUiHost::start(ID3D12Device*, ID3D12CommandQueue*, const std::string&, int, int, std::string& error)
{
	error = "RmlUi is not built (MITIRU_WITH_RMLUI=OFF)";
	return false;
}
inline void RmlUiHost::stop() {}
inline bool RmlUiHost::active() const noexcept { return false; }
inline const std::string& RmlUiHost::documentPath() const noexcept
{
	static const std::string empty;
	return empty;
}
inline void RmlUiHost::setInt(std::string_view, int) {}
inline void RmlUiHost::setFloat(std::string_view, float) {}
inline void RmlUiHost::setBool(std::string_view, bool) {}
inline void RmlUiHost::setText(std::string_view, std::string_view) {}
inline void RmlUiHost::processInput(const UiPointer&, std::span<const platform::Win32KeyMessage>) {}
inline void RmlUiHost::processInput(const UiPointer&, std::span<const platform::Win32KeyMessage>, const UiExtraInput&) {}
inline bool RmlUiHost::focusedTextField(float[4]) const { return false; }
inline void RmlUiHost::update(double) {}
inline void RmlUiHost::render(ID3D12Resource*, int, int) {}
inline void RmlUiHost::reloadDocument() {}
inline void RmlUiHost::setUiScale(float) {}
inline std::vector<UiAction> RmlUiHost::takeActions() { return {}; }
inline void RmlUiHost::setExternalImageSource(UiExternalImageFn, void*) {}

#endif

} // namespace mitiru::ui_rml
