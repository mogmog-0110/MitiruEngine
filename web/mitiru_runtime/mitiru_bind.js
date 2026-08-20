/*!
 * mitiru_bind.js — 宣言的データバインダ (ゼロ JS で HTML/CSS ゲーム UI を組む)
 *
 * 目的: ゲーム開発者が JavaScript を一行も書かずに、C++ が push する状態を
 * HTML/CSS だけで画面に反映できるようにする。状態は C++ が所有し
 * (ADR 0005, signal-only bridge)、この runtime は「受け取った値を DOM へ
 * 写すだけ」の純粋な描画層。ゲームロジック・状態は一切持たない。
 *
 * 前提: mitiru_cef_state.js を先に読み込むこと (window.mitiru を提供)。
 *
 * ── 状態スキーマ (C++ → HTML) ───────────────────────────────────────────
 *   スカラ / 小オブジェクト : JSON 文字列   例 view.boss = '{"active":true,"pct":62}'
 *   ホットなリスト          : コンパクト区切り (FrameIntents の byte 制限対策)
 *                             例 view.scene = "2,80,120,1;2,300,60,0"
 *                             → 要素ごとに data-m-fields の列名へ割り当て
 *
 * ── バインド語彙 (HTML の data-m-* 属性) ────────────────────────────────
 *   data-m-text="path"            textContent にパスの値
 *                                 (+ data-m-format=int|kmb|pct|time|comma|f2|secs|x100|pct01|ago)
 *   data-m-tpl="… {path} …"       テンプレ文字列 ({path} / {path:fmt} を埋める)
 *   data-m-show="cond[; cond]"    真のとき表示 ("path == v" / "!=" / "!path" / 数値比較 "< > <= >=" 可)
 *                                 ";" 区切りは AND、条件内の "|" は OR
 *   data-m-hide="cond[; cond]"    show の反転
 *   data-m-enabled="cond[; cond]" 全条件 真 で活性 (それ以外 disabled)。ボタン/フォーム用
 *   data-m-disabled="cond[; cond]" 全条件 真 で不活性
 *   data-m-value="path"           <input>/<select> の値へ状態を書き戻す (C++→JS)。focus 中は skip
 *   data-m-class="cls: path; …"   真のときクラス付与
 *   data-m-style="prop: {path}u"  スタイル束縛 (例 "width: {view.boss.pct}%")
 *   data-m-attr="src: {path}; …"  任意属性に値をバインド (画像 src / title / aria 等)
 *   data-m-action="name"          クリック/入力で dispatch(name, arg) (HTML → C++ 入力)
 *     data-m-arg="path"             dispatch に載せる値 (repeat 内なら item の値。例: 押した項目の id)
 *       data-m-arg="'hard'" / "42"    引用符は文字列リテラル、数値はそのまま値 (難易度ボタン等)
 *     フォーム要素 (input/select) は現在値を自動で arg に載せる (スライダー/選択 等の設定 UI)
 *     data-m-payload='{"k":{path}}' payload を JSON テンプレで組む (arg より優先)。{path} は
 *                                 state / item の値が JSON 型のまま埋まる (文字列は自動 quote)。
 *                                 {$value} は prompt 結果かフォーム現在値。注: コンパクト区切り
 *                                 リストの field は文字列になる (JSON リストは型を保つ)
 *     data-m-confirm="text"         dispatch 前に確認 (mitiru.modal.confirm → window.confirm 代替)
 *       data-m-confirm-title="t"      確認ダイアログの見出し
 *     data-m-prompt="title"         dispatch 前に文字列入力 (mitiru.modal.prompt)。結果は {$value}。
 *       data-m-prompt-message / data-m-prompt-default
 *       data-m-prompt-pattern="re"    不一致は再入力、cancel で dispatch 中止
 *   data-m-toast="path"           {kind,message} の変化でトースト表示 — textContent=message、
 *                                 kind-<kind> + is-visible を付与し data-m-toast-ms (既定 4000)
 *                                 後に is-visible を外す (CSS transition 用)
 *   data-m-input="path"           <input> のテキスト入力を C++ へ届ける (名前欄 等)
 *                                 確定時 (Enter / blur) に action "input:<path>" +
 *                                 payload {"value":"<入力値>"} を dispatch。IME 変換中は送らない
 *     data-m-input-live (属性のみ)  入力の度にも送る (150ms デバウンス)
 *   data-m-flash="field"          値が変わった瞬間に m-flash クラスを一瞬付与 (CSS 発火用)
 *   data-m-tween="path"           数値が変わったとき ~300ms でカウントアップ/ダウン表示
 *                                 (data-m-format=int|kmb に対応。data-m-text と同一要素では tween が優先)
 *   data-m-repeat="listPath"      リストを子 <template> で要素群に展開 (プール使い回し)
 *     data-m-fields="a,b,c"         コンパクト列の列名 (JSON リストなら不要)
 *     data-m-type="t"               case 選択に使う列名 (既定 "t")
 *     data-m-key="field"            指定時は item[key] で要素を保持 → CSS transition でスライド
 *     data-m-sep=";" / data-m-fsep=","  区切り (既定 ; と ,)
 *     <template [data-m-case="v"]>   item[type]==v のとき使うテンプレ
 *     item 内: data-m-text/tpl/class/style/flash は item フィールド基準で解決
 *              data-m-pos="xF,yF"     transform: translate(x,y)
 *              data-m-rot="deg"       回転を付与 (リテラル度数)
 *     生成された要素には一瞬 m-enter クラスが付く (CSS 生成アニメ用、任意)
 *     data-m-leave (属性のみ)        keyed repeat で key が消えたとき即削除せず m-leave
 *                                 クラスを付与し animationend (または 400ms fallback) 後に削除
 *
 *   開発時: <body data-m-debug> か URL に ?mdebug=1 で未知パスを警告。
 */
