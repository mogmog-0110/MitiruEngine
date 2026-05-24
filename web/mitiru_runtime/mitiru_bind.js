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
 *   data-m-text="path"            textContent にパスの値 (+ data-m-format=int|kmb|pct|time|comma)
 *   data-m-tpl="… {path} …"       テンプレ文字列 ({path} / {path:fmt} を埋める)
 *   data-m-show="path"            真のとき表示 ("path == v" / "!=" / "!path" / 数値比較 "< > <= >=" 可)
 *   data-m-hide="path"            show の反転
 *   data-m-class="cls: path; …"   真のときクラス付与
 *   data-m-style="prop: {path}u"  スタイル束縛 (例 "width: {view.boss.pct}%")
 *   data-m-attr="src: {path}; …"  任意属性に値をバインド (画像 src / title / aria 等)
 *   data-m-action="name"          クリック/入力で dispatch(name, arg) (HTML → C++ 入力)
 *     data-m-arg="path"             dispatch に載せる値 (repeat 内なら item の値。例: 押した項目の id)
 *     フォーム要素 (input/select) は現在値を自動で arg に載せる (スライダー/選択 等の設定 UI)
 *   data-m-flash="field"          値が変わった瞬間に m-flash クラスを一瞬付与 (CSS 発火用)
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
  var rawCache = Object.create(null);     // key -> last raw string
  var parsedCache = Object.create(null);  // key -> parsed (object/array/string/number)

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
    return v == null ? '' : String(v);
  }

  // "… {path} … {path:fmt} …" を埋める。
  function renderTpl(tpl, item) {
    return String(tpl).replace(/\{([^}]+)\}/g, function (_, expr) {
      var parts = expr.split(':');
      return fmt(resolve(parts[0].trim(), item), parts[1] && parts[1].trim());
    });
  }

  // ── 1 要素へ data-m-* を適用 (item != null なら item スコープ) ──
  function applyBindings(el, item) {
    var d = el.dataset;
    if (d.mText != null)  { el.textContent = fmt(resolve(d.mText, item), d.mFormat); }
    if (d.mTpl != null)   { el.textContent = renderTpl(d.mTpl, item); }
    if (d.mShow != null)  { el.style.display = evalCond(d.mShow, item) ? '' : 'none'; }
    if (d.mHide != null)  { el.style.display = evalCond(d.mHide, item) ? 'none' : ''; }
    if (d.mClass != null) { applyClass(el, d.mClass, item); }
    if (d.mStyle != null) { applyStyle(el, d.mStyle, item); }
    if (d.mPos != null)   { applyPos(el, d.mPos, d.mRot, d.mAnchor, item); }
    if (d.mFlash != null) { applyFlash(el, d.mFlash, item); }
    if (d.mAttr != null)  { applyAttr(el, d.mAttr, item); }
    if (d.mArg != null)   { el._marg = resolve(d.mArg, item); }   // dispatch に載せる値 (item スコープ対応)
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
  // dispatch に載せる引数: data-m-arg があればその値 (item スコープは _marg)、無ければフォーム値。
  function actionArg(el) {
    if (el.dataset.mArg != null) {
      return (el._marg !== undefined) ? el._marg : resolve(el.dataset.mArg, null);
    }
    return formValue(el);
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
      el.addEventListener(ev, function () {
        if (typeof mitiru.dispatch === 'function') { mitiru.dispatch(el.dataset.mAction, actionArg(el)); }
      });
    });
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
    this.templates = {};       // case -> <template>, '' -> default
    this.pool = [];            // index プール (keyField 無し時)
    this.byKey = Object.create(null);  // key プール (keyField 有り時)
    var tpls = container.querySelectorAll(':scope > template');
    for (var i = 0; i < tpls.length; i++) {
      this.templates[tpls[i].dataset.mCase || ''] = tpls[i];
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
      for (var key in this.byKey) {
        if (!seen[key]) { var s = this.byKey[key]; if (s.el.parentNode) { s.el.parentNode.removeChild(s.el); } delete this.byKey[key]; }
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
  var SELECTOR = '[data-m-text],[data-m-tpl],[data-m-show],[data-m-hide],[data-m-class],[data-m-style],[data-m-pos],[data-m-flash],[data-m-attr],[data-m-action]';

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
      ['mText', 'mTpl', 'mShow', 'mHide', 'mClass', 'mStyle', 'mAttr', 'mArg'].forEach(function (a) {
        if (dd[a] != null) { keysIn(dd[a]).forEach(function (k) { subscribed[k] = true; }); }
      });
    }

    // HTML → C++: data-m-action を dispatch に配線 (ユーザーは JS 不要)。repeat 内の
    // 要素は _makeSlot で都度配線する。
    wireActions(document);

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
    warn('bound', topBinds.length, 'elements,', repeats.length, 'repeats; keys:', Object.keys(subscribed));
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})(typeof window !== 'undefined' ? window : globalThis);
