/*!
 * mitiru_audio.js — no-op に強い audio manager (NF-02)
 *
 * audio hardware が使えない場合でも event を発火する統合 audio API を提供する。
 * scene は event に haptic hook を登録でき、audio layer は実際の playback が
 * 起きたかに関係なくそれらを呼ぶ。
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.audio.setManifest(manifest | url)    sound map を読み込む (inline obj か URL string)
 *   mitiru.audio.manifest()                      現在の manifest の frozen copy
 *   mitiru.audio.play(key, opts?)                'category.key' → 再生 or 安全に no-op
 *   mitiru.audio.se(key, opts?)                  → play('se.' + key, opts)
 *   mitiru.audio.bgm(key, opts?)                 新 BGM へ crossfade; null で停止
 *   mitiru.audio.voice(key, opts?)               再生; 鳴っている間 BGM を duck
 *   mitiru.audio.stop(category)                  'se'|'bgm'|'voice'|'all'
 *   mitiru.audio.stopAll()                       → stop('all')
 *   mitiru.audio.setVolume(category, 0..1)       'master'|'bgm'|'se'|'voice'
 *   mitiru.audio.volume(category)                category の現在 volume
 *   mitiru.audio.setDucking({ bgm, fadeMs })     BGM の duck 比率 + fade 時間を設定
 *   mitiru.audio.on(event, cb)                   subscribe; unsubscribe fn を返す
 *   mitiru.audio.off(event, cb)                  unsubscribe
 *   mitiru.audio.isAvailable()                   Audio constructor があれば true
 *
 * ── Events ──────────────────────────────────────────────────────────────────
 *   'play'          { category, key, resolved }   URL が manifest に無いと resolved=false
 *   'stop'          { category, key }
 *   'bgm:change'    { from, to }
 *   'voice:start'   { key }
 *   'voice:end'     { key }
 *
 * ── Volume 計算 ─────────────────────────────────────────────────────────────
 *   effectiveVolume = master * category * perClip
 *   setVolume('master', 0.5) は全 category を半分にする。
 *   setVolume('se', 0) は他の category に影響を与えず SE を mute する。
 *
 * Implements spec: docs/feedback-from-kaerucrape/NF-02
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.audio) { return; }  // 読み込み済み

	// ── 利用可否チェック ────────────────────────────────────────
	// 呼び出しごとに live で調べる。これにより module 読み込み後の
	// window.Audio の hot-swap (test harness、platform feature flag) も反映される。
	function _audioAvailable()
	{
		try { return typeof global.Audio === 'function'; }
		catch (_e) { return false; }
	}

	var _warnedNoAudio = false;

	function _checkAudioAvail()
	{
		var avail = _audioAvailable();
		if (!avail && !_warnedNoAudio)
		{
			_warnedNoAudio = true;
			console.warn('[mitiru.audio] Audio constructor unavailable — playback disabled');
		}
		return avail;
	}

	// ── internal state ────────────────────────────────────────────
	var _manifest     = null;   // frozen な manifest object
	var _warnedKeys   = {};     // warn 済みの key (manifest に無いもの)
	var _warnedNoMfst = false;  // manifest 欠落について warn 済みか

	// Volume レベル: master と per-category。
	var _volumes = { master: 1, bgm: 1, se: 1, voice: 1 };

	// Ducking 設定。
	var _ducking = { bgm: 0.3, fadeMs: 200 };

	// Active BGM 追跡。
	var _bgmNode     = null;   // 現在の BGM Audio element
	var _bgmKey      = null;   // 現在の BGM key 文字列
	var _bgmDuckMult = 1;      // 1 = 通常、<1 = ducked; volume に上乗せで適用

	// Active voice 追跡 (同時 voice の参照カウント)。
	var _voiceCount = 0;
	var _voiceNodes = [];   // [{ key, node }]

	// Active SE node (撃ちっ放し; stop('se') 用に追跡)。
	var _seNodes = [];

	// Event subscriber: { eventName: [fn, ...] }
	var _listeners = Object.create(null);

	// ── event emitter ─────────────────────────────────────────────
	function _emit(name, detail)
	{
		var arr = _listeners[name];
		if (!arr || arr.length === 0) { return; }
		var copy = arr.slice();
		for (var i = 0; i < copy.length; ++i)
		{
			try { copy[i](detail); }
			catch (e) { console.error('[mitiru.audio] event handler threw (event=' + name + '):', e); }
		}
	}

	// ── volume helpers ────────────────────────────────────────────
	function _clamp01(v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

	function _effectiveVol(category, perClip)
	{
		perClip = (perClip === undefined) ? 1 : perClip;
		return _clamp01(_volumes.master) * _clamp01(_volumes[category] || 0) * _clamp01(perClip);
	}

	function _bgmEffectiveVol()
	{
		return _effectiveVol('bgm') * _clamp01(_bgmDuckMult);
	}

	function _applyVolumeToNode(node, vol)
	{
		if (node && typeof node.volume !== 'undefined')
		{
			node.volume = _clamp01(vol);
		}
	}

	// ── fade helper ──────────────────────────────────────────────
	// `node.volume` を `durationMs` かけて `fromVol` から `toVol` へ線形に fade する。
	// 完了時 (または durationMs <= 0 なら即座) に `onDone()` を呼ぶ。
	function _fade(node, fromVol, toVol, durationMs, onDone)
	{
		if (!node || durationMs <= 0)
		{
			_applyVolumeToNode(node, toVol);
			if (typeof onDone === 'function') { onDone(); }
			return;
		}

		var start    = null;
		var from     = _clamp01(fromVol);
		var to       = _clamp01(toVol);

		function step(ts)
		{
			if (!start) { start = ts; }
			var elapsed = ts - start;
			var t = Math.min(elapsed / durationMs, 1);
			_applyVolumeToNode(node, from + (to - from) * t);
			if (t < 1)
			{
				global.requestAnimationFrame(step);
			}
			else if (typeof onDone === 'function')
			{
				onDone();
			}
		}

		global.requestAnimationFrame(step);
	}

	// ── manifest helpers ──────────────────────────────────────────
	function _resolveUrl(category, key)
	{
		if (!_manifest) { return null; }
		var cat = _manifest[category];
		if (!cat) { return null; }
		return cat[key] || null;
	}

	function _warnOnce(fullKey)
	{
		if (_warnedKeys[fullKey]) { return; }
		_warnedKeys[fullKey] = true;
		console.warn('[mitiru.audio] unknown key: "' + fullKey + '"');
	}

	// ── Audio node factory ────────────────────────────────────────
	function _makeNode(url, volume)
	{
		var node = new global.Audio(url);
		node.volume = _clamp01(volume);
		return node;
	}

	// ── SE node GC (再生終了した node を除去) ──────────────────────────
	function _gcSeNodes()
	{
		_seNodes = _seNodes.filter(function(n) { return !n.ended; });
	}

	// ── ducking logic ─────────────────────────────────────────────
	function _applyDucking()
	{
		var targetMult = _voiceCount > 0 ? _ducking.bgm : 1;
		var fromVol    = _bgmNode ? _bgmNode.volume : 0;
		_bgmDuckMult   = targetMult;
		_fade(_bgmNode, fromVol, _bgmEffectiveVol(), _ducking.fadeMs, null);
	}

	// ── play 内部処理 ────────────────────────────────────────────
	// { node, resolved } を返す — audio が使えない場合 node は null になりうる。
	function _playAudio(category, key, opts)
	{
		opts = opts || {};
		var fullKey  = category + '.' + key;
		var url      = _resolveUrl(category, key);
		var resolved = (url !== null);

		if (!resolved) { _warnOnce(fullKey); }

		if (!resolved || !_checkAudioAvail()) { return { node: null, resolved: resolved }; }

		var perClip = opts.volume !== undefined ? opts.volume : 1;
		var vol     = _effectiveVol(category, perClip);
		var node    = _makeNode(url, vol);

		if (opts.loop) { node.loop = true; }

		node.play().catch(function(e)
		{
			console.warn('[mitiru.audio] play() rejected for "' + fullKey + '": ' + e.message);
		});

		return { node: node, resolved: resolved };
	}

	// ── public API ────────────────────────────────────────────────
	var audio = mitiru.audio = Object.create(null);

	// ── setManifest ───────────────────────────────────────────────
	audio.setManifest = function(manifestOrUrl)
	{
		if (typeof manifestOrUrl === 'string')
		{
			// URL path — 可能なら mitiru.fetch、無ければ global fetch で取得。
			var fetcher = (mitiru.fetch && typeof mitiru.fetch === 'function')
				? mitiru.fetch
				: global.fetch.bind(global);

			return fetcher(manifestOrUrl)
				.then(function(r)
				{
					if (!r.ok) { throw new Error('mitiru.audio.setManifest: HTTP ' + r.status); }
					return r.json();
				})
				.then(function(obj)
				{
					_manifest     = Object.freeze(obj);
					_warnedKeys   = {};
					_warnedNoMfst = false;
					return _manifest;
				});
		}

		// Inline object。
		if (manifestOrUrl !== null && typeof manifestOrUrl === 'object')
		{
			_manifest     = Object.freeze(Object.assign({}, manifestOrUrl));
			_warnedKeys   = {};
			_warnedNoMfst = false;
			return Promise.resolve(_manifest);
		}

		return Promise.reject(new Error('mitiru.audio.setManifest: expected object or URL string'));
	};

	// ── manifest ──────────────────────────────────────────────────
	audio.manifest = function()
	{
		return _manifest;
	};

	// ── isAvailable ───────────────────────────────────────────────
	audio.isAvailable = function()
	{
		return _audioAvailable();
	};

	// ── on / off ─────────────────────────────────────────────────
	audio.on = function(event, cb)
	{
		if (typeof event !== 'string' || typeof cb !== 'function')
		{
			throw new Error('mitiru.audio.on: (string, function) required');
		}
		if (!_listeners[event]) { _listeners[event] = []; }
		_listeners[event].push(cb);
		return function() { audio.off(event, cb); };
	};

	audio.off = function(event, cb)
	{
		var arr = _listeners[event];
		if (!arr) { return; }
		var i = arr.indexOf(cb);
		if (i >= 0) { arr.splice(i, 1); }
	};

	// ── volume ────────────────────────────────────────────────────
	audio.setVolume = function(category, value)
	{
		var valid = { master: 1, bgm: 1, se: 1, voice: 1 };
		if (!valid[category])
		{
			throw new Error('mitiru.audio.setVolume: unknown category "' + category + '"');
		}
		_volumes[category] = _clamp01(value);

		// live な BGM node へ再適用。
		if (category === 'master' || category === 'bgm')
		{
			_applyVolumeToNode(_bgmNode, _bgmEffectiveVol());
		}
	};

	audio.volume = function(category)
	{
		if (!(category in _volumes))
		{
			throw new Error('mitiru.audio.volume: unknown category "' + category + '"');
		}
		return _volumes[category];
	};

	// ── setDucking ────────────────────────────────────────────────
	audio.setDucking = function(opts)
	{
		opts = opts || {};
		if (typeof opts.bgm    === 'number') { _ducking.bgm    = _clamp01(opts.bgm); }
		if (typeof opts.fadeMs === 'number') { _ducking.fadeMs = Math.max(0, opts.fadeMs); }
	};

	// ── play ──────────────────────────────────────────────────────
	audio.play = function(fullKey, opts)
	{
		if (!_manifest && !_warnedNoMfst)
		{
			_warnedNoMfst = true;
			console.warn('[mitiru.audio] play() called before setManifest()');
		}

		// 最初の '.' でのみ分割する。
		var dotIdx   = typeof fullKey === 'string' ? fullKey.indexOf('.') : -1;
		var category = dotIdx >= 0 ? fullKey.slice(0, dotIdx) : '';
		var key      = dotIdx >= 0 ? fullKey.slice(dotIdx + 1) : '';

		if (!category || !key)
		{
			console.warn('[mitiru.audio] play(): expected "category.key", got: ' + fullKey);
			_emit('play', { category: category, key: key, resolved: false });
			return null;
		}

		var result   = _manifest ? _playAudio(category, key, opts) : { node: null, resolved: false };
		var resolved = _manifest ? result.resolved : false;

		_emit('play', { category: category, key: key, resolved: resolved });
		return result.node;
	};

	// ── se ────────────────────────────────────────────────────────
	audio.se = function(key, opts)
	{
		var result = audio.play('se.' + key, opts);
		if (result)
		{
			_gcSeNodes();
			_seNodes.push(result);
		}
		return result;
	};

	// ── bgm ───────────────────────────────────────────────────────
	audio.bgm = function(key, opts)
	{
		opts = opts || {};
		var fadeMs  = typeof opts.fadeMs === 'number' ? opts.fadeMs : 400;
		var fromKey = _bgmKey;

		// 現在の BGM を fade-out して停止。
		if (_bgmNode)
		{
			var nodeToStop = _bgmNode;
			var volNow     = nodeToStop.volume;
			_bgmNode = null;
			_bgmKey  = null;

			_fade(nodeToStop, volNow, 0, fadeMs, function()
			{
				nodeToStop.pause();
				nodeToStop.src = '';
			});
		}

		_emit('bgm:change', { from: fromKey, to: key || null });

		if (!key) { return; }

		// URL を解決して新 BGM を開始。
		var url = _resolveUrl('bgm', key);
		if (!url)
		{
			_warnOnce('bgm.' + key);
			_emit('play', { category: 'bgm', key: key, resolved: false });
			return;
		}

		if (!_checkAudioAvail())
		{
			_emit('play', { category: 'bgm', key: key, resolved: true });
			return;
		}

		var targetVol = _bgmEffectiveVol();
		var node      = _makeNode(url, 0);  // 無音で開始し fade in
		node.loop     = opts.loop !== false; // BGM は default で loop

		_bgmNode = node;
		_bgmKey  = key;

		node.play().catch(function(e)
		{
			console.warn('[mitiru.audio] bgm play() rejected: ' + e.message);
		});

		_fade(node, 0, targetVol, fadeMs, null);
		_emit('play', { category: 'bgm', key: key, resolved: true });
	};

	// ── voice ─────────────────────────────────────────────────────
	audio.voice = function(key, opts)
	{
		var url      = _resolveUrl('voice', key);
		var resolved = (url !== null);

		if (!resolved) { _warnOnce('voice.' + key); }

		_emit('voice:start', { key: key });
		_voiceCount++;
		_applyDucking();

		if (!resolved || !_checkAudioAvail())
		{
			// playback は no-op だが event は発火済み; 即座の終了を模擬する。
			_voiceCount = Math.max(0, _voiceCount - 1);
			_applyDucking();
			_emit('voice:end', { key: key });
			_emit('play', { category: 'voice', key: key, resolved: false });
			return null;
		}

		var perClip = (opts && opts.volume !== undefined) ? opts.volume : 1;
		var vol     = _effectiveVol('voice', perClip);
		var node    = _makeNode(url, vol);

		_voiceNodes.push({ key: key, node: node });

		node.addEventListener('ended', function()
		{
			_onVoiceEnded(key, node);
		});

		node.play().catch(function(e)
		{
			console.warn('[mitiru.audio] voice play() rejected for "' + key + '": ' + e.message);
		});

		_emit('play', { category: 'voice', key: key, resolved: true });
		return node;
	};

	function _onVoiceEnded(key, node)
	{
		_voiceNodes = _voiceNodes.filter(function(v) { return v.node !== node; });
		_voiceCount = Math.max(0, _voiceCount - 1);
		_applyDucking();
		_emit('voice:end', { key: key });
	}

	// ── stop ─────────────────────────────────────────────────────
	audio.stop = function(category)
	{
		if (category === 'all')
		{
			audio.stop('se');
			audio.stop('bgm');
			audio.stop('voice');
			return;
		}

		if (category === 'se')
		{
			_gcSeNodes();
			for (var i = 0; i < _seNodes.length; ++i)
			{
				try
				{
					_seNodes[i].pause();
					_seNodes[i].currentTime = 0;
				}
				catch (_e) { /* 無視 */ }
				_emit('stop', { category: 'se', key: '' });
			}
			_seNodes = [];
			return;
		}

		if (category === 'bgm')
		{
			var key = _bgmKey;
			if (_bgmNode)
			{
				var node = _bgmNode;
				_bgmNode = null;
				_bgmKey  = null;
				_fade(node, node.volume, 0, 200, function()
				{
					node.pause();
					node.src = '';
				});
			}
			_emit('stop', { category: 'bgm', key: key || '' });
			return;
		}

		if (category === 'voice')
		{
			var snapshot = _voiceNodes.slice();
			_voiceNodes  = [];
			_voiceCount  = 0;
			for (var j = 0; j < snapshot.length; ++j)
			{
				try { snapshot[j].node.pause(); } catch (_e) { /* 無視 */ }
				_emit('stop',       { category: 'voice', key: snapshot[j].key });
				_emit('voice:end',  { key: snapshot[j].key });
			}
			_applyDucking();
			return;
		}

		console.warn('[mitiru.audio] stop(): unknown category "' + category + '"');
	};

	// ── stopAll ───────────────────────────────────────────────────
	audio.stopAll = function()
	{
		audio.stop('all');
	};

})(typeof window !== 'undefined' ? window : globalThis);
