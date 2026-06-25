#pragma once

/// @file CefErrorPage.hpp
/// @brief CEF ロード失敗時に出す自前エラーページ (data: URI を生成)。
/// @details 失敗 URL / エラー文 / コードを画面に出してデバッグを助ける。外部素材は
///          読めない (それ自体が失敗する) ので、画像なしの自己完結 HTML を base64 で
///          data URI に載せる。MitiruCefLoadHandler::OnLoadError から使う。
///          見た目はミニマル路線 (余白広め・1px ライン・抑えた配色・URL コピー付き)。

#if defined(_WIN32) && defined(MITIRU_HAS_CEF)

#include <string>

namespace mitiru::cef
{

/// 表示する動的値を HTML エスケープする。
inline std::string htmlEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 16);
    for (const char c : s)
    {
        switch (c)
        {
            case '&':  o += "&amp;";  break;
            case '<':  o += "&lt;";   break;
            case '>':  o += "&gt;";   break;
            case '"':  o += "&quot;"; break;
            case '\'': o += "&#39;";  break;
            default:   o += c;        break;
        }
    }
    return o;
}

/// 任意バイト列を base64 にする (data URI に安全に載せるため。特殊文字の心配が消える)。
inline std::string base64Encode(const std::string& in)
{
    static const char* kT =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    int val = 0, bits = -6;
    for (const unsigned char c : in)
    {
        val = (val << 8) + c;
        bits += 8;
        while (bits >= 0)
        {
            out += kT[(val >> bits) & 0x3F];
            bits -= 6;
        }
    }
    if (bits > -6) { out += kT[((val << (bits + 8)) >> 8) & 0x3F]; }
    while (out.size() % 4 != 0) { out += '='; }
    return out;
}

/// 文字列中の all 出現を置換する (テンプレ穴埋め用)。
inline void replaceAll(std::string& s, const std::string& from, const std::string& to)
{
    if (from.empty()) { return; }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

/// 自前エラーページの生 HTML を返す (resource handler が 200 text/html で直接返す用)。
inline std::string buildErrorPageHtml(const std::string& failedUrl,
                                      const std::string& errorText,
                                      int                errorCode)
{
    // 「見つからない」記号は CSS 描画 (font 非依存)。日本語は UTF-8 (ビルドは /utf-8)。
    std::string html = R"HTML(<!doctype html><html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>読み込みエラー — MitiruEngine</title>
<style>
:root{--bg:#15161a;--fg:#e8eaed;--dim:#878c97;--faint:#565b66;--accent:#d8a657;--line:#2b2e36;--mono:ui-monospace,"Cascadia Code",Consolas,"SFMono-Regular",monospace}
*{box-sizing:border-box}html,body{height:100%;margin:0}
body{background:var(--bg);color:var(--fg);font-family:"Segoe UI","Yu Gothic UI",system-ui,sans-serif;display:flex;align-items:center;justify-content:center;padding:40px;-webkit-font-smoothing:antialiased}
.wrap{width:min(520px,100%);animation:rise .5s cubic-bezier(.2,.7,.2,1) both}
@keyframes rise{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}
.kao{font-family:var(--mono);font-size:34px;color:var(--accent);letter-spacing:1px;opacity:.95;margin:0 0 22px}
h1{font-size:23px;font-weight:600;letter-spacing:.01em;margin:0 0 6px}
.brand{color:var(--faint);font-size:11px;letter-spacing:.22em;text-transform:uppercase;margin-bottom:30px}
.diag{border-top:1px solid var(--line);padding-top:20px}
.row{display:flex;align-items:baseline;gap:14px;padding:7px 0}
.row .k{color:var(--dim);font-size:12px;flex:0 0 64px;letter-spacing:.04em}
.row .v{color:var(--fg);font-family:var(--mono);font-size:13px;word-break:break-all;flex:1}
.row .v.err{color:var(--accent)}
.row .v .code{color:var(--faint)}
.copy{flex:0 0 auto;appearance:none;background:transparent;border:1px solid var(--line);color:var(--dim);font-size:11px;padding:3px 9px;border-radius:6px;cursor:pointer;font-family:var(--mono);transition:.15s}
.copy:hover{color:var(--fg);border-color:var(--faint)}
.hints{margin:26px 0 0;padding:0;list-style:none;color:var(--dim);font-size:13px;line-height:1.5}
.hints li{padding:4px 0 4px 18px;position:relative}
.hints li::before{content:"\25B8";position:absolute;left:0;color:var(--faint)}
.hints code{font-family:var(--mono);color:var(--accent);font-size:12px}
.foot{margin-top:28px;color:var(--faint);font-size:11.5px}
.foot code{font-family:var(--mono)}
</style></head>
<body><div class="wrap">
<div class="kao">(；・∀・)</div>
<h1>ページを読み込めませんでした</h1>
<div class="brand">MitiruEngine</div>
<div class="diag">
<div class="row"><span class="k">読み込み先</span><span class="v" id="url">__URL__</span><button class="copy" onclick="cp(this)">コピー</button></div>
<div class="row"><span class="k">エラー</span><span class="v err">__ERR__ <span class="code">(__CODE__)</span></span></div>
</div>
<ul class="hints">
<li><code>scene.html</code> が DLL の隣 (<code>assets/scene.html</code>) に無い / パス違い</li>
<li>サーバ経由の URL なのにサーバ未起動 / ポート不一致</li>
<li><code>--url</code> / <code>mitiru.toml [cef] start_url</code> の指定ミス</li>
</ul>
<div class="foot">HTML UI が不要なら <code>[cef] enabled=false</code> で CEF を切れます。</div>
</div>
<script>function cp(b){var t=document.getElementById('url').textContent;try{var a=document.createElement('textarea');a.value=t;document.body.appendChild(a);a.select();document.execCommand('copy');document.body.removeChild(a);}catch(e){}if(navigator.clipboard){navigator.clipboard.writeText(t).catch(function(){});}b.textContent='✓ コピーしました';setTimeout(function(){b.textContent='コピー';},1500);}</script>
</body></html>)HTML";

    replaceAll(html, "__URL__", htmlEscape(failedUrl));
    replaceAll(html, "__ERR__", htmlEscape(errorText));
    replaceAll(html, "__CODE__", std::to_string(errorCode));
    return html;
}

/// 上記 HTML を `data:text/html;...;base64,...` にして返す (LoadURL / start URL 用)。
inline std::string buildErrorDataUri(const std::string& failedUrl,
                                     const std::string& errorText,
                                     int                errorCode)
{
    return "data:text/html;charset=utf-8;base64,"
        + base64Encode(buildErrorPageHtml(failedUrl, errorText, errorCode));
}

}  // namespace mitiru::cef

#endif  // _WIN32 && MITIRU_HAS_CEF
