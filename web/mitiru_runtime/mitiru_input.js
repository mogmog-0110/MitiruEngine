/*!
 * mitiru_input.js — unified keyboard / mouse / gamepad input abstraction (NF-03)
 *
 * Provides a single action-map driven API over keyboard, mouse, and Gamepad API.
 * The active device is auto-detected and prompt strings are device-contextual.
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.input.setActionMap(map)       Record<actionName, binding[]>
 *   mitiru.input.actionMap()             frozen snapshot of current map
 *   mitiru.input.setDeadzone(0..1)       gamepad stick deadzone (default 0.15)
 *   mitiru.input.button(action)          bool — currently held
 *   mitiru.input.pressed(action)         bool — true only on frame it went down
 *   mitiru.input.released(action)        bool — true only on frame it went up
 *   mitiru.input.axis(action)            -1..+1 (gamepad stick axes)
 *   mitiru.input.activeDevice()          'keyboard' | 'mouse' | 'gamepad'
 *   mitiru.input.promptFor(action)       first binding matching active device
 *   mitiru.input.start()                 begin polling + event listeners
 *   mitiru.input.stop()                  remove listeners + cancel RAF loop
 *   mitiru.input.rumble(ms, strong?, weak?)  attempt gamepad rumble
 *   mitiru.input.on(event, cb)           subscribe to events
 *   mitiru.input.off(event, cb)          unsubscribe
 *
 * ── Events ──────────────────────────────────────────────────────────────────
 *   'action:down'   { action, binding, device }
 *   'action:up'     { action, binding, device }
 *   'device:change' { from, to }
 *
 * ── Binding Syntax ──────────────────────────────────────────────────────────
 *   Keyboard: KeyboardEvent.key e.g. 'Enter' 'Space' 'ArrowUp'
 *   Mouse:    'Mouse.0' (left) | 'Mouse.1' (middle) | 'Mouse.2' (right)
 *   Gamepad:  'Gamepad.A/B/X/Y' 'Gamepad.LB/RB/LT/RT' 'Gamepad.Start/Back'
 *             'Gamepad.DPad.Up/Down/Left/Right'
 *             'Gamepad.LStick.Up/Down/Left/Right' (virtual buttons from axis)
 *   Axes:     'Gamepad.LStickX/Y' 'Gamepad.RStickX/Y'
 *
 * Implements spec: docs/feedback-from-engine/NF-03
 */
