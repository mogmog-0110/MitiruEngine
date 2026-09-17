#pragma once

/// @file CefBridgeAbi.hpp
/// @brief mitiru_cef_bridge (常時 Release CRT DLL) の C-only ABI 面。
/// @details ADR 0032 の PoC (create / loadUrl / tick / shutdown) に加え、
///          Wave 2 残作業だった resize / getTexture / 入力転送 / JS signal を実装する。
///
///          この境界は ADR 0005 (Host-Game C-ABI signal flow) と同じ規律に従う:
///          POD (整数 / const char* / opaque handle / trivially-copyable struct) 以外を
///          越境させない。CefRefPtr / std::string / CEF 型を host 側のシグネチャに出さない。

#include <cstdint>
#include <type_traits>

#if defined(_WIN32)
#  if defined(MITIRU_CEF_BRIDGE_BUILD)
#    define MCEF_BRIDGE_API extern "C" __declspec(dllexport)
#  else
#    define MCEF_BRIDGE_API extern "C" __declspec(dllimport)
#  endif
#else
#  define MCEF_BRIDGE_API extern "C"
#endif

/// @brief 不透明ハンドル。中身 (MitiruCefApp/Client/Browser) は境界を越えない。
using McefBridgeHandle = void*;

/// @brief 境界 ABI バージョン。host とビルド世代が食い違う組合せの混成起動を
///        検出するための照合値 (v21 build fingerprint / ModuleApi と同型の考え方。
///        ただし ModuleApi の版とは独立)。
MCEF_BRIDGE_API int32_t mcef_bridge_abi_version();

/// @brief CEF を初期化し、ブラウザを 1 つ生成する。
/// @param exeDir   実行ファイルのあるディレクトリ (helper exe / resources の基点)
/// @param logPath  CEF ログファイルパス (空文字可)
/// @param startUrl 最初に開く URL
/// @return 成功時は非 null ハンドル、失敗時は nullptr
MCEF_BRIDGE_API McefBridgeHandle mcef_bridge_create(
    const char* exeDir,
    const char* logPath,
    const char* startUrl,
    int32_t     width,
    int32_t     height);

/// @brief URL に遷移する。handle が nullptr なら無視する。
MCEF_BRIDGE_API void mcef_bridge_load_url(McefBridgeHandle handle, const char* url);

/// @brief CEF メッセージポンプを 1 回実行する。ゲームループ先頭で毎フレーム呼ぶ。
MCEF_BRIDGE_API void mcef_bridge_tick(McefBridgeHandle handle);

/// @brief ブラウザを閉じて CefShutdown() する。handle は以後無効。
MCEF_BRIDGE_API void mcef_bridge_shutdown(McefBridgeHandle handle);

/// @brief ビューポートをリサイズする (MitiruCefBrowser::resize への薄い委譲)。
MCEF_BRIDGE_API void mcef_bridge_resize(McefBridgeHandle handle, int32_t width, int32_t height);

/// @brief 最新の OnPaint バッファ (BGRA) を取得する。
/// @details バッファの所有権は bridge 側 (DLL 内) にある。返された outPixels は
///          次にこの関数を呼ぶまでの間だけ有効なので、呼び出し側 (host) は必要なら
///          自分のバッファへコピーしてから次のフレームに進むこと (ADR 0032 c-3 と
///          同じ「callback 中コピーのみ」規約を pull 型 API に適用したもの)。
///          新しい frame が無い場合は直近の内容をそのまま返す。handle が無効、
///          または OnPaint が一度も発生していない場合、幅と高さは 0、outPixels は
///          nullptr になる。
/// @param outWidth,outHeight,outPixels  null なら該当項目は書き込まれない。
MCEF_BRIDGE_API void mcef_bridge_get_texture(
    McefBridgeHandle handle,
    int32_t*         outWidth,
    int32_t*         outHeight,
    const uint8_t**  outPixels);

/// @brief 越境用マウス/キー入力イベント種別。
enum McefInputEventKind : int32_t
{
    MCEF_INPUT_MOUSE_MOVE  = 0,
    MCEF_INPUT_MOUSE_DOWN  = 1,
    MCEF_INPUT_MOUSE_UP    = 2,
    MCEF_INPUT_MOUSE_WHEEL = 3,
    MCEF_INPUT_KEY_DOWN    = 4,
    MCEF_INPUT_KEY_UP      = 5,
    MCEF_INPUT_KEY_CHAR    = 6,
};

/// @brief マウス/キー入力 1 件を表す POD。前フレームとの差分検出は host 側の責務のまま
///        残し、ここは差分済みイベント 1 件を越境させるだけの薄い形にする。
struct McefInputEvent
{
    int32_t kind        = MCEF_INPUT_MOUSE_MOVE; ///< McefInputEventKind
    int32_t x           = 0;                     ///< CEF 論理座標 (mouse 系のみ)
    int32_t y           = 0;
    int32_t button      = 0;                     ///< 0=left 1=middle 2=right (mouse down/up)
    int32_t modifiers   = 0;                     ///< CEF cef_event_flags_t とビット互換の値
    int32_t keyCode     = 0;                     ///< windows_key_code (key 系のみ)
    int32_t wheelDelta  = 0;                      ///< mouse wheel の回転量
};
static_assert(std::is_trivially_copyable_v<McefInputEvent>, "McefInputEvent は POD (DLL 境界)");

/// @brief 差分検出済みの入力イベントをブラウザへ転送する。handle/event が null なら無視。
MCEF_BRIDGE_API void mcef_bridge_send_input(McefBridgeHandle handle, const McefInputEvent* event);

/// @brief C++ から JS へ push する単発の JS コード実行 (state push 用の薄い経路)。
MCEF_BRIDGE_API void mcef_bridge_execute_js(McefBridgeHandle handle, const char* code);

/// @brief JS 側の cefQuery 1 件に応答するコールバック。
/// @details outResponse は呼び出し側 (bridge 内部) が用意した outResponseCapacity
///          バイトの固定長バッファ。std::string や std::function を境界に出さないための形
///          (ADR 0005 と同じ規律)。書き込みが capacity を超える分は切り詰められる。
using McefQueryCallback = void (*)(
    void*       userdata,
    const char* payload,
    char*       outResponse,
    int32_t     outResponseCapacity);

/// @brief JS 側 cefQuery({request: "名前|payload"}) の「名前」に対するハンドラーを登録する。
///        handle/name/callback が null なら無視。同名を再登録すると上書きする。
MCEF_BRIDGE_API void mcef_bridge_register_handler(
    McefBridgeHandle    handle,
    const char*         name,
    McefQueryCallback   callback,
    void*               userdata);
