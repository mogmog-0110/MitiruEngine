/*!
 * mitiru_telemetry.js — tools/generate_bridge.py による自動生成
 * Source: bridges/telemetry.bridge.json
 * 再生成:
 *   python tools/generate_bridge.py bridges/telemetry.bridge.json
 *
 * 軽量な metrics / event-counter bridge。bridge codegen pipeline のデモ。consumer プロジェクトでは削除して問題ない。
 *
 * Attaches to: window.mitiru.telemetry
 * Dispatch: mitiru.dispatch('telemetry.<method>', payload)
 *
 * JS fallback を override する場合 (vitest や非 CEF build 等):
 *   mitiru.telemetry._fallback.<method> = function(...args) { ... };
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.telemetry) { return; }  // ロード済み

	const _fallback = {
		emit: function(event, value) { throw new Error('telemetry.emit: no JS fallback configured (override mitiru.telemetry._fallback.emit)'); },
		snapshot: function() { throw new Error('telemetry.snapshot: no JS fallback configured (override mitiru.telemetry._fallback.snapshot)'); },
		reset: function() { throw new Error('telemetry.reset: no JS fallback configured (override mitiru.telemetry._fallback.reset)'); }
	};

	const api = {};
	api._fallback = _fallback;

	api.emit = async function(event, value)
	{
		if (value === undefined) { value = 1; }
		if (typeof event !== 'string') { throw new TypeError('telemetry.emit: event must be string'); }
		if (value !== undefined && (typeof value !== 'number' || !Number.isFinite(value))) { throw new TypeError('telemetry.emit: value must be number'); }
		if (mitiru.dispatch)
		{
			try { return await mitiru.dispatch('telemetry.emit', { event, value }); }
			catch (_e) { /* JS fallback に fall through */ }
		}
		return _fallback.emit(event, value);
	};

	api.snapshot = async function()
	{
		if (mitiru.dispatch)
		{
			try { return await mitiru.dispatch('telemetry.snapshot', {}); }
			catch (_e) { /* JS fallback に fall through */ }
		}
		return _fallback.snapshot();
	};

	api.reset = async function()
	{
		if (mitiru.dispatch)
		{
			try { return await mitiru.dispatch('telemetry.reset', {}); }
			catch (_e) { /* JS fallback に fall through */ }
		}
		return _fallback.reset();
	};

	mitiru.telemetry = api;
})(typeof window !== 'undefined' ? window : globalThis);
