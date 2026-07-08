/*!
 * mitiru_save.js — ゲーム全状態の save slot (F-11)
 *
 * 10 個の番号付き save slot。各 slot が version 付き・timestamp 付きの
 * game-state blob を保持する。localStorage (dev / web の fallback) と
 * CEF file-bridge backend (production、実質的なサイズ上限なし) をサポート。
 *
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.save.SCHEMA_VERSION            number — 保存形式変更時に上げる
 *   mitiru.save.MAX_SLOTS                 number — 常に 10
 *   mitiru.save.write(slot, data, meta?)  Promise<void>
 *   mitiru.save.read(slot)               Promise<{data,meta}|null>
 *   mitiru.save.list()                   Promise<Array<{slot,exists,meta}>>
 *   mitiru.save.delete(slot)             Promise<void>
 *   mitiru.save.registerMigration(from, to, fn)
 *   mitiru.save.migrateAll()             Promise<void>  — 使用中の全 slot を migrate
 *   mitiru.save.backend()                'cef' | 'localStorage'
 *   mitiru.save.sizeBytes()              Promise<number>  — CEF では -1
 *   mitiru.save.exportSlot(slot)         Promise<Blob>   — NF-06
 *   mitiru.save.exportAll()              Promise<Blob>   — NF-06
 *   mitiru.save.importSlot(blob, slot)   Promise<void>   — NF-06
 *   mitiru.save.importAll(blob)          Promise<{imported:number[],skipped:number[]}> — NF-06
 *   mitiru.save.triggerDownload(blob, filename?)  void  — NF-06
 *   mitiru.save.currentVersion()         number          — NF-06
 *
 * ── Storage key (localStorage) ─────────────────────────────────────────────
 *   mitiru.save.slot.<N>.data      commit 済み data blob (JSON)
 *   mitiru.save.slot.<N>.meta      commit 済み meta blob (JSON)
 *   mitiru.save.slot.<N>.staging   atomic staging — write 中のみ存在
 *
 * ── CEF handler namespace ────────────────────────────────────────────────────
 *   save.write   {slot, payload: <full blob JSON string>}
 *   save.read    {slot}
 *   save.list    {}
 *   save.delete  {slot}
 *
 *   C++ 実装: include/mitiru/cef/SaveStore.hpp (StateStore に上記 4 handler を
 *   登録し atomic file I/O へマップ)。handler 未登録なら mitiru.dispatch が
 *   reject し、本 module は localStorage へ fallback する。
 *
 * ── 正典宣言 (docs/adr/0022) ─────────────────────────────────────────────────
 *   本ファイルが save bridge の正典。bridges/_generated/save.generated.js は
 *   codegen 忠実度検証専用の fixture で、runtime 使用禁止。
 *
 * ── Supersedes ───────────────────────────────────────────────────────────────
 *   mitiru.state.save / .load / .listSlots (F-03) は per-key の一時的な state
 *   永続化として残る。game 全状態の blob (save 画面 flow) にはこの module を
 *   代わりに使う。
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.save) { return; }  // 読み込み済み

	// ── 定数 ─────────────────────────────────────────────────
	const SCHEMA_VERSION = 1;
	const MAX_SLOTS      = 10;
	const KEY_PREFIX     = 'mitiru.save.slot.';

	// ── migration registry ────────────────────────────────────────
	// 各 entry: { from: number, to: number, fn: (oldData) => newData }
	const _migrations = [];

	// ── helpers ───────────────────────────────────────────────────
	function _validateSlot(slot)
	{
		if (typeof slot !== 'number' || !Number.isInteger(slot)
		    || slot < 0 || slot >= MAX_SLOTS)
		{
			throw new RangeError(
				'mitiru.save: slot must be an integer in [0, ' + (MAX_SLOTS - 1) + '], got: ' + slot
			);
		}
	}

	function _dataKey(slot)    { return KEY_PREFIX + slot + '.data'; }
	function _metaKey(slot)    { return KEY_PREFIX + slot + '.meta'; }
	function _stagingKey(slot) { return KEY_PREFIX + slot + '.staging'; }

	function _now() { return Date.now(); }

	// ── migration chain ───────────────────────────────────────────
	function _applyMigrations(blob)
	{
		let current = blob;
		let version = (typeof current.version === 'number') ? current.version : 0;

		// chain が順番に適用されるよう、migration を `from` 昇順に sort する。
		const sorted = _migrations.slice().sort(function(a, b) { return a.from - b.from; });

		for (let i = 0; i < sorted.length; ++i)
		{
			const m = sorted[i];
			if (version === m.from)
			{
				try
				{
					const migrated = m.fn(current.data);
					current = Object.assign({}, current, {
						data:    migrated,
						version: m.to,
					});
					version = m.to;
				}
				catch (e)
				{
					throw new Error(
						'mitiru.save: migration ' + m.from + '→' + m.to + ' threw: ' + e.message
					);
				}
			}
		}
		return current;
	}

	// ── backend 検出 ─────────────────────────────────────────
	function _isCef()
	{
		return typeof global.cefQuery === 'function';
	}

	// ── localStorage fallback 付き CEF dispatch ───────────────────
	// Promise を返す。C++ handler が reject した場合 (未登録) は、透過的に
	// localStorage 実装へ fallback する。
	function _cefDispatch(action, args)
	{
		if (!_isCef() || typeof mitiru.dispatch !== 'function')
		{
			return Promise.reject(new Error('cef unavailable'));
		}
		return mitiru.dispatch(action, args)
			.catch(function(err)
			{
				// C++ 側に handler が単に未登録なだけなら error message に
				// "unknown action" が含まれる。それを bubble させず localStorage
				// fallback を使う signal として扱う。
				if (err && typeof err.message === 'string'
				    && err.message.indexOf('unknown action') !== -1)
				{
					return Promise.reject(new Error('cef-handler-missing'));
				}
				return Promise.reject(err);
			});
	}

	// ── localStorage 実装 ───────────────────────────────

	function _lsWrite(slot, data, meta)
	{
		const timestamp = _now();
		const blob = {
			version:         SCHEMA_VERSION,
			timestamp:       meta.timestamp  || timestamp,
			playtimeMs:      meta.playtimeMs || 0,
			title:           meta.title            || '',
			description:     meta.description      || '',
			thumbnailDataUrl: meta.thumbnailDataUrl || '',
			data:            data,
		};
		const blobStr = JSON.stringify(blob);
		const metaObj = {
			version:         blob.version,
			timestamp:       blob.timestamp,
			playtimeMs:      blob.playtimeMs,
			title:           blob.title,
			description:     blob.description,
			thumbnailDataUrl: blob.thumbnailDataUrl,
		};

		// Atomic staging: まず staging を書き、commit し、最後に掃除する。
		try
		{
			global.localStorage.setItem(_stagingKey(slot), blobStr);
			global.localStorage.setItem(_dataKey(slot), blobStr);
			global.localStorage.setItem(_metaKey(slot), JSON.stringify(metaObj));
			global.localStorage.removeItem(_stagingKey(slot));
		}
		catch (e)
		{
			// quota 超過等 — recovery 用に staging key を残す。
			throw new Error('mitiru.save: write failed (slot ' + slot + '): ' + e.message);
		}
		return Promise.resolve();
	}

	function _lsRead(slot)
	{
		const dataStr    = global.localStorage.getItem(_dataKey(slot));
		const stagingStr = global.localStorage.getItem(_stagingKey(slot));

		// 破損 recovery: staging があるが commit 済み data が無い (または
		// 両者が異なる) 場合、staging を使い warning を log する。
		let raw = null;
		if (stagingStr && (!dataStr || dataStr !== stagingStr))
		{
			console.warn(
				'[mitiru.save] slot ' + slot + ': staging key found — '
				+ 'recovering from interrupted write.'
			);
			raw = stagingStr;
		}
		else if (dataStr)
		{
			raw = dataStr;
		}

		if (!raw) { return Promise.resolve(null); }

		let blob;
		try { blob = JSON.parse(raw); }
		catch (e)
		{
			console.warn('[mitiru.save] slot ' + slot + ': corrupt JSON — treating as empty.');
			return Promise.resolve(null);
		}

		blob = _applyMigrations(blob);

		const meta = {
			version:         blob.version,
			timestamp:       blob.timestamp,
			playtimeMs:      blob.playtimeMs,
			title:           blob.title,
			description:     blob.description,
			thumbnailDataUrl: blob.thumbnailDataUrl,
		};
		return Promise.resolve({ data: blob.data, meta: meta });
	}

	function _lsList()
	{
		const result = [];
		for (let i = 0; i < MAX_SLOTS; ++i)
		{
			const metaStr = global.localStorage.getItem(_metaKey(i));
			if (metaStr)
			{
				let metaObj;
				try { metaObj = JSON.parse(metaStr); }
				catch (_e) { metaObj = null; }
				result.push({ slot: i, exists: true,  meta: metaObj });
			}
			else
			{
				result.push({ slot: i, exists: false, meta: null });
			}
		}
		return Promise.resolve(result);
	}

	function _lsDelete(slot)
	{
		global.localStorage.removeItem(_dataKey(slot));
		global.localStorage.removeItem(_metaKey(slot));
		global.localStorage.removeItem(_stagingKey(slot));
		return Promise.resolve();
	}

	function _lsSizeBytes()
	{
		let total = 0;
		for (let i = 0; i < MAX_SLOTS; ++i)
		{
			const d = global.localStorage.getItem(_dataKey(i));
			const m = global.localStorage.getItem(_metaKey(i));
			if (d) { total += d.length * 2; }  // UTF-16: 1 文字 2 bytes
			if (m) { total += m.length * 2; }
		}
		return Promise.resolve(total);
	}

	// ── CEF 実装 (登録済み C++ handler に委譲) ────

	function _cefWrite(slot, data, meta)
	{
		const timestamp = _now();
		const blob = {
			version:         SCHEMA_VERSION,
			timestamp:       meta.timestamp        || timestamp,
			playtimeMs:      meta.playtimeMs       || 0,
			title:           meta.title            || '',
			description:     meta.description      || '',
			thumbnailDataUrl: meta.thumbnailDataUrl || '',
			data:            data,
		};
		return _cefDispatch('save.write', { slot: slot, payload: JSON.stringify(blob) })
			.then(function() { return undefined; });
	}

	function _cefRead(slot)
	{
		return _cefDispatch('save.read', { slot: slot })
			.then(function(resp)
			{
				if (!resp) { return null; }
				// resp は C++ handler の登録方法次第で、parse 済み object か
				// JSON string のどちらかになりうる。
				let blob = (typeof resp === 'string') ? JSON.parse(resp) : resp;
				blob = _applyMigrations(blob);
				const meta = {
					version:         blob.version,
					timestamp:       blob.timestamp,
					playtimeMs:      blob.playtimeMs,
					title:           blob.title,
					description:     blob.description,
					thumbnailDataUrl: blob.thumbnailDataUrl,
				};
				return { data: blob.data, meta: meta };
			});
	}

	function _cefList()
	{
		return _cefDispatch('save.list', {})
			.then(function(resp)
			{
				if (Array.isArray(resp)) { return resp; }
				if (typeof resp === 'string') { return JSON.parse(resp); }
				return [];
			});
	}

	function _cefDelete(slot)
	{
		return _cefDispatch('save.delete', { slot: slot })
			.then(function() { return undefined; });
	}

	// ── fallback 付き wrapper ────────────────────────────────────
	// まず CEF を試す; error ("unknown action" / cef 不可) 時は localStorage を使う。

	function _withFallback(cefFn, lsFn)
	{
		if (!_isCef() || typeof mitiru.dispatch !== 'function') { return lsFn(); }
		return cefFn().catch(function(err)
		{
			if (err && err.message === 'cef-handler-missing')
			{
				return lsFn();
			}
			return Promise.reject(err);
		});
	}

	// ── public API ─────────────────────────────────────────────────
	const save = mitiru.save = {};

	save.SCHEMA_VERSION = SCHEMA_VERSION;
	save.MAX_SLOTS      = MAX_SLOTS;

	/**
	 * ゲームの全状態を slot に書き込む。
	 * @param {number}  slot  0 .. MAX_SLOTS-1
	 * @param {object}  data  JSON 直列化可能な game state
	 * @param {object} [meta] {title?, description?, thumbnailDataUrl?, playtimeMs?, timestamp?}
	 * @returns {Promise<void>}
	 */
	save.write = function(slot, data, meta)
	{
		_validateSlot(slot);
		const m = (meta && typeof meta === 'object') ? meta : {};
		return _withFallback(
			function() { return _cefWrite(slot, data, m); },
			function() { return _lsWrite(slot, data, m); }
		);
	};

	/**
	 * slot を読む。
	 * @param {number} slot
	 * @returns {Promise<{data:object,meta:object}|null>}  slot が空なら null
	 */
	save.read = function(slot)
	{
		_validateSlot(slot);
		return _withFallback(
			function() { return _cefRead(slot); },
			function() { return _lsRead(slot); }
		);
	};

	/**
	 * 全 slot を列挙する。
	 * @returns {Promise<Array<{slot:number,exists:boolean,meta:object|null}>>}
	 *   常にちょうど MAX_SLOTS 個の entry。
	 */
	save.list = function()
	{
		return _withFallback(
			function() { return _cefList(); },
			function() { return _lsList(); }
		);
	};

	/**
	 * slot を削除する。slot が既に空なら no-op。
	 * @param {number} slot
	 * @returns {Promise<void>}
	 */
	save.delete = function(slot)
	{
		_validateSlot(slot);
		return _withFallback(
			function() { return _cefDelete(slot); },
			function() { return _lsDelete(slot); }
		);
	};

	/**
	 * `fromVersion` から `toVersion` への migration を登録する。
	 * 保存 version < SCHEMA_VERSION のとき read 時に `from` 昇順で適用される。
	 * @param {number}   fromVersion
	 * @param {number}   toVersion
	 * @param {function} migrate   (oldData: object) => newData: object
	 */
	save.registerMigration = function(fromVersion, toVersion, migrate)
	{
		if (typeof fromVersion !== 'number' || typeof toVersion !== 'number')
		{
			throw new TypeError('mitiru.save.registerMigration: versions must be numbers');
		}
		if (typeof migrate !== 'function')
		{
			throw new TypeError('mitiru.save.registerMigration: migrate must be a function');
		}
		_migrations.push({ from: fromVersion, to: toVersion, fn: migrate });
	};

	/**
	 * 登録済みの全 migration を、使用中の全 slot に適用する。
	 * game 起動時の update 後 migration sweep に有用。
	 * @returns {Promise<void>}
	 */
	save.migrateAll = async function()
	{
		for (let i = 0; i < MAX_SLOTS; ++i)
		{
			const result = await save.read(i);
			if (result !== null)
			{
				await save.write(i, result.data, result.meta);
			}
		}
	};

	/**
	 * 現在 active な storage backend を調べる。
	 * @returns {'cef'|'localStorage'}
	 */
	save.backend = function()
	{
		return _isCef() ? 'cef' : 'localStorage';
	};

	/**
	 * localStorage 上で save data が使う総 byte 数。
	 * CEF backend 上では -1 を返す (disk 使用量は JS から問い合わせ不可)。
	 * @returns {Promise<number>}
	 */
	save.sizeBytes = async function()
	{
		if (_isCef()) { return -1; }
		return _lsSizeBytes();
	};

	// ── NF-06: export / import / download ─────────────────────────

	/**
	 * migration を適用せず localStorage から生の保存 blob を読む。
	 * parse 済み blob object を返す、slot が空 / 破損なら null。
	 * exportSlot/exportAll が使い、export した bundle に実際の保存形を持たせる
	 * — _applyMigrations は export 時ではなく importSlot 時に走る。
	 *
	 * @param {number} slot
	 * @returns {object|null}
	 */
	function _readLocalStorageBlob(slot)
	{
		const dataStr    = global.localStorage.getItem(_dataKey(slot));
		const stagingStr = global.localStorage.getItem(_stagingKey(slot));

		let raw = null;
		if (stagingStr && (!dataStr || dataStr !== stagingStr))
		{
			raw = stagingStr;
		}
		else if (dataStr)
		{
			raw = dataStr;
		}

		if (!raw) { return null; }

		try   { return JSON.parse(raw); }
		catch (_e) { return null; }
	}

	/**
	 * 単一の save slot を可搬な Blob として export する。
	 *
	 * Bundle 形式 (JSON, application/octet-stream):
	 * {
	 *   engineVersion: number,   // export 時の SCHEMA_VERSION
	 *   slots: {
	 *     "<N>": {               // localStorage から取った生の保存 blob そのまま
	 *       version:          number,
	 *       timestamp:        number,
	 *       playtimeMs:       number,
	 *       title:            string,
	 *       description:      string,
	 *       thumbnailDataUrl: string,
	 *       data:             object,
	 *     }
	 *   }
	 * }
	 *
	 * 生 blob は migration を走らせずに export する。これにより受け取り側で
	 * importSlot 時に _applyMigrations を実行できる。
	 *
	 * @param {number} slot  0 .. MAX_SLOTS-1
	 * @returns {Promise<Blob>}  MIME: application/octet-stream
	 */
	save.exportSlot = function(slot)
	{
		_validateSlot(slot);
		return Promise.resolve().then(function()
		{
			const raw = _readLocalStorageBlob(slot);
			const bundle = {
				engineVersion: SCHEMA_VERSION,
				slots:         {},
			};
			if (raw !== null) { bundle.slots[String(slot)] = raw; }
			return new Blob([JSON.stringify(bundle)], { type: 'application/octet-stream' });
		});
	};

	/**
	 * 使用中の全 save slot を 1 つの可搬な Blob として export する。
	 *
	 * Bundle 形式は exportSlot と同じだが `slots` は 0..MAX_SLOTS 個の entry を
	 * 持ちうる。空の slot は完全に省略する。
	 *
	 * @returns {Promise<Blob>}  MIME: application/octet-stream
	 */
	save.exportAll = function()
	{
		return Promise.resolve().then(function()
		{
			const bundle = {
				engineVersion: SCHEMA_VERSION,
				slots:         {},
			};
			for (let i = 0; i < MAX_SLOTS; ++i)
			{
				const raw = _readLocalStorageBlob(i);
				if (raw !== null) { bundle.slots[String(i)] = raw; }
			}
			return new Blob([JSON.stringify(bundle)], { type: 'application/octet-stream' });
		});
	};

	/**
	 * bundle Blob を特定の slot に import し、既存 data を上書きする。
	 *
	 * 一致する slot の保存 blob は書き込み前に _applyMigrations を通すため、
	 * import 後の slot は常に現在の SCHEMA_VERSION になる。
	 *
	 * 不正な slot では同期的に RangeError を throw する (write() と同じ挙動)。
	 * bundle.engineVersion > SCHEMA_VERSION なら reject する。
	 * bundle にその slot の data が無い場合は void で resolve する。
	 *
	 * @param {Blob}   blob
	 * @param {number} slot  0 .. MAX_SLOTS-1
	 * @returns {Promise<void>}
	 */
	save.importSlot = function(blob, slot)
	{
		// 同期的に validate — write() が async 処理に入る前の最初の文で
		// _validateSlot を呼ぶのと同じ挙動。
		_validateSlot(slot);
		return blob.text().then(function(text)
		{
			let bundle;
			try   { bundle = JSON.parse(text); }
			catch (e) { throw new Error('mitiru.save.importSlot: invalid bundle JSON: ' + e.message); }

			if (typeof bundle.engineVersion !== 'number')
			{
				throw new Error('mitiru.save.importSlot: bundle missing engineVersion');
			}
			if (bundle.engineVersion > SCHEMA_VERSION)
			{
				throw new Error(
					'mitiru.save.importSlot: bundle engineVersion ' + bundle.engineVersion
					+ ' is newer than runtime SCHEMA_VERSION ' + SCHEMA_VERSION
					+ '; cannot import'
				);
			}
			if (!bundle.slots || typeof bundle.slots !== 'object')
			{
				throw new Error('mitiru.save.importSlot: bundle missing slots object');
			}

			const raw = bundle.slots[String(slot)];
			if (raw === undefined) { return undefined; }  // bundle に slot 無し — no-op

			// 登録済み migration を適用し、slot を現在 version で書き込む。
			const migrated = _applyMigrations(raw);

			const meta = {
				timestamp:        migrated.timestamp,
				playtimeMs:       migrated.playtimeMs,
				title:            migrated.title,
				description:      migrated.description,
				thumbnailDataUrl: migrated.thumbnailDataUrl,
			};
			return _withFallback(
				function() { return _cefWrite(slot, migrated.data, meta); },
				function() { return _lsWrite(slot, migrated.data, meta); }
			);
		});
	};

	/**
	 * bundle Blob から全 slot を import する。
	 *
	 * bundle にある slot は local data を上書きする。
	 * bundle に無い slot はそのまま残す。
	 *
	 * bundle.engineVersion > SCHEMA_VERSION なら reject する。
	 *
	 * @param {Blob} blob
	 * @returns {Promise<{imported: number[], skipped: number[]}>}
	 *   imported — 書き込まれた slot index
	 *   skipped  — bundle に無かった slot index
	 */
	save.importAll = function(blob)
	{
		return blob.text().then(function(text)
		{
			let bundle;
			try   { bundle = JSON.parse(text); }
			catch (e) { throw new Error('mitiru.save.importAll: invalid bundle JSON: ' + e.message); }

			if (typeof bundle.engineVersion !== 'number')
			{
				throw new Error('mitiru.save.importAll: bundle missing engineVersion');
			}
			if (bundle.engineVersion > SCHEMA_VERSION)
			{
				throw new Error(
					'mitiru.save.importAll: bundle engineVersion ' + bundle.engineVersion
					+ ' is newer than runtime SCHEMA_VERSION ' + SCHEMA_VERSION
					+ '; cannot import'
				);
			}
			if (!bundle.slots || typeof bundle.slots !== 'object')
			{
				throw new Error('mitiru.save.importAll: bundle missing slots object');
			}

			const imported = [];
			const skipped  = [];
			const writes   = [];

			for (let i = 0; i < MAX_SLOTS; ++i)
			{
				const raw = bundle.slots[String(i)];
				if (raw === undefined)
				{
					skipped.push(i);
					continue;
				}

				imported.push(i);
				const migrated = _applyMigrations(raw);
				const meta = {
					timestamp:        migrated.timestamp,
					playtimeMs:       migrated.playtimeMs,
					title:            migrated.title,
					description:      migrated.description,
					thumbnailDataUrl: migrated.thumbnailDataUrl,
				};
				// closure で loop 変数を捕捉する。
				(function(s, d, m)
				{
					writes.push(
						_withFallback(
							function() { return _cefWrite(s, d, m); },
							function() { return _lsWrite(s, d, m); }
						)
					);
				}(i, migrated.data, meta));
			}

			return Promise.all(writes).then(function()
			{
				return { imported: imported, skipped: skipped };
			});
		});
	};

	/**
	 * Blob の browser file-download を起動する。同期的。
	 *
	 * 一時的な object-URL を作り、anchor の click を模擬し、object URL の
	 * leak を防ぐため次の macrotask (100 ms) で URL を revoke する。
	 *
	 * @param {Blob}   blob
	 * @param {string} [filename='mitiru_save.sav']
	 */
	save.triggerDownload = function(blob, filename)
	{
		const name = (typeof filename === 'string' && filename) ? filename : 'mitiru_save.sav';
		const url  = URL.createObjectURL(blob);
		const a    = document.createElement('a');
		a.href     = url;
		a.download = name;
		a.style.display = 'none';
		document.body.appendChild(a);
		a.click();
		document.body.removeChild(a);
		// browser が download を開始できるだけの猶予を置いてから revoke する。
		setTimeout(function() { URL.revokeObjectURL(url); }, 100);
	};

	/**
	 * 現在の schema version 番号を返す。
	 * 便宜的な accessor — save.SCHEMA_VERSION と同じ値。
	 * @returns {number}
	 */
	save.currentVersion = function()
	{
		return SCHEMA_VERSION;
	};

})(typeof window !== 'undefined' ? window : globalThis);
