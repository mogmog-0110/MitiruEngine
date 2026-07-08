/*!
 * mitiru_bind_tools.js — ツール窓 (SharedSnapshot 系) を zero-JS 化する読み取り専用 widget 群
 *
 * mitiru_tool_cef の C++ は SharedSnapshot 全体を window.applySnapshot(env) で push する
 * (replay は window.applyReplay(d))。このモジュールがそれを state store の retained key に
 * 写すので、各ツールページは mitiru_bind.js の data-m-* 属性だけで書ける。
 *
 * 前提: mitiru_cef_state.js → mitiru_bind.js → 本ファイル の順に読み込むこと。
 *
 * ── C++ push → state key ────────────────────────────────────────────────
 *   applySnapshot(env)  → tool.ready (bool) / tool.snap (snapshot object)
 *   applyReplay(d)      → replay.loaded / replay.file / replay.error / replay.ok /
 *                         replay.total / replay.cur / replay.pct / replay.frame /
 *                         replay.keys ([{k}]) / replay.strip ([{h}])  (初回 push のみ採用)
 *
 * ── widget 語彙 (1 ツール窓 = 1 関心事なので各 widget はページに 1 個) ──
 *   data-m-tree="path"            JSON を開閉つきツリー表示 (scene tree)。tree.count を publish
 *     data-m-tree-root="label"      ルート行のラベル (既定 "data")
 *   data-m-kv="path"              {section:{title,state}} を section-title + kv 表で表示。
 *     data-m-kv-skip="a,b"          除外 key / data-m-kv-only="x" は単一 key に限定。
 *                                 kv.count (描画した section 数) を publish
 *   data-m-spark-push="path"      数値パスを購読して履歴スパークラインを canvas に描画
 *     data-m-spark-cap="180"        保持サンプル数 / data-m-spark-baseline="60" 基準線 +
 *     data-m-spark-min="75"         スケール下限
 *   data-m-rewind="path"          過去フレーム記録の state を購読し tt.ok / tt.pct を publish。
 *                                 [data-m-tt-playpause] で再生/一時停止、[data-m-tt-scrub] を
 *                                 ドラッグして過去へ戻す (cefQuery "timetravel.scrub|<offset>")
 *   data-m-replay-scrub           replay のスクラブバー。クリック / ←→ Home End で移動
 */
