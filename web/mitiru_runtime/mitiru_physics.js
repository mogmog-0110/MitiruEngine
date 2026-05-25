/*!
 * mitiru_physics.js — C++ physics bridge の JS consumer (H-06)
 *
 * window.cefQuery 経由の CEF/Box2D bridge を promise ベース API で包む。
 * simulation は全て C++ が所有する; JS は frame ごとの position/angle 更新と
 * contact event を requestAnimationFrame poll loop で受け取る。
 *
 * window.cefQuery が undefined の場合 (素の browser / mock 無しの test) は、
 * 全 call が即座に Error('physics bridge unavailable') で reject する。
 * JS 側の fallback は無い — engine 哲学: simulation は C++ が所有する。
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.physics.createWorld(opts)           Promise<World>
 *     opts: { gravityX, gravityY }
 *
 * World インスタンスの method:
 *   world.createCircle(opts)                   Promise<bodyId:number>
 *     opts: { x, y, r, density, restitution, friction, userData }
 *   world.createStaticEdge(opts)               Promise<bodyId:number>
 *     opts: { x1, y1, x2, y2, friction, restitution }
 *   world.destroyBody(bodyId)                  Promise<void>
 *   world.setLinearVelocity(bodyId, vx, vy)   Promise<void>
 *   world.on(eventName, fn)                    unsubscribe:function
 *     events: 'bodies:update'  → fn([{id,x,y,angle},…])
 *             'contact'        → fn({a,b,aData,bData})
 *             'error'          → fn(Error)
 *   world.start()                              void  (冪等)
 *   world.stop()                               void  (冪等)
 *   world.destroy()                            Promise<void>
 *
 * Implements spec: docs/hato-project-engine-requests-20260425.md H-06
 */