(function(global)
{
	'use strict';

	var mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.input) { return; }  // already loaded

	// Xbox button index map (standard mapping)
	// index: A=0 B=1 X=2 Y=3 LB=4 RB=5 LT=6 RT=7 Back=8 Start=9 DPad Up=12..Right=15
	var GAMEPAD_BUTTON_MAP = {
		'Gamepad.A': 0, 'Gamepad.B': 1, 'Gamepad.X': 2, 'Gamepad.Y': 3,
		'Gamepad.LB': 4, 'Gamepad.RB': 5, 'Gamepad.LT': 6, 'Gamepad.RT': 7,
		'Gamepad.Back': 8, 'Gamepad.Start': 9,
		'Gamepad.DPad.Up': 12, 'Gamepad.DPad.Down': 13,
		'Gamepad.DPad.Left': 14, 'Gamepad.DPad.Right': 15,
	};

	// Virtual stick buttons: [axisIndex, sign] — fires when axis*sign > deadzone
	var GAMEPAD_STICK_VIRTUAL = {
		'Gamepad.LStick.Up': [1, -1], 'Gamepad.LStick.Down': [1, 1],
		'Gamepad.LStick.Left': [0, -1], 'Gamepad.LStick.Right': [0, 1],
		'Gamepad.RStick.Up': [3, -1], 'Gamepad.RStick.Down': [3, 1],
		'Gamepad.RStick.Left': [2, -1], 'Gamepad.RStick.Right': [2, 1],
	};

	// Named axis bindings for .axis()
	var GAMEPAD_AXIS_MAP = {
		'Gamepad.LStickX': 0, 'Gamepad.LStickY': 1,
		'Gamepad.RStickX': 2, 'Gamepad.RStickY': 3,
	};

	// ── classify a binding string by device ──────────────────
	function _deviceOf(binding)
	{
		if (typeof binding !== 'string') { return 'keyboard'; }
		if (binding.indexOf('Gamepad.') === 0) { return 'gamepad'; }
		if (binding.indexOf('Mouse.')   === 0) { return 'mouse'; }
		return 'keyboard';
	}

	// ── internal state ────────────────────────────────────────
	var _actionMap    = Object.create(null);   // { action: [binding, ...] }
	var _frozenMap    = Object.create(null);   // deep-frozen snapshot
	var _deadzone     = 0.15;
	var _activeDevice = 'keyboard';

	// Per-action held state: { action: bool }
	var _held     = Object.create(null);
	// One-shot flags reset each frame
	var _pressed  = Object.create(null);
	var _released = Object.create(null);

	// Raw held bindings (not actions) — for multi-binding tracking
	// { binding: bool }
	var _bindingHeld = Object.create(null);

	// Previous gamepad state for edge detection
	// { padIndex: { buttons: [bool,...], axes: [float,...] } }
	var _padPrevState = Object.create(null);

	var _running  = false;
	var _rafId    = null;
	var _listeners = Object.create(null);
	var _warnedNoRumble = false;

	// ── event emitter ─────────────────────────────────────────
	function _emit(name, detail)
	{
		var arr = _listeners[name];
		if (!arr || arr.length === 0) { return; }
		var copy = arr.slice();
		for (var i = 0; i < copy.length; ++i)
		{
			try { copy[i](detail); }
			catch (e) { console.error('[mitiru.input] event handler threw (' + name + '):', e); }
		}
	}

	// ── device change helper ──────────────────────────────────
	function _setDevice(dev)
	{
		if (dev === _activeDevice) { return; }
		var prev = _activeDevice;
		_activeDevice = dev;
		_emit('device:change', { from: prev, to: dev });
	}

	// ── action edge detection: binding went down/up ───────────
	// Called whenever a raw binding's held state changes.
	function _onBindingDown(binding, device)
	{
		if (_bindingHeld[binding]) { return; }  // already held
		_bindingHeld[binding] = true;

		// Find every action that includes this binding.
		var actions = Object.keys(_actionMap);
		for (var i = 0; i < actions.length; ++i)
		{
			var action   = actions[i];
			var bindings = _actionMap[action];
			if (bindings.indexOf(binding) < 0) { continue; }

			// Was the action already held by another binding?
			var wasHeld = _isActionHeld(action, binding);
			if (!wasHeld)
			{
				_held[action]    = true;
				_pressed[action] = true;
				_emit('action:down', { action: action, binding: binding, device: device });
			}
		}
	}

	function _onBindingUp(binding, device)
	{
		if (!_bindingHeld[binding]) { return; }  // already released
		_bindingHeld[binding] = false;

		var actions = Object.keys(_actionMap);
		for (var i = 0; i < actions.length; ++i)
		{
			var action   = actions[i];
			var bindings = _actionMap[action];
			if (bindings.indexOf(binding) < 0) { continue; }

			// Is any other binding for this action still held?
			var stillHeld = _isActionHeld(action, null);
			if (!stillHeld)
			{
				_held[action]     = false;
				_released[action] = true;
				_emit('action:up', { action: action, binding: binding, device: device });
			}
		}
	}

	// Returns true if any binding for the action is held, ignoring `exceptBinding`.
	function _isActionHeld(action, exceptBinding)
	{
		var bindings = _actionMap[action] || [];
		for (var i = 0; i < bindings.length; ++i)
		{
			if (bindings[i] === exceptBinding) { continue; }
			if (_bindingHeld[bindings[i]]) { return true; }
		}
		return false;
	}

	// ── keyboard handlers ─────────────────────────────────────
	function _onKeydown(e)
	{
		_setDevice('keyboard');
		_onBindingDown(e.key, 'keyboard');
	}

	function _onKeyup(e)
	{
		_onBindingUp(e.key, 'keyboard');
	}

	// ── mouse handlers ────────────────────────────────────────
	function _onMousedown(e)
	{
		_setDevice('mouse');
		_onBindingDown('Mouse.' + e.button, 'mouse');
	}

	function _onMouseup(e)
	{
		_onBindingUp('Mouse.' + e.button, 'mouse');
	}

	// ── RAF gamepad polling ───────────────────────────────────
	function _rafLoop()
	{
		if (!_running) { return; }

		// Clear one-shot flags from previous frame.
		_pressed  = Object.create(null);
		_released = Object.create(null);

		_pollGamepads();

		_rafId = global.requestAnimationFrame(_rafLoop);
	}

	function _pollGamepads()
	{
		var pads;
		try { pads = navigator.getGamepads ? navigator.getGamepads() : []; }
		catch (_e) { pads = []; }

		for (var pi = 0; pi < pads.length; ++pi)
		{
			var pad = pads[pi];
			if (!pad || !pad.connected) { continue; }

			var prev = _padPrevState[pad.index] || { buttons: [], axes: [] };

			// Check named buttons.
			var btnKeys = Object.keys(GAMEPAD_BUTTON_MAP);
			for (var bi = 0; bi < btnKeys.length; ++bi)
			{
				var name = btnKeys[bi];
				var idx  = GAMEPAD_BUTTON_MAP[name];
				var btn  = pad.buttons[idx];
				if (!btn) { continue; }

				var nowPressed  = btn.pressed;
				var wasPressed  = !!prev.buttons[idx];

				if (nowPressed && !wasPressed)
				{
					_setDevice('gamepad');
					_onBindingDown(name, 'gamepad');
				}
				else if (!nowPressed && wasPressed)
				{
					_onBindingUp(name, 'gamepad');
				}
			}

			// Check virtual stick buttons.
			var stickKeys = Object.keys(GAMEPAD_STICK_VIRTUAL);
			for (var si = 0; si < stickKeys.length; ++si)
			{
				var sname  = stickKeys[si];
				var spec   = GAMEPAD_STICK_VIRTUAL[sname];
				var axisV  = pad.axes[spec[0]] || 0;
				var dir    = spec[1];

				var nowVirt  = (axisV * dir) > _deadzone;
				var wasVirt  = !!(prev.axes[spec[0]] !== undefined
				              && (prev.axes[spec[0]] * dir) > _deadzone);

				if (nowVirt && !wasVirt)
				{
					_setDevice('gamepad');
					_onBindingDown(sname, 'gamepad');
				}
				else if (!nowVirt && wasVirt)
				{
					_onBindingUp(sname, 'gamepad');
				}
			}

			// Detect any axis above deadzone → switch active device.
			for (var ai = 0; ai < pad.axes.length; ++ai)
			{
				if (Math.abs(pad.axes[ai]) > _deadzone)
				{
					_setDevice('gamepad');
					break;
				}
			}

			// Save snapshot (immutable arrays).
			var btnSnapshot  = [];
			for (var bsi = 0; bsi < pad.buttons.length; ++bsi)
			{
				btnSnapshot[bsi] = pad.buttons[bsi] ? pad.buttons[bsi].pressed : false;
			}
			var axisSnapshot = pad.axes.slice ? pad.axes.slice() : Array.prototype.slice.call(pad.axes);
			_padPrevState[pad.index] = { buttons: btnSnapshot, axes: axisSnapshot };
		}
	}

	// ── reset all held/pressed/released state ─────────────────
	function _resetState()
	{
		_held        = Object.create(null);
		_pressed     = Object.create(null);
		_released    = Object.create(null);
		_bindingHeld = Object.create(null);
		_padPrevState = Object.create(null);
	}

	// ── public API ────────────────────────────────────────────
	var input = mitiru.input = Object.create(null);

	// ── setActionMap ──────────────────────────────────────────
	input.setActionMap = function(map)
	{
		if (!map || typeof map !== 'object')
		{
			throw new Error('mitiru.input.setActionMap: expected object');
		}
		var newMap = Object.create(null);
		var keys   = Object.keys(map);
		for (var i = 0; i < keys.length; ++i)
		{
			var k = keys[i];
			var v = map[k];
			newMap[k] = Array.isArray(v) ? v.slice() : [];
		}
		_actionMap  = newMap;

		// Build frozen snapshot (deep freeze each binding array + outer).
		var snap = Object.create(null);
		var skeys = Object.keys(_actionMap);
		for (var si = 0; si < skeys.length; ++si)
		{
			snap[skeys[si]] = Object.freeze(_actionMap[skeys[si]].slice());
		}
		_frozenMap = Object.freeze(snap);

		_resetState();
	};

	// ── actionMap ─────────────────────────────────────────────
	input.actionMap = function()
	{
		return _frozenMap;
	};

	// ── setDeadzone ───────────────────────────────────────────
	input.setDeadzone = function(v)
	{
		_deadzone = (v < 0 ? 0 : v > 1 ? 1 : v);
	};

	// ── button ────────────────────────────────────────────────
	input.button = function(action)
	{
		if (!_actionMap[action]) { return false; }
		return !!_held[action];
	};

	// ── pressed ───────────────────────────────────────────────
	input.pressed = function(action)
	{
		if (!_actionMap[action]) { return false; }
		return !!_pressed[action];
	};

	// ── released ──────────────────────────────────────────────
	input.released = function(action)
	{
		if (!_actionMap[action]) { return false; }
		return !!_released[action];
	};

	// ── axis ─────────────────────────────────────────────────
	input.axis = function(action)
	{
		if (!_actionMap[action]) { return 0; }
		var bindings = _actionMap[action];
		var pads;
		try { pads = navigator.getGamepads ? navigator.getGamepads() : []; }
		catch (_e) { pads = []; }

		for (var bi = 0; bi < bindings.length; ++bi)
		{
			var binding = bindings[bi];
			var axisIdx = GAMEPAD_AXIS_MAP[binding];
			if (axisIdx === undefined) { continue; }
			for (var pi = 0; pi < pads.length; ++pi)
			{
				var pad = pads[pi];
				if (!pad || !pad.connected) { continue; }
				var raw = pad.axes[axisIdx] || 0;
				return Math.abs(raw) < _deadzone ? 0 : raw;
			}
		}
		return 0;
	};

	// ── activeDevice ─────────────────────────────────────────
	input.activeDevice = function()
	{
		return _activeDevice;
	};

	// ── promptFor ────────────────────────────────────────────
	input.promptFor = function(action)
	{
		var bindings = _actionMap[action];
		if (!bindings || bindings.length === 0) { return null; }
		var dev = _activeDevice;
		for (var i = 0; i < bindings.length; ++i)
		{
			if (_deviceOf(bindings[i]) === dev) { return bindings[i]; }
		}
		// Fallback: return first binding.
		return bindings[0];
	};

	// ── on / off ──────────────────────────────────────────────
	input.on = function(event, cb)
	{
		if (typeof event !== 'string' || typeof cb !== 'function')
		{
			throw new Error('mitiru.input.on: (string, function) required');
		}
		if (!_listeners[event]) { _listeners[event] = []; }
		_listeners[event].push(cb);
		return function() { input.off(event, cb); };
	};

	input.off = function(event, cb)
	{
		var arr = _listeners[event];
		if (!arr) { return; }
		var i = arr.indexOf(cb);
		if (i >= 0) { arr.splice(i, 1); }
	};

	// ── start ────────────────────────────────────────────────
	input.start = function()
	{
		if (_running) { return; }  // idempotent
		_running = true;
		_resetState();
		document.addEventListener('keydown',   _onKeydown);
		document.addEventListener('keyup',     _onKeyup);
		document.addEventListener('mousedown', _onMousedown);
		document.addEventListener('mouseup',   _onMouseup);
		_rafId = global.requestAnimationFrame(_rafLoop);
	};

	// ── stop ─────────────────────────────────────────────────
	input.stop = function()
	{
		if (!_running) { return; }
		_running = false;
		document.removeEventListener('keydown',   _onKeydown);
		document.removeEventListener('keyup',     _onKeyup);
		document.removeEventListener('mousedown', _onMousedown);
		document.removeEventListener('mouseup',   _onMouseup);
		if (_rafId !== null)
		{
			global.cancelAnimationFrame(_rafId);
			_rafId = null;
		}
		_resetState();
	};

	// ── rumble ────────────────────────────────────────────────
	input.rumble = function(ms, strong, weak)
	{
		var pads;
		try { pads = navigator.getGamepads ? navigator.getGamepads() : []; }
		catch (_e) { pads = []; }

		var found = false;
		for (var pi = 0; pi < pads.length; ++pi)
		{
			var pad = pads[pi];
			if (!pad || !pad.connected) { continue; }
			if (pad.vibrationActuator && typeof pad.vibrationActuator.playEffect === 'function')
			{
				pad.vibrationActuator.playEffect('dual-rumble', {
					startDelay:     0,
					duration:       ms || 100,
					strongMagnitude: strong !== undefined ? strong : 0.5,
					weakMagnitude:   weak   !== undefined ? weak   : 0.5,
				});
				found = true;
				break;
			}
		}

		if (!found && !_warnedNoRumble)
		{
			_warnedNoRumble = true;
			console.warn('[mitiru.input] rumble(): no gamepad with vibrationActuator available');
		}
	};

})(typeof window !== 'undefined' ? window : globalThis);