(function (global) {
  'use strict';
  var mitiru = global.mitiru;
  if (!mitiru || typeof mitiru.onStateChange !== 'function') {
    console.error('[mitiru-bind] window.mitiru not found — load mitiru_cef_state.js first.');
    return;
  }

  var DEBUG = false;
  function warn() { if (DEBUG) { console.warn.apply(console, ['[mitiru-bind]'].concat([].slice.call(arguments))); } }

  // ── 値ストア: 生文字列を保持し、JSON は変化時だけパースしてメモ化 ──
  var rawCache = Object.create(null);     // key -> 直近の生文字列
  var parsedCache = Object.create(null);  // key -> パース結果 (object/array/string/number)

  function parseValue(key) {
    var raw = mitiru.getState(key);
    if (raw === rawCache[key] && key in parsedCache) { return parsedCache[key]; }
    rawCache[key] = raw;
    var v = raw;
    if (typeof raw === 'string') {
      var s = raw.trim();
      if (s.length && (s[0] === '{' || s[0] === '[')) {
        try { v = JSON.parse(s); } catch (e) { v = raw; }
      }
    }
    parsedCache[key] = v;
    return v;
  }

  // path = "view.boss.pct" → state key "view.boss" の JSON を pct まで辿る。
  // 状態キー自体がドットを含む (例 "view.persec") ので、キーとサブパスの境界は
  // 「実在する最長プレフィックス」で判定する: 長い方から getState を試し、最初に
  // 値が見つかったところがキー。残りは JSON を辿る。
  // item スコープがある場合はまず item の中を辿る (item は素の object)。
  // mdebug 用: 未知 path は即警告しない (起動直後は「まだ push されてないだけ」が普通)。
  // 3 秒後に再判定し、その時点でも prefix が見つからない path だけ 1 回警告する。
  var pendingUnknown = Object.create(null);
  var unknownTimerArmed = false;
  function hasAnyPrefix(path) {
    var segs = String(path).split('.');
    for (var n = segs.length; n >= 1; n--) {
      if (mitiru.getState(segs.slice(0, n).join('.')) !== undefined) { return true; }
    }
    return false;
  }
  function warnUnknownPath(path) {
    if (!DEBUG || pendingUnknown[path]) { return; }
    pendingUnknown[path] = true;
    if (unknownTimerArmed) { return; }
    unknownTimerArmed = true;
    setTimeout(function () {
      Object.keys(pendingUnknown).forEach(function (p) {
        if (!hasAnyPrefix(p)) {
          warn('未知の state パス "' + p + '" — C++ 側で push されていません (typo か push 漏れ)');
        }
      });
    }, 3000);
  }

  function resolve(path, item) {
    var segs = String(path).split('.');
    if (item != null) {
      var iv = item;
      for (var i = 0; i < segs.length; i++) {
        if (iv == null) { return undefined; }
        iv = iv[segs[i]];
      }
      return iv;
    }
    for (var n = segs.length; n >= 1; n--) {
      var key = segs.slice(0, n).join('.');
      if (mitiru.getState(key) !== undefined) {
        var v = parseValue(key);
        for (var j = n; j < segs.length; j++) {
          if (v == null) { return undefined; }
          v = v[segs[j]];
        }
        return v;
      }
    }
    warnUnknownPath(String(path));
    return undefined;
  }

  function truthy(v) {
    return !(v === undefined || v === null || v === false || v === 0 ||
             v === '' || v === '0' || v === 'false');
  }

  // ── フォーマッタ ──
  function fmt(v, spec) {
    if (spec === 'kmb') {
      var n = Number(v) || 0, u = ['', 'K', 'M', 'B', 'T'], i = 0;
      while (n >= 1000 && i < u.length - 1) { n /= 1000; i++; }
      return (i === 0 ? Math.floor(n) : n.toFixed(1)) + u[i];
    }
    if (spec === 'int') { return String(Math.floor(Number(v) || 0)); }
    if (spec === 'pct') { return (Number(v) || 0) + '%'; }
    if (spec === 'time') { var ts = Math.max(0, Math.floor(Number(v) || 0)); return Math.floor(ts / 60) + ':' + ('0' + (ts % 60)).slice(-2); }
    if (spec === 'comma') { return (Math.floor(Number(v) || 0)).toLocaleString('en-US'); }
    if (spec === 'f2')    { return (Number(v) || 0).toFixed(2); }
    if (spec === 'secs')  { return ((Number(v) || 0) / 1000).toFixed(1); }             // ms → 秒 1 桁
    if (spec === 'x100')  { return String(Math.round(clamp01(Number(v) || 0) * 100)); } // 0..1 → 0..100
    if (spec === 'pct01') { return (Math.round(clamp01(Number(v) || 0) * 10000) / 100) + '%'; }
    if (spec === 'ago') {                                                               // unix 秒 → 相対時刻
      var at = Math.floor(Number(v) || 0);
      if (!at) { return ''; }
      var d = Math.max(0, Math.floor(Date.now() / 1000) - at);
      if (d < 60)    { return d + 's ago'; }
      if (d < 3600)  { return Math.floor(d / 60) + 'm ago'; }
      if (d < 86400) { return Math.floor(d / 3600) + 'h ago'; }
      return Math.floor(d / 86400) + 'd ago';
    }
    return v == null ? '' : String(v);
  }
  function clamp01(n) { return n < 0 ? 0 : (n > 1 ? 1 : n); }

  // "… {path} … {path:fmt} …" を埋める。
  function renderTpl(tpl, item) {
    return String(tpl).replace(/\{([^}]+)\}/g, function (_, expr) {
      var parts = expr.split(':');
      return fmt(resolve(parts[0].trim(), item), parts[1] && parts[1].trim());
    });
  }

  // data-m-tween="path": 数値変化を ~300ms でカウントアップ/ダウン表示。
  // data-m-format に従いフォーマット。非数値は即セット。
  var TWEEN_MS = 300;
  function applyTween(el, path, item) {
    var raw = resolve(path, item);
    var target = Number(raw);
    if (isNaN(target)) { el.textContent = raw == null ? '' : String(raw); el._mtween_last = target; return; }
    var from = (el._mtween_last !== undefined && !isNaN(el._mtween_last)) ? el._mtween_last : target;
    el._mtween_last = target;
    if (from === target) { return; }
    var spec = el.dataset.mFormat;
    var start = null;
    if (el._mtween_raf) { cancelAnimationFrame(el._mtween_raf); }
    function step(ts) {
      if (!start) { start = ts; }
      var t = Math.min(1, (ts - start) / TWEEN_MS);
      el.textContent = fmt(from + (target - from) * t, spec);
      if (t < 1) { el._mtween_raf = requestAnimationFrame(step); }
      else { el._mtween_raf = null; el._mtween_last = target; }
    }
    el._mtween_raf = requestAnimationFrame(step);
  }

  // ── 1 要素へ data-m-* を適用 (item != null なら item スコープ) ──
  function applyBindings(el, item) {
    var d = el.dataset;
    if (d.mTween != null) { applyTween(el, d.mTween, item); }
    else if (d.mText != null) { el.textContent = fmt(resolve(d.mText, item), d.mFormat); }
    if (d.mTpl != null)   { el.textContent = renderTpl(d.mTpl, item); }
    if (d.mShow != null)  { el.style.display = evalCondList(d.mShow, item) ? '' : 'none'; }
    if (d.mHide != null)  { el.style.display = evalCondList(d.mHide, item) ? 'none' : ''; }
    if (d.mEnabled != null)  { el.disabled = !evalCondList(d.mEnabled, item); }
    if (d.mDisabled != null) { el.disabled = evalCondList(d.mDisabled, item); }
    if (d.mClass != null) { applyClass(el, d.mClass, item); }
    if (d.mStyle != null) { applyStyle(el, d.mStyle, item); }
    if (d.mPos != null)   { applyPos(el, d.mPos, d.mRot, d.mAnchor, item); }
    if (d.mFlash != null) { applyFlash(el, d.mFlash, item); }
    if (d.mAttr != null)  { applyAttr(el, d.mAttr, item); }
    if (d.mValue != null) { applyValue(el, d.mValue, item); }
    if (d.mToast != null) { applyToast(el, d.mToast, item); }
    if (d.mArg != null)   { el._marg = argValue(d.mArg, item); }  // dispatch に載せる値 (item スコープ対応)
    if (d.mDrag != null)  { el._mdragValue = resolve(d.mDrag, item); }  // 掴んだときに運ぶ値 (mousedown 時に読み直さない: スロットは使い回される)
    if (d.mPayload != null) { el._mitem = item; }                 // payload はクリック時に item で組む
  }

  // data-m-value="path": フォーム値への書き戻し (C++→JS)。入力中の上書きはしない。
  function applyValue(el, path, item) {
    if (document.activeElement === el) { return; }
    var v = resolve(path, item);
    if (el.type === 'checkbox') { el.checked = truthy(v); return; }
    var s = v == null ? '' : String(v);
    if (el.value !== s) { el.value = s; }
  }

  // data-m-toast="path": {kind,message} の変化で表示 → data-m-toast-ms 後に閉じる。
  function applyToast(el, path, item) {
    var v = resolve(path, item);
    var raw;
    try { raw = JSON.stringify(v); } catch (e) { raw = String(v); }
    if (el._mtoast === raw) { return; }
    el._mtoast = raw;
    var msg = (v && v.message) ? String(v.message) : '';
    if (!msg) { return; }
    var kind = (v && v.kind) ? String(v.kind) : 'info';
    if (el._mtoastKind) { el.classList.remove(el._mtoastKind); }
    el._mtoastKind = 'kind-' + kind;
    el.textContent = msg;
    el.classList.add(el._mtoastKind, 'is-visible');
    var ms = Number(el.dataset.mToastMs) || 4000;
    if (el._mtoastTimer) { clearTimeout(el._mtoastTimer); }
    el._mtoastTimer = setTimeout(function () {
      el._mtoastTimer = null;
      el.classList.remove('is-visible');
    }, ms);
  }

  // data-m-attr="src: {path}; title: {path}" — 任意属性に値をバインド (画像 src 等)。
  function applyAttr(el, spec, item) {
    spec.split(';').forEach(function (pair) {
      if (!pair.trim()) { return; }
      var idx = pair.indexOf(':');
      if (idx < 0) { return; }
      el.setAttribute(pair.slice(0, idx).trim(), renderTpl(pair.slice(idx + 1).trim(), item));
    });
  }

  // ── HTML → C++ アクション配線 (クリック / フォーム入力。値も dispatch に載せる) ──
  // フォーム要素なら現在値を返す (数値化できれば数値)。
  function formValue(el) {
    var t = (el.tagName || '').toLowerCase();
    if (t === 'input' && el.type === 'checkbox') { return el.checked; }
    if (t === 'input' || t === 'select' || t === 'textarea') {
      var n = Number(el.value);
      return (el.value !== '' && !isNaN(n)) ? n : el.value;
    }
    return undefined;
  }
  // data-m-arg の値解釈: 引用符 'x' / "x" は文字列リテラル、数値リテラルは Number、
  // それ以外は state path (repeat 内なら item フィールド) として解決する。
  function argValue(spec, item) {
    var m = /^\s*(['"])([\s\S]*)\1\s*$/.exec(spec);
    if (m) { return m[2]; }
    if (/^\s*-?\d+(\.\d+)?\s*$/.test(spec)) { return Number(spec); }
    return resolve(spec, item);
  }
  // dispatch に載せる引数: data-m-arg があればその値 (item スコープは _marg)、無ければフォーム値。
  function actionArg(el) {
    if (el.dataset.mArg != null) {
      return (el._marg !== undefined) ? el._marg : argValue(el.dataset.mArg, null);
    }
    return formValue(el);
  }

  // data-m-payload='{"k":{path}}' を JSON テンプレとして埋める。{path} は state / item の値が
  // JSON 型のまま入る (文字列なら quote 付き)。{$value} は prompt 結果 / フォーム現在値。
  function buildPayload(el, value) {
    var s = String(el.dataset.mPayload);
    s = s.replace(/\{\$value\}/g, function () {
      return JSON.stringify(value === undefined ? null : value);
    });
    s = s.replace(/\{([A-Za-z_][\w.]*)(:[a-zA-Z0-9]+)?\}/g, function (_, p, f) {
      var v = resolve(p, el._mitem != null ? el._mitem : null);
      if (f) { return JSON.stringify(fmt(v, f.slice(1))); }
      return JSON.stringify(v === undefined ? null : v);
    });
    try { return JSON.parse(s); } catch (e) { return s; }
  }

  // 確認 / 入力ガード。mitiru.modal (mitiru_modal.js) を優先し、無ければ native dialog、
  // それも無ければ素通し (headless テスト等)。
  function askConfirm(el) {
    var text = el.dataset.mConfirm;
    if (mitiru.modal && typeof mitiru.modal.confirm === 'function') {
      return mitiru.modal.confirm({ title: el.dataset.mConfirmTitle || '', body: text });
    }
    if (typeof global.confirm === 'function') { return Promise.resolve(global.confirm(text)); }
    return Promise.resolve(true);
  }
  function promptOnce(el, current) {
    if (mitiru.modal && typeof mitiru.modal.prompt === 'function') {
      return mitiru.modal.prompt({
        title: el.dataset.mPrompt,
        body: el.dataset.mPromptMessage || '',
        defaultValue: current,
      });
    }
    if (typeof global.prompt === 'function') {
      return Promise.resolve(global.prompt(el.dataset.mPrompt, current));
    }
    return Promise.resolve(null);
  }
  function askPrompt(el) {
    var pat = null;
    try { pat = el.dataset.mPromptPattern ? new RegExp(el.dataset.mPromptPattern) : null; }
    catch (e) { pat = null; }
    function ask(current) {
      return promptOnce(el, current).then(function (v) {
        if (v == null) { return undefined; }        // cancel → dispatch しない
        if (pat && !pat.test(v)) { return ask(v); } // 不一致 → 入力値を保って再入力
        return v;
      });
    }
    return ask(el.dataset.mPromptDefault || '');
  }
  function fireAction(el, value) {
    if (typeof mitiru.dispatch !== 'function') { return; }
    var arg;
    if (el.dataset.mPayload != null) {
      arg = buildPayload(el, value !== undefined ? value : formValue(el));
    } else if (value !== undefined) {
      arg = value;
    } else {
      arg = actionArg(el);
    }
    mitiru.dispatch(el.dataset.mAction, arg);
  }

  function wireActions(root) {
    var list = [];
    if (root.dataset && root.dataset.mAction != null) { list.push(root); }
    var found = root.querySelectorAll ? root.querySelectorAll('[data-m-action]') : [];
    for (var i = 0; i < found.length; i++) { list.push(found[i]); }
    list.forEach(function (el) {
      if (el._mwired) { return; }
      el._mwired = true;
      var t = (el.tagName || '').toLowerCase(), ev = 'click';
      if (t === 'input' && (el.type === 'range' || el.type === 'text' || el.type === 'number')) { ev = 'input'; }
      else if (t === 'select' || (t === 'input' && (el.type === 'checkbox' || el.type === 'radio'))) { ev = 'change'; }
      el.addEventListener(ev, function (e) {
        if (el.disabled) { return; }
        // 入れ子の action 要素は内側だけが撃つ。行 (select.pick) の中に折りたたみの
        // ハンドル (tree.toggle) を置いた Makina のアウトライナで、ハンドルを押すと選択も
        // 動いた -- click は親へ泡立つ。二重 dispatch を期待するページは無い。
        if (e && e.stopPropagation) { e.stopPropagation(); }
        if (el.dataset.mConfirm != null) {
          askConfirm(el).then(function (ok) { if (ok) { fireAction(el, undefined); } });
          return;
        }
        if (el.dataset.mPrompt != null) {
          askPrompt(el).then(function (v) { if (v !== undefined) { fireAction(el, v); } });
          return;
        }
        fireAction(el, undefined);
      });
    });
  }

  // ── HTML → C++ テキスト入力 (data-m-input) ──
  // 確定時 (Enter / blur) に action "input:<path>" + payload {"value":"<入力値>"} を送る。
  // data-m-input-live 付きなら input の度にも送る (150ms デバウンス)。
  // IME 変換中 (compositionstart〜compositionend) は送らず、確定後の値だけ送る。
  var INPUT_DEBOUNCE_MS = 150;
  function sendInput(el) {
    if (el._mintimer) { clearTimeout(el._mintimer); el._mintimer = null; }  // 確定送信は live の予約を破棄
    if (typeof mitiru.dispatch === 'function') {
      mitiru.dispatch('input:' + el.dataset.mInput, { value: el.value });
    }
  }
  function scheduleLiveInput(el) {
    if (el._mintimer) { clearTimeout(el._mintimer); }
    el._mintimer = setTimeout(function () { el._mintimer = null; sendInput(el); }, INPUT_DEBOUNCE_MS);
  }
  function wireInputs(root) {
    var list = [];
    if (root.dataset && root.dataset.mInput != null) { list.push(root); }
    var found = root.querySelectorAll ? root.querySelectorAll('[data-m-input]') : [];
    for (var i = 0; i < found.length; i++) { list.push(found[i]); }
    var wired = 0;
    list.forEach(function (el) {
      if (el._minwired) { return; }
      el._minwired = true;
      wired++;
      var live = el.dataset.mInputLive != null;
      el.addEventListener('compositionstart', function () { el._mcomposing = true; });
      el.addEventListener('compositionend', function () {
        el._mcomposing = false;
        if (live) { scheduleLiveInput(el); }   // 変換確定後の値を live で 1 回送る
      });
      el.addEventListener('keydown', function (e) {
        if (e.key !== 'Enter' || e.isComposing || el._mcomposing) { return; }
        sendInput(el);
      });
      el.addEventListener('blur', function () {
        if (el._mcomposing) { return; }
        sendInput(el);
      });
      if (live) {
        el.addEventListener('input', function () {
          if (el._mcomposing) { return; }
          scheduleLiveInput(el);
        });
      }
    });
    return wired;
  }

  // data-m-flash="field": 値が前回から変わったら m-flash クラスを一瞬付ける
  // (CSS keyframe を発火 → マージのポップ等)。初回は記録のみで発火しない。
  function applyFlash(el, path, item) {
    var cur = resolve(path, item);
    if (el._mflash !== undefined && el._mflash !== cur) {
      el.classList.remove('m-flash');
      void el.offsetWidth;                 // reflow して連続発火でも再生し直す
      el.classList.add('m-flash');
      el.addEventListener('animationend', function () { el.classList.remove('m-flash'); }, { once: true });
    }
    el._mflash = cur;
  }

  // 複合条件: ";" 区切りは AND、条件内の "|" は OR。単一条件は evalCond へ委譲。
  function evalCondList(spec, item) {
    return String(spec).split(';').every(function (andTerm) {
      if (!andTerm.trim()) { return true; }
      return andTerm.split('|').some(function (orTerm) {
        return evalCond(orTerm, item);
      });
    });
  }

  function evalCond(expr, item) {
    expr = expr.trim();
    if (expr[0] === '!') { return !truthy(resolve(expr.slice(1).trim(), item)); }
    var m = expr.match(/^(.+?)\s*(==|!=|<=|>=|<|>)\s*(.+)$/);
    if (m) {
      var a = resolve(m[1].trim(), item), b = m[3].trim(), op = m[2];
      if (op === '==') { return String(a) === b; }
      if (op === '!=') { return String(a) !== b; }
      var na = Number(a), nb = Number(b);   // 不等号は数値比較 (HP 警告など)
      if (op === '<')  { return na < nb; }
      if (op === '>')  { return na > nb; }
      if (op === '<=') { return na <= nb; }
      return na >= nb;
    }
    return truthy(resolve(expr, item));
  }

  function applyClass(el, spec, item) {
    spec.split(';').forEach(function (pair) {
      if (!pair.trim()) { return; }
      var idx = pair.indexOf(':');
      if (idx < 0) { return; }
      var cls = pair.slice(0, idx).trim();
      el.classList.toggle(cls, evalCond(pair.slice(idx + 1), item));
    });
  }

  function applyStyle(el, spec, item) {
    spec.split(';').forEach(function (pair) {
      if (!pair.trim()) { return; }
      var idx = pair.indexOf(':');
      if (idx < 0) { return; }
      var prop = pair.slice(0, idx).trim();
      el.style[prop] = renderTpl(pair.slice(idx + 1).trim(), item);
    });
  }

  // data-m-pos="xField,yField" + 任意の data-m-anchor="hw,hh" で中心座標→左上に補正。
  function applyPos(el, spec, rot, anchor, item) {
    var f = spec.split(',');
    var x = Number(resolve(f[0].trim(), item)) || 0;
    var y = Number(resolve((f[1] || '').trim(), item)) || 0;
    if (anchor) { var a = anchor.split(','); x -= (Number(a[0]) || 0); y -= (Number(a[1]) || 0); }
    el.style.transform = 'translate(' + x + 'px,' + y + 'px)' + (rot ? ' rotate(' + rot + 'deg)' : '');
  }

  // ── repeat: リストを子テンプレで要素群に展開 (プール使い回しで 60fps 耐性) ──
  function Repeat(container) {
    this.el = container;
    var d = container.dataset;
    this.listPath = d.mRepeat;
    this.typeField = d.mType || 't';
    this.fields = d.mFields ? d.mFields.split(',').map(function (s) { return s.trim(); }) : null;
    this.sep = d.mSep || ';';
    this.fsep = d.mFsep || ',';
    this.keyField = d.mKey || null;  // 指定時は item[key] で要素を保持 (slide transition 用)
    this.templates = {};       // case -> <template>、'' -> 既定
    this.pool = [];            // index プール (keyField 無し時)
    this.byKey = Object.create(null);  // key プール (keyField 有り時)
    // 直下の <template> を集める (:scope 非対応環境でも動く手動走査)。
    for (var c = container.firstElementChild; c; c = c.nextElementSibling) {
      if ((c.tagName || '').toLowerCase() === 'template') {
        this.templates[c.dataset.mCase || ''] = c;
      }
    }
    this.list = document.createElement('div');   // 実体の入れ物 (template の後ろ)
    this.list.style.display = 'contents';
    container.appendChild(this.list);
  }

  // 生文字列 / JSON をアイテム配列に正規化。
  Repeat.prototype.items = function () {
    if (this.fields) {                       // コンパクト区切り
      var raw = mitiru.getState(this.listPath);
      if (raw == null || raw === '') { return []; }
      var self = this;
      return String(raw).split(this.sep).filter(function (t) { return t.length > 0; }).map(function (tok) {
        var cols = tok.split(self.fsep), o = {};
        for (var i = 0; i < self.fields.length; i++) { o[self.fields[i]] = cols[i]; }
        return o;
      });
    }
    var v = parseValue(this.listPath);        // JSON 配列
    return Array.isArray(v) ? v : [];
  };

  Repeat.prototype._caseOf = function (item) {
    return String(item[this.typeField] != null ? item[this.typeField] : '');
  };
  Repeat.prototype._makeSlot = function (tpl, cas) {
    var node = tpl.content.firstElementChild.cloneNode(true);
    node.classList.add('m-enter');   // 生成時アニメ (CSS .m-enter が無ければ無害)
    node.addEventListener('animationend', function () { node.classList.remove('m-enter'); }, { once: true });
    this.list.appendChild(node);
    wireActions(node);   // repeat 内の data-m-action も配線 (item の値を載せて dispatch)
    wireInputs(node);    // repeat 内の data-m-input も配線
    wireDrag(node);      // repeat 内の data-m-drag も配線
    return { el: node, _case: cas, binds: collectBinds(node, true) };
  };
  Repeat.prototype._bind = function (slot, item) {
    slot.el.style.display = '';
    applyBindings(slot.el, item);
    for (var b = 0; b < slot.binds.length; b++) { applyBindings(slot.binds[b], item); }
  };
  Repeat.prototype.flush = function () {
    var items = this.items();
    if (this.keyField) {
      // key プール: 同じ key の要素を保持 → transform が CSS transition でスライドする。
      var seen = Object.create(null);
      for (var i = 0; i < items.length; i++) {
        var item = items[i], k = String(item[this.keyField]), cas = this._caseOf(item);
        var tpl = this.templates[cas] || this.templates[''];
        if (!tpl) { continue; }
        seen[k] = true;
        var slot = this.byKey[k];
        if (!slot || slot._case !== cas) {
          if (slot && slot.el.parentNode) { slot.el.parentNode.removeChild(slot.el); }
          slot = this.byKey[k] = this._makeSlot(tpl, cas);
        }
        this._bind(slot, item);
      }
      var usesLeave = this.el.hasAttribute('data-m-leave');
      for (var key in this.byKey) {
        if (!seen[key]) {
          var s = this.byKey[key];
          delete this.byKey[key];
          if (s.el.parentNode) {
            if (usesLeave && !s.el._mleaving) {
              s.el._mleaving = true;
              s.el.classList.add('m-leave');
              var _el = s.el;
              var _tid = setTimeout(function () { if (_el.parentNode) { _el.parentNode.removeChild(_el); } }, 400);
              _el.addEventListener('animationend', function () { clearTimeout(_tid); if (_el.parentNode) { _el.parentNode.removeChild(_el); } }, { once: true });
            } else {
              s.el.parentNode.removeChild(s.el);
            }
          }
        }
      }
      return;
    }
    // index プール: 順序ベース (60fps の弾幕等、安定 id が無いもの)。
    for (var n = 0; n < items.length; n++) {
      var it = items[n], c2 = this._caseOf(it), t2 = this.templates[c2] || this.templates[''];
      if (!t2) { continue; }
      var sl = this.pool[n];
      if (!sl || sl._case !== c2) {
        if (sl && sl.el.parentNode) { sl.el.parentNode.removeChild(sl.el); }
        sl = this.pool[n] = this._makeSlot(t2, c2);
      }
      this._bind(sl, it);
    }
    for (var j = items.length; j < this.pool.length; j++) {
      if (this.pool[j]) { this.pool[j].el.style.display = 'none'; }
    }
  };

  // ── バインディング収集 ──
  // ── ドラッグ&ドロップ (data-m-drag / data-m-drop) ─────────────────────────
  //
  //   data-m-drag="field"   この要素を掴めるようにする。field は掴んだ物を識別する値
  //                         (repeat 内なら item のフィールド、外なら state path)
  //   data-m-drop="action"  この要素に落とせるようにする。落ちたら
  //                         dispatch(action, {from: 掴んだ値, to: この要素の data-m-arg})
  //
  // HTML5 の draggable は使わない。OSR (CEF off-screen) では native の
  // StartDragging / DragTarget* をホストが実装しないと dragstart すら発火せず、
  // 「ページは正しいのにドラッグだけ死んでいる」という一番追いにくい壊れ方をする。
  // マウスは既に届いているので、mousedown / mousemove / mouseup だけで組む。
  //
  // 4px 動くまではドラッグ扱いしない。行は data-m-action のクリックも持っていて、
  // 閾値なしでは選択のつもりのクリックが全部ドラッグに化ける。
  var dragState = null;   // {value, el, x, y, moved}
  function wireDrag(root) {
    // root 自身も候補に入れる。querySelectorAll は子孫しか返さないので、repeat の行の
    // ように data-m-drag がスロット要素そのものに付くと、これ無しでは毎回 0 件配線に
    // なる (collectBinds の includeSelf と同じ理由)。
    var found = [];
    if (root.matches && root.matches('[data-m-drag]')) { found.push(root); }
    var q = root.querySelectorAll ? root.querySelectorAll('[data-m-drag]') : [];
    for (var i = 0; i < q.length; i++) { found.push(q[i]); }
    for (var i = 0; i < found.length; i++) {
      (function (el) {
        if (el._mdragWired) { return; }
        el._mdragWired = true;
        el.addEventListener('mousedown', function (e) {
          if (e.button !== 0) { return; }
          // _mdragValue は applyBindings が束縛のたびに焼き直す (_marg と同じ流儀)。
          // ここで resolve し直さないのは、repeat のスロットが使い回されるからで、
          // 配線時の item はもう別の行かもしれない。
          dragState = { value: el._mdragValue,
                        el: el, x: e.clientX, y: e.clientY, moved: false };
        });
      })(found[i]);
    }
  }
  document.addEventListener('mousemove', function (e) {
    if (!dragState) { return; }
    if (!dragState.moved) {
      var dx = e.clientX - dragState.x, dy = e.clientY - dragState.y;
      if (dx * dx + dy * dy < 16) { return; }
      dragState.moved = true;
      dragState.el.classList.add('m-dragging');
    }
    var over = e.target && e.target.closest ? e.target.closest('[data-m-drop]') : null;
    var marked = document.querySelectorAll('.m-drop-hover');
    for (var i = 0; i < marked.length; i++) { marked[i].classList.remove('m-drop-hover'); }
    if (over && over !== dragState.el) { over.classList.add('m-drop-hover'); }
  });
  document.addEventListener('mouseup', function (e) {
    if (!dragState) { return; }
    var st = dragState;
    dragState = null;
    st.el.classList.remove('m-dragging');
    var marked = document.querySelectorAll('.m-drop-hover');
    for (var i = 0; i < marked.length; i++) { marked[i].classList.remove('m-drop-hover'); }
    if (!st.moved) { return; }             // クリックだった。data-m-action に任せる
    var over = e.target && e.target.closest ? e.target.closest('[data-m-drop]') : null;
    if (!over || over === st.el) { return; }
    // _marg も束縛のたびに焼き直されている。落ち先の「いまの」中身が入っている。
    var to = over._marg;
    if (typeof mitiru.dispatch === 'function') {
      mitiru.dispatch(over.dataset.mDrop, { from: st.value, to: to });
    }
  });

  var SELECTOR = '[data-m-text],[data-m-tpl],[data-m-show],[data-m-hide],[data-m-class],[data-m-style],[data-m-pos],[data-m-flash],[data-m-attr],[data-m-action],[data-m-tween],[data-m-enabled],[data-m-disabled],[data-m-value],[data-m-toast]';

  // node 配下の (item スコープ用) 単純バインド要素を集める。repeat はネスト不可とする。
  function collectBinds(root, includeSelf) {
    var out = [];
    if (includeSelf && matches(root, SELECTOR)) { out.push(root); }
    var found = root.querySelectorAll(SELECTOR);
    for (var i = 0; i < found.length; i++) { out.push(found[i]); }
    return out;
  }
  function matches(el, sel) { return el.matches ? el.matches(sel) : false; }

  // path に現れる識別子の「全プレフィックス」を購読候補にする。実在キーがどの長さか
  // (view.persec か view.shop か) は事前に分からないので、全プレフィックスを subscribe
  // する。未設定キーへの購読は no-op なので害は無い。
  function keysIn(str) {
    var keys = [];
    String(str).replace(/[A-Za-z_][\w.]*/g, function (m) {
      var segs = m.split('.');
      for (var n = 1; n <= segs.length; n++) {
        var k = segs.slice(0, n).join('.');
        if (keys.indexOf(k) < 0) { keys.push(k); }
      }
      return m;
    });
    return keys;
  }

  // ── 初期化 ──
  function init() {
    DEBUG = document.documentElement.hasAttribute('data-m-debug') ||
            document.body.hasAttribute('data-m-debug') ||
            /[?&]mdebug=1/.test(location.search);

    // data-m-show の要素は、最初の状態が届くまで隠しておく。素の HTML は全要素が
    // 見えているので、そのままだと起動直後に UI が全部一瞬映ってから消える。
    // data-m-hide (真のとき隠す) は既定で見えているのが正しいので触らない。
    //
    // この script は body の末尾で読まれるため、実行より前に最初のペイントが走る
    // ことがある。その 1 枚も消したいページは、head の style に
    //   html:not(.m-ready) [data-m-show]{display:none !important;}
    // を書いておく。m-ready はここで付けるので、束縛後は inline の display が生きる。
    var preHide = document.querySelectorAll('[data-m-show]');
    for (var ph = 0; ph < preHide.length; ph++) { preHide[ph].style.display = 'none'; }
    document.documentElement.classList.add('m-ready');

    var topBinds = [];   // repeat の外側 (item スコープ無し)
    var repeats = [];
    var subscribed = Object.create(null);

    // repeat コンテナを先に拾い、その内部は topBinds から除外する。
    var repEls = document.querySelectorAll('[data-m-repeat]');
    for (var r = 0; r < repEls.length; r++) {
      var rep = new Repeat(repEls[r]);
      repeats.push(rep);
      keysIn(rep.listPath).forEach(function (k) { subscribed[k] = true; });
    }

    var all = document.querySelectorAll(SELECTOR);
    for (var i = 0; i < all.length; i++) {
      var el = all[i];
      if (el.closest('[data-m-repeat]')) { continue; }   // repeat 内は item 解決に任せる
      topBinds.push(el);
      var dd = el.dataset;
      ['mText', 'mTpl', 'mShow', 'mHide', 'mClass', 'mStyle', 'mAttr', 'mArg', 'mTween',
       'mEnabled', 'mDisabled', 'mValue', 'mToast'].forEach(function (a) {
        if (dd[a] != null) { keysIn(dd[a]).forEach(function (k) { subscribed[k] = true; }); }
      });
    }

    // HTML → C++: data-m-action を dispatch に配線 (ユーザーは JS 不要)。repeat 内の
    // 要素は _makeSlot で都度配線する。
    wireActions(document);
    wireDrag(document);

    // HTML → C++: data-m-input のテキスト入力を配線 (確定値を dispatch)。
    var inputCount = wireInputs(document);

    var pending = false;
    function flush() {
      pending = false;
      for (var i = 0; i < topBinds.length; i++) { applyBindings(topBinds[i], null); }
      for (var j = 0; j < repeats.length; j++) { repeats[j].flush(); }
    }
    function schedule() { if (!pending) { pending = true; (global.requestAnimationFrame || setTimeout)(flush); } }

    Object.keys(subscribed).forEach(function (key) {
      mitiru.onStateChange(key, schedule);   // retained: 購読時に即発火 → 初期描画も走る
    });
    schedule();
    warn('bound', topBinds.length, 'elements,', repeats.length, 'repeats,', inputCount, 'inputs; keys:', Object.keys(subscribed));
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})(typeof window !== 'undefined' ? window : globalThis);