(function(global)
{
	'use strict';

	var mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.physics) { return; }  // 読み込み済み

	// ── bridge の利用可否 ───────────────────────────────────────
	var _warnedUnavail = false;

	function _bridgeAvail()
	{
		return typeof global.cefQuery === 'function';
	}

	function _requireBridge()
	{
		if (!_bridgeAvail())
		{
			if (!_warnedUnavail)
			{
				_warnedUnavail = true;
				console.warn('[mitiru.physics] CEF bridge unavailable — all calls will reject');
			}
			return false;
		}
		return true;
	}

	// ── 低レベル cefQuery wrapper ────────────────────────────────
	// handler|payloadJSON を送り、parse 済みの response object で resolve する。
	// bridge が使えない、onFailure 発火、response に { error } がある場合は
	// Error で reject する。
	function _query(handlerName, payloadObj)
	{
		if (!_requireBridge())
		{
			return Promise.reject(new Error('physics bridge unavailable'));
		}

		return new Promise(function(resolve, reject)
		{
			var payloadStr = JSON.stringify(payloadObj);
			global.cefQuery({
				request: handlerName + '|' + payloadStr,
				onSuccess: function(respStr)
				{
					var resp;
					try { resp = JSON.parse(respStr); }
					catch (e)
					{
						reject(new Error('physics: invalid JSON response from ' + handlerName));
						return;
					}
					if (resp && typeof resp.error === 'string')
					{
						reject(new Error('physics: ' + handlerName + ' error: ' + resp.error));
					}
					else
					{
						resolve(resp);
					}
				},
				onFailure: function(_code, msg)
				{
					reject(new Error('physics: ' + handlerName + ' failed: ' + msg));
				}
			});
		});
	}

	// ── event emitter ファクトリ ─────────────────────────────────────
	function _makeEmitter()
	{
		var _listeners = Object.create(null);

		function emit(name, detail)
		{
			var arr = _listeners[name];
			if (!arr || arr.length === 0) { return; }
			var copy = arr.slice();
			for (var i = 0; i < copy.length; ++i)
			{
				try { copy[i](detail); }
				catch (e)
				{
					// 1 つの不良 handler が frame loop を止めないよう握り潰す
				}
			}
		}

		function on(name, fn)
		{
			if (typeof name !== 'string' || typeof fn !== 'function')
			{
				throw new Error('mitiru.physics.on: (string, function) required');
			}
			if (!_listeners[name]) { _listeners[name] = []; }
			_listeners[name].push(fn);
			return function()
			{
				var arr = _listeners[name];
				if (!arr) { return; }
				var idx = arr.indexOf(fn);
				if (idx >= 0) { arr.splice(idx, 1); }
			};
		}

		return { emit: emit, on: on };
	}

	// ── World ファクトリ ─────────────────────────────────────────────
	function _makeWorld(worldId)
	{
		var _emitter    = _makeEmitter();
		var _rafId      = null;
		var _lastTs     = null;
		var _destroyed  = false;

		// ── poll tick ─────────────────────────────────────────────
		function _tick(ts)
		{
			if (_rafId === null) { return; }  // stop() が呼ばれた

			var dtMs = (_lastTs === null) ? 16.6 : (ts - _lastTs);
			_lastTs = ts;

			_query('physics.poll', { worldId: worldId, dtMs: dtMs })
				.then(function(resp)
				{
					if (_rafId === null) { return; }  // async 中に stop された

					var moves    = Array.isArray(resp.moves)    ? resp.moves    : [];
					var contacts = Array.isArray(resp.contacts) ? resp.contacts : [];

					if (moves.length > 0 || contacts.length === 0)
					{
						_emitter.emit('bodies:update', moves);
					}

					for (var i = 0; i < contacts.length; ++i)
					{
						_emitter.emit('contact', contacts[i]);
					}

					if (_rafId !== null)
					{
						_rafId = global.requestAnimationFrame(_tick);
					}
				})
				.catch(function(err)
				{
					_rafId = null;
					_lastTs = null;
					_emitter.emit('error', err);
				});
		}

		// ── public な world object ───────────────────────────────────
		var world = {};

		world.createCircle = function(opts)
		{
			return _query('physics.createCircle', {
				worldId:     worldId,
				x:           opts.x,
				y:           opts.y,
				r:           opts.r,
				density:     opts.density,
				restitution: opts.restitution,
				friction:    opts.friction,
				userData:    opts.userData !== undefined ? String(opts.userData) : ''
			}).then(function(resp) { return resp.bodyId; });
		};

		world.createStaticEdge = function(opts)
		{
			return _query('physics.createStaticEdge', {
				worldId:     worldId,
				x1:          opts.x1,
				y1:          opts.y1,
				x2:          opts.x2,
				y2:          opts.y2,
				friction:    opts.friction     !== undefined ? opts.friction     : 0.5,
				restitution: opts.restitution  !== undefined ? opts.restitution  : 0
			}).then(function(resp) { return resp.bodyId; });
		};

		world.destroyBody = function(bodyId)
		{
			return _query('physics.destroyBody', { worldId: worldId, bodyId: bodyId })
				.then(function() { return undefined; });
		};

		world.setLinearVelocity = function(bodyId, vx, vy)
		{
			return _query('physics.setLinearVelocity', {
				worldId: worldId,
				bodyId:  bodyId,
				vx:      vx,
				vy:      vy
			}).then(function() { return undefined; });
		};

		world.on = function(eventName, fn)
		{
			return _emitter.on(eventName, fn);
		};

		world.start = function()
		{
			if (_rafId !== null || _destroyed) { return; }
			_lastTs = null;
			_rafId  = global.requestAnimationFrame(_tick);
		};

		world.stop = function()
		{
			if (_rafId === null) { return; }
			global.cancelAnimationFrame(_rafId);
			_rafId  = null;
			_lastTs = null;
		};

		world.destroy = function()
		{
			world.stop();
			_destroyed = true;
			return _query('physics.destroyWorld', { worldId: worldId })
				.then(function() { return undefined; });
		};

		return world;
	}

	// ── public な mitiru.physics namespace ──────────────────────────
	var physics = mitiru.physics = Object.create(null);

	/**
	 * C++ 側に新しい physics world を作成する。
	 * @param {object} opts  { gravityX:number, gravityY:number }
	 * @returns {Promise<World>}
	 */
	physics.createWorld = function(opts)
	{
		return _query('physics.createWorld', {
			gravityX: opts.gravityX !== undefined ? opts.gravityX : 0,
			gravityY: opts.gravityY !== undefined ? opts.gravityY : 9.81
		}).then(function(resp)
		{
			return _makeWorld(resp.worldId);
		});
	};

})(typeof window !== 'undefined' ? window : globalThis);
