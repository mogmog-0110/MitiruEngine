/*!
 * mitiru_reflect_decorate.js：GameMemory reflect 欄の読み取り専用な追加装飾
 *
 * data-m-kv (mitiru_bind_tools.js) が描画した "Game memory" section の行を、
 * module/AutoReflect.hpp の MITIRU_ENUM / MITIRU_FIELD_RANGE / MITIRU_FIELD_UI_RANGE /
 * MITIRU_FIELD_GROUP の宣言に沿って select / スライダー / グループ折り畳みへ差し替える。
 * inspect.html と scene.html (game memory タブ) の両方が共有するスクリプト。
 * SCOPE の「inspector は観察専用」を守るため、生成する部品は全部 disabled 固定にする。
 *
 * FieldDescriptor.elemType の合成書式 (module/FieldAttr.hpp が書く) を読む。
 * "enum:Idle,Run,..." なら select、"range:min:max" なら値域をそのままスライダーへ、
 * "range:min:max;ui:a:b" なら ui の方をスライダーへ (値域はオラクル用で表示しない)、
 * "ui:a:b" 単体でもスライダーになる (値域は未宣言)。末尾の ";group:名前" は
 * どの書式にも付けられ、同じ名前の行をまとめて details へ折り畳む。
 */
(function (global) {
	'use strict';

	function parseMeta(m) {
		if (!m || typeof m.elemType !== 'string' || m.elemType === '') { return null; }
		let rest = m.elemType;
		let group = null;
		const gi = rest.indexOf(';group:');
		if (gi >= 0) { group = rest.slice(gi + 7); rest = rest.slice(0, gi); }
		else if (rest.indexOf('group:') === 0) { group = rest.slice(6); rest = ''; }

		let widget = null;
		if (rest.indexOf('enum:') === 0) {
			widget = { kind: 'enum', names: rest.slice(5).split(',') };
		} else if (rest.indexOf('range:') === 0) {
			const segs = rest.split(';');
			const rangeParts = segs[0].slice(6).split(':');
			const uiSeg = segs.find(function (s) { return s.indexOf('ui:') === 0; });
			const sliderParts = uiSeg ? uiSeg.slice(3).split(':') : rangeParts;
			widget = { kind: 'range', min: Number(sliderParts[0]), max: Number(sliderParts[1]) };
		} else if (rest.indexOf('ui:') === 0) {
			const p = rest.slice(3).split(':');
			widget = { kind: 'range', min: Number(p[0]), max: Number(p[1]) };
		}
		if (!widget && !group) { return null; }
		return { widget: widget, group: group };
	}

	// 数値やラベルを disabled な select / range input に差し替える (読み取り専用の見た目だけ変更)。
	function decorateRow(rowEl, value, meta) {
		const parsed = parseMeta(meta);
		if (!parsed) { return; }
		if (parsed.group) { rowEl.dataset.reflectGroup = parsed.group; }
		const w = parsed.widget;
		if (!w) { return; }
		const vEl = rowEl.querySelector('.v');
		if (!vEl) { return; }
		vEl.textContent = '';
		if (w.kind === 'enum') {
			const sel = document.createElement('select');
			sel.disabled = true;
			w.names.forEach(function (name, i) {
				const opt = document.createElement('option');
				opt.value = String(i); opt.textContent = name;
				if (i === (value | 0)) { opt.selected = true; }
				sel.appendChild(opt);
			});
			vEl.appendChild(sel);
		} else if (w.kind === 'range' && typeof value === 'number') {
			const input = document.createElement('input');
			input.type = 'range'; input.disabled = true; input.step = 'any';
			input.min = String(w.min); input.max = String(w.max); input.value = String(value);
			const label = document.createElement('span');
			label.textContent = ' ' + (Number.isInteger(value) ? value : value.toFixed(2));
			vEl.appendChild(input); vEl.appendChild(label);
		}
	}

	// group 指定済みの行を、初出順のまま details/summary に包んで畳む (再描画のたびに作り直す)。
	// :scope は非対応環境があるため (mitiru_bind.js 同様の理由)、直下の子を手動で走査する。
	function foldGroups(box) {
		Array.prototype.slice.call(box.children).forEach(function (child) {
			if (!child.classList.contains('reflect-group')) { return; }
			Array.prototype.slice.call(child.children).forEach(function (inner) {
				if (inner.tagName !== 'SUMMARY') { box.insertBefore(inner, child); }
			});
			box.removeChild(child);
		});
		const order = [];
		const rowsByGroup = {};
		Array.prototype.slice.call(box.children).forEach(function (row) {
			if (!row.classList.contains('row')) { return; }
			const g = row.dataset.reflectGroup;
			if (!g) { return; }
			if (!rowsByGroup[g]) { rowsByGroup[g] = []; order.push(g); }
			rowsByGroup[g].push(row);
		});
		order.forEach(function (g) {
			const rows = rowsByGroup[g];
			const details = document.createElement('details');
			details.className = 'reflect-group';
			const summary = document.createElement('summary');
			summary.textContent = g;
			details.appendChild(summary);
			box.insertBefore(details, rows[0]);
			rows.forEach(function (r) { details.appendChild(r); });
		});
	}

	// "Game memory" section だけを狙い撃ちして装飾する (他の kv section は触らない)。
	function decorateSection() {
		const gm = global.mitiru && typeof global.mitiru.getState === 'function'
			? (global.mitiru.getState('tool.snap') || {}).gameMemory : null;
		if (!gm || !gm.state || !gm.meta) { return; }
		document.querySelectorAll('.section-title').forEach(function (title) {
			if (title.textContent !== (gm.title || 'Game memory')) { return; }
			const box = title.nextElementSibling;
			if (!box) { return; }
			box.querySelectorAll('.row').forEach(function (row) {
				const kEl = row.querySelector('.k');
				const key = kEl && kEl.textContent;
				if (key && key in gm.state) { decorateRow(row, gm.state[key], gm.meta[key]); }
			});
			foldGroups(box);
		});
	}

	function init() {
		document.addEventListener('DOMContentLoaded', function () {
			if (global.mitiru && typeof global.mitiru.onStateChange === 'function') {
				global.mitiru.onStateChange('tool.snap', decorateSection);
			}
		});
	}

	global.mitiruReflectDecorate = { init: init, parseMeta: parseMeta };
})(window);