(function (global) {
  'use strict';
  var mitiru = global.mitiru;
  if (!mitiru || typeof mitiru.onStateChange !== 'function') {
    console.error('[mitiru-bind-tools] window.mitiru not found — load mitiru_cef_state.js first.');
    return;
  }
  var document = global.document;

  // widget が導出した値を retained state に流す (binder が購読して DOM に写す)。
  function publish(key, value) {
    if (mitiru._state && typeof mitiru._state._onChange === 'function') {
      mitiru._state._onChange(key, value);
    }
  }

  // binder と同じ最長プレフィックス path 解決 (store key + JSON 追跡)。
  function resolve(path) {
    var segs = String(path).split('.');
    for (var n = segs.length; n >= 1; n--) {
      var v = mitiru.getState(segs.slice(0, n).join('.'));
      if (v === undefined) { continue; }
      if (typeof v === 'string') {
        var s = v.trim();
        if (s.length && (s[0] === '{' || s[0] === '[')) {
          try { v = JSON.parse(s); } catch (e) { /* 文字列のまま */ }
        }
      }
      for (var j = n; j < segs.length; j++) {
        if (v == null) { return undefined; }
        v = v[segs[j]];
      }
      return v;
    }
    return undefined;
  }

  function subscribePrefixes(path, fn) {
    var segs = String(path).split('.');
    for (var n = 1; n <= segs.length; n++) {
      mitiru.onStateChange(segs.slice(0, n).join('.'), fn);
    }
  }

  function splitList(spec) {
    if (!spec) { return []; }
    return String(spec).split(',').map(function (s) { return s.trim(); })
      .filter(function (s) { return s.length > 0; });
  }

  // ── C++ → JS 入口: SharedSnapshot push を state key へ ─────────────────
  global.applySnapshot = function (env) {
    publish('tool.ready', !!(env && env.ready));
    publish('tool.snap', (env && env.snap) || {});
  };

  // ── スパークライン描画 (テクニカルノートテーマ: ink 単色 + 破線カーソル) ──
  function drawSpark(canvas, arr, opts) {
    if (typeof canvas.getContext !== 'function') { return; }
    var x = canvas.getContext('2d');
    if (!x) { return; }
    var dpr = global.devicePixelRatio || 1;
    var w = canvas.clientWidth || canvas.width, h = canvas.clientHeight || canvas.height;
    canvas.width = w * dpr; canvas.height = h * dpr;
    x.scale(dpr, dpr); x.clearRect(0, 0, w, h);
    if (!arr.length) { return; }
    var i, px, py;
    if (opts.baseline) {
      // 絶対スケール (0 起点) + 基準線とラベル (perf の 60fps 線)。
      var mx = opts.minScale || opts.baseline * 1.25;
      for (i = 0; i < arr.length; i++) { mx = Math.max(mx, arr[i] * 1.1); }
      var by = h - (opts.baseline / mx) * h;
      x.strokeStyle = '#e6e7ea'; x.lineWidth = 1;
      x.beginPath(); x.moveTo(0, by); x.lineTo(w, by); x.stroke();
      x.fillStyle = '#999da4'; x.font = "12px 'IBM Plex Mono','Consolas',monospace";
      x.fillText(String(opts.baseline), w - 22, by - 6);
      if (arr.length >= 2) {
        var cap = opts.cap || arr.length;
        x.strokeStyle = '#1a1c1f'; x.lineWidth = 2; x.lineJoin = 'round'; x.beginPath();
        for (i = 0; i < arr.length; i++) {
          px = w * i / (cap - 1); py = h - (arr[i] / mx) * h;
          if (i) { x.lineTo(px, py); } else { x.moveTo(px, py); }
        }
        x.stroke();
      }
      return;
    }
    // 相対スケール (min-max 正規化)。
    var mn = Math.min.apply(null, arr), mx2 = Math.max.apply(null, arr);
    if (mx2 === mn) { mx2 = mn + 1; }
    x.strokeStyle = '#1a1c1f'; x.lineWidth = 2; x.lineJoin = 'round'; x.beginPath();
    for (i = 0; i < arr.length; i++) {
      px = w * i / (arr.length - 1 || 1);
      py = h - 2 - ((arr[i] - mn) / (mx2 - mn)) * (h - 4);
      if (i) { x.lineTo(px, py); } else { x.moveTo(px, py); }
    }
    x.stroke();
    if (opts.cursor != null) {
      var cx = w * opts.cursor / (arr.length - 1 || 1);
      x.strokeStyle = '#1a1c1f'; x.lineWidth = 1; x.setLineDash([3, 3]);
      x.beginPath(); x.moveTo(cx, 0); x.lineTo(cx, h); x.stroke();
      x.setLineDash([]);
    }
  }

  // ── data-m-spark-push: 数値パスの履歴を canvas に描く (perf) ───────────
  function initSparkPush(canvas) {
    var d = canvas.dataset;
    var path = d.mSparkPush;
    var cap = Number(d.mSparkCap) || 180;
    var opts = { baseline: Number(d.mSparkBaseline) || 0, minScale: Number(d.mSparkMin) || 0, cap: cap };
    var hist = [];
    function draw() { drawSpark(canvas, hist, opts); }
    subscribePrefixes(path, function () {
      var v = Number(resolve(path));
      if (isNaN(v)) { return; }
      hist.push(v);
      while (hist.length > cap) { hist.shift(); }
      draw();
    });
    global.addEventListener('resize', draw);
  }

  // ── data-m-tree: JSON の開閉ツリー (scene tree) ─────────────────────────
  function initTree(el) {
    var path = el.dataset.mTree;
    var rootLabel = el.dataset.mTreeRoot || 'data';
    var collapsed = Object.create(null);   // 明示的に閉じた path (既定は展開)
    var lastJson = '', lastSnap = null;

    function leafRow(line, key, node) {
      var tog = document.createElement('span'); tog.className = 'tog leaf'; line.appendChild(tog);
      var k = document.createElement('span'); k.className = 'key'; k.textContent = key; line.appendChild(k);
      var c = document.createElement('span'); c.className = 'colon'; c.textContent = ':'; line.appendChild(c);
      var v = document.createElement('span'); v.className = 'val'; v.textContent = String(node); line.appendChild(v);
    }
    function row(key, node, depth, p) {
      var frag = document.createDocumentFragment();
      var line = document.createElement('div'); line.className = 'row';
      for (var i = 0; i < depth; i++) {
        var ind = document.createElement('span'); ind.className = 'indent line'; line.appendChild(ind);
      }
      frag.appendChild(line);
      if (node === null || typeof node !== 'object') { leafRow(line, key, node); return frag; }
      var arr = Array.isArray(node);
      var open = !collapsed[p];
      var tog = document.createElement('span'); tog.className = 'tog'; tog.textContent = open ? '-' : '+';
      line.appendChild(tog);
      var k = document.createElement('span'); k.className = 'key'; k.textContent = key; line.appendChild(k);
      var n = arr ? node.length : Object.keys(node).length;
      if (n) {
        var hint = document.createElement('span'); hint.className = 'obj-hint';
        hint.textContent = arr ? n + ' items' : n + ' keys';
        line.appendChild(hint);
      }
      var kids = document.createElement('div');
      kids.className = 'children' + (open ? '' : ' collapsed');
      var entries = arr ? node.map(function (v, i2) { return ['[' + i2 + ']', v]; })
                        : Object.keys(node).map(function (k2) { return [k2, node[k2]]; });
      for (var e = 0; e < entries.length; e++) {
        kids.appendChild(row(entries[e][0], entries[e][1], depth + 1, p + '/' + entries[e][0]));
      }
      frag.appendChild(kids);
      tog.addEventListener('click', function () {
        if (collapsed[p]) { delete collapsed[p]; } else { collapsed[p] = true; }
        rebuild();   // 同じ snap で再描画 (開閉状態は collapsed が保持)
      });
      return frag;
    }
    function rebuild() {
      el.textContent = '';
      el.appendChild(row(rootLabel, lastSnap, 0, rootLabel));
    }
    subscribePrefixes(path, function () {
      var snap = resolve(path);
      var count = (snap && typeof snap === 'object') ? Object.keys(snap).length : 0;
      publish('tree.count', count);
      if (!count) { return; }
      var j = JSON.stringify(snap);
      if (j === lastJson) { return; }   // 構造が変わらなければ再描画しない
      lastJson = j; lastSnap = snap;
      rebuild();
    });
  }

  // ── data-m-kv: {section:{title,state}} → section-title + kv 表 ─────────
  function kvValue(v) {
    if (v === null) { return 'null'; }
    // 数値: 整数はそのまま、小数は 2 桁に丸める (生の float 精度で桁がばらつくのを防ぐ)。
    if (typeof v === 'number') { return Number.isInteger(v) ? String(v) : v.toFixed(2); }
    if (Array.isArray(v)) {
      // primitive 配列は中身を列挙、オブジェクト配列は件数 (詳細は scene tree で)
      var allPrim = v.every(function (x) { return typeof x !== 'object' || x === null; });
      if (allPrim) { return '[ ' + v.join(', ') + ' ]'; }
      return v.length + ' items';
    }
    if (typeof v === 'object') {
      // ネストした object は中身を inline 要約
      var parts = Object.keys(v).map(function (k) {
        var x = v[k];
        return k + ' ' + (x !== null && typeof x === 'object' ? '…' : String(x));
      });
      var s = parts.join('  ·  ');
      return s.length > 64 ? s.slice(0, 62) + '…' : s;
    }
    return String(v);
  }
  function kvRow(host, key, value) {
    var r = document.createElement('div'); r.className = 'row';
    if (key != null) {
      var k = document.createElement('span'); k.className = 'k'; k.textContent = key; r.appendChild(k);
    }
    var v = document.createElement('span'); v.className = 'v'; v.textContent = kvValue(value); r.appendChild(v);
    host.appendChild(r);
  }
  function kvSection(host, key, entry) {
    var title = document.createElement('div'); title.className = 'section-title';
    title.textContent = (entry && entry.title) || key;
    host.appendChild(title);
    var box = document.createElement('div'); box.className = 'kv';
    var state = (entry && typeof entry.state === 'object') ? entry.state : entry;
    if (state && typeof state === 'object' && !Array.isArray(state)) {
      // order 配列が来ていれば宣言順で、無ければ key 順で並べる。
      var keys = (entry && Array.isArray(entry.order)) ? entry.order : Object.keys(state);
      keys.forEach(function (kk) { if (kk in state) { kvRow(box, kk, state[kk]); } });
    } else if (Array.isArray(state)) {
      state.forEach(function (vv, i) { kvRow(box, '[' + i + ']', vv); });
    } else {
      kvRow(box, null, state);
    }
    host.appendChild(box);
  }
  function initKv(el) {
    var path = el.dataset.mKv;
    var skip = splitList(el.dataset.mKvSkip);
    var only = splitList(el.dataset.mKvOnly);
    var lastJson = '';
    subscribePrefixes(path, function () {
      var snap = resolve(path);
      var keys = (snap && typeof snap === 'object') ? Object.keys(snap) : [];
      if (only.length) { keys = keys.filter(function (k) { return only.indexOf(k) >= 0; }); }
      keys = keys.filter(function (k) { return skip.indexOf(k) < 0; });
      publish('kv.count', keys.length);
      if (!keys.length) { el.textContent = ''; lastJson = ''; return; }
      var j = JSON.stringify(keys.map(function (k) { return snap[k]; }));
      if (j === lastJson) { return; }
      lastJson = j;
      el.textContent = '';
      keys.forEach(function (k) { kvSection(el, k, snap[k]); });
    });
  }

  // ── data-m-rewind: ▶/⏸ 付きの横シークバー (YouTube 風) ──
  //   再生中はつまみが最新へ追従。一時停止すると好きなだけ過去のフレームを見られる。
  function initTimeTravel(root) {
    var path = root.dataset.mRewind;
    var scrubEl = document.querySelector('[data-m-tt-scrub]');
    var ppEl = document.querySelector('[data-m-tt-playpause]');
    var S = null, at = 0, paused = false, dragging = false;

    function setPaused(p) {
      paused = p;
      document.body.classList.toggle('paused', p);
      document.body.classList.toggle('playing', !p);
    }

    // フレーム数: capacity を優先。無ければ *History 配列長 (値の履歴を出す章との後方互換)。
    function frameCount(st) {
      if (st && st.capacity != null) { return st.capacity | 0; }
      var chs = Object.keys(st || {}).filter(function (k) { return /History$/.test(k) && Array.isArray(st[k]); });
      return chs.length ? st[chs[0]].length : 0;
    }
    function render() {
      if (!S) { return; }
      var len = frameCount(S);
      at = Math.max(0, Math.min(len - 1, at));
      publish('tt.ok', len > 1);
      publish('tt.pct', len > 1 ? at / (len - 1) * 100 : 0);
    }
    // つまみの位置 (at, 0=最古) を offsetFromNewest(0=最新) に変換し、そのフレームで止める。
    function sendScrub() {
      if (!S || typeof global.cefQuery !== 'function') { return; }
      var len = frameCount(S);
      if (len < 1) { return; }
      global.cefQuery({
        request: 'timetravel.scrub|' + ((len - 1) - at),
        onSuccess: function () {}, onFailure: function () {},
      });
    }
    function sendResume() {
      if (typeof global.cefQuery !== 'function') { return; }
      global.cefQuery({ request: 'timetravel.resume|1', onSuccess: function () {}, onFailure: function () {} });
    }
    subscribePrefixes(path, function () {
      var st = resolve(path);
      var len = (st && typeof st === 'object') ? frameCount(st) : 0;
      if (!len) { S = null; publish('tt.ok', false); return; }
      S = st;
      if (!paused) { at = len - 1; }   // 再生中は最新 (ライブ端) へ追従。停止中は位置を保つ
      render();
    });
    function atFromEvent(e) {
      var len = frameCount(S), r = scrubEl.getBoundingClientRect();
      return Math.round((e.clientX - r.left) / (r.width || 1) * (len - 1));
    }

    // ▶/⏸ ボタン: 再生 ⇄ 一時停止。
    if (ppEl) {
      ppEl.addEventListener('click', function () {
        if (!S) { return; }
        if (paused) { setPaused(false); at = frameCount(S) - 1; render(); sendResume(); }  // ▶ そこから再生
        else { setPaused(true); at = frameCount(S) - 1; render(); sendScrub(); }             // ⏸ いまで止める
      });
    }
    // バーを掴む = 自動で一時停止 + そのフレームへ。離しても止まったまま (好きなだけ見られる)。
    if (scrubEl) {
      scrubEl.addEventListener('pointerdown', function (e) {
        if (!S) { return; }
        dragging = true;
        try { scrubEl.setPointerCapture(e.pointerId); } catch (err) {}
        if (!paused) { setPaused(true); }
        at = atFromEvent(e); render(); sendScrub();
      });
      scrubEl.addEventListener('pointermove', function (e) {
        if (!dragging || !S) { return; }
        at = atFromEvent(e); render(); sendScrub();
      });
      var endDrag = function () { dragging = false; };   // 離しても停止のまま (再生は ▶ で)
      scrubEl.addEventListener('pointerup', endDrag);
      scrubEl.addEventListener('pointercancel', endDrag);
    }
    // 停止中は ← → で 1 フレームずつ。
    document.addEventListener('keydown', function (e) {
      if (!S || !paused) { return; }
      if (e.key === 'ArrowLeft') { at--; render(); sendScrub(); }
      else if (e.key === 'ArrowRight') { at++; render(); sendScrub(); }
    });
  }

  // ── replay: applyReplay を受けて derived state を publish (読み取り専用) ──
  var replayFrames = [], replayCur = 0, replayLoaded = false;
  function publishReplayCur() {
    if (!replayFrames.length) { return; }
    replayCur = Math.max(0, Math.min(replayFrames.length - 1, replayCur));
    var f = replayFrames[replayCur];
    publish('replay.cur', replayCur + 1);
    publish('replay.pct', replayFrames.length > 1 ? replayCur / (replayFrames.length - 1) * 100 : 0);
    publish('replay.frame', { i: f.i, mx: f.mx, my: f.my, mb: f.mb || '', pad: !!f.pad });
    publish('replay.keys', (f.k || []).map(function (k) { return { k: k }; }));
  }
  global.applyReplay = function (d) {
    if (replayLoaded) { return; }   // C++ は毎 tick push → 初回のみ (scrub 位置を守る)
    replayLoaded = true;
    publish('replay.loaded', true);
    publish('replay.file', (d && d.file) || '');
    var err = (d && d.error) || '';
    var frames = (d && d.frames) || [];
    if (!err && !frames.length) { err = 'empty recording (0 frames)'; }
    publish('replay.error', err);
    publish('replay.ok', !err);
    if (err) { return; }
    replayFrames = frames; replayCur = 0;
    publish('replay.total', frames.length);
    // 入力活動 strip (held key 数で高さ、最大 200 本)
    var N = Math.min(frames.length, 200), step = frames.length / N, strip = [];
    for (var i = 0; i < N; i++) {
      var fr = frames[Math.floor(i * step)];
      strip.push({ h: fr.k ? Math.min(100, fr.k.length * 30 + 8) : 2 });
    }
    publish('replay.strip', strip);
    publishReplayCur();
  };
  function initReplayScrub(scrubEl) {
    document.addEventListener('keydown', function (e) {
      if (!replayFrames.length) { return; }
      if (e.key === 'ArrowRight') { replayCur++; publishReplayCur(); }
      else if (e.key === 'ArrowLeft') { replayCur--; publishReplayCur(); }
      else if (e.key === 'Home') { replayCur = 0; publishReplayCur(); }
      else if (e.key === 'End') { replayCur = replayFrames.length - 1; publishReplayCur(); }
    });
    scrubEl.addEventListener('click', function (e) {
      if (!replayFrames.length) { return; }
      var r = scrubEl.getBoundingClientRect();
      replayCur = Math.round((e.clientX - r.left) / (r.width || 1) * (replayFrames.length - 1));
      publishReplayCur();
    });
  }

  // ── 初期化 ──
  function init() {
    var el = document.querySelector('[data-m-tree]');
    if (el) { initTree(el); }
    el = document.querySelector('[data-m-kv]');
    if (el) { initKv(el); }
    var sparks = document.querySelectorAll('[data-m-spark-push]');
    for (var i = 0; i < sparks.length; i++) { initSparkPush(sparks[i]); }
    el = document.querySelector('[data-m-rewind]');
    if (el) { initTimeTravel(el); }
    el = document.querySelector('[data-m-replay-scrub]');
    if (el) { initReplayScrub(el); }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})(typeof window !== 'undefined' ? window : globalThis);
