/*!
 * mitiru_audio.js — no-op-safe audio manager (NF-02)
 *
 * Provides a unified audio API that fires events even when audio hardware is
 * unavailable. Scenes can register haptic hooks on events and the audio layer
 * will call them regardless of whether actual playback occurred.
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.audio.setManifest(manifest | url)    load sound map (inline obj or URL string)
 *   mitiru.audio.manifest()                      frozen copy of current manifest
 *   mitiru.audio.play(key, opts?)                'category.key' → plays or no-ops safely
 *   mitiru.audio.se(key, opts?)                  → play('se.' + key, opts)
 *   mitiru.audio.bgm(key, opts?)                 crossfade to new BGM; null stops
 *   mitiru.audio.voice(key, opts?)               plays; ducks BGM while active
 *   mitiru.audio.stop(category)                  'se'|'bgm'|'voice'|'all'
 *   mitiru.audio.stopAll()                       → stop('all')
 *   mitiru.audio.setVolume(category, 0..1)       'master'|'bgm'|'se'|'voice'
 *   mitiru.audio.volume(category)                current volume for category
 *   mitiru.audio.setDucking({ bgm, fadeMs })     configure BGM duck ratio + fade time
 *   mitiru.audio.on(event, cb)                   subscribe; returns unsubscribe fn
 *   mitiru.audio.off(event, cb)                  unsubscribe
 *   mitiru.audio.isAvailable()                   true when Audio constructor exists
 *
 * ── Events ──────────────────────────────────────────────────────────────────
 *   'play'          { category, key, resolved }   resolved=false when URL not in manifest
 *   'stop'          { category, key }
 *   'bgm:change'    { from, to }
 *   'voice:start'   { key }
 *   'voice:end'     { key }
 *
 * ── Volume math ─────────────────────────────────────────────────────────────
 *   effectiveVolume = master * category * perClip
 *   setVolume('master', 0.5) halves all categories.
 *   setVolume('se', 0) mutes SE while leaving other categories unaffected.
 *
 * Implements spec: docs/feedback-from-kaerucrape/NF-02
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.audio) { return; }  // already loaded

	// ── availability check ────────────────────────────────────────
	// Probed live each call so hot-swapping window.Audio (test harnesses,
	// platform feature flags) is honoured after module load.
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
	var _manifest     = null;   // frozen manifest object
	var _warnedKeys   = {};     // keys warned about (missing from manifest)
	var _warnedNoMfst = false;  // warned about missing manifest

	// Volume levels: master and per-category.
	var _volumes = { master: 1, bgm: 1, se: 1, voice: 1 };

	// Ducking configuration.
	var _ducking = { bgm: 0.3, fadeMs: 200 };

	// Active BGM tracking.
	var _bgmNode     = null;   // current BGM Audio element
	var _bgmKey      = null;   // current BGM key string
	var _bgmDuckMult = 1;      // 1 = normal, <1 = ducked; applied on top of volume

	// Active voice tracking (ref-count for simultaneous voices).
	var _voiceCount = 0;
	var _voiceNodes = [];   // [{ key, node }]

	// Active SE nodes (fire-and-forget; tracked so stop('se') works).
	var _seNodes = [];

	// Event subscribers: { eventName: [fn, ...] }
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

	// ── fade helpers ──────────────────────────────────────────────
	// Linearly fades `node.volume` from `fromVol` to `toVol` over `durationMs`.
	// Calls `onDone()` when complete (or immediately if durationMs <= 0).
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

	// ── SE node GC (remove ended nodes) ──────────────────────────
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

	// ── play internals ────────────────────────────────────────────
	// Returns { node, resolved } — node may be null when audio unavailable.
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
			// URL path — fetch via mitiru.fetch if available, else global fetch.
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

		// Inline object.
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

		// Reapply to live BGM node.
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

		// Split on first '.' only.
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

		// Stop current BGM with fade-out.
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

		// Resolve URL and start new BGM.
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
		var node      = _makeNode(url, 0);  // start silent, fade in
		node.loop     = opts.loop !== false; // BGM loops by default

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
			// No-op for playback but event already fired; simulate immediate end.
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
				catch (_e) { /* ignore */ }
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
				try { snapshot[j].node.pause(); } catch (_e) { /* ignore */ }
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
