/*!
 * mitiru_save.js — whole-game-state save slots (F-11)
 *
 * 10 numbered save slots, each holding a versioned, timestamped game-state blob.
 * Supports localStorage (dev / web fallback) and a CEF file-bridge backend
 * (production, no practical size limit).
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-11
 *
 * ── API ─────────────────────────────────────────────────────────────────────
 *   mitiru.save.SCHEMA_VERSION            number — bump when stored format changes
 *   mitiru.save.MAX_SLOTS                 number — always 10
 *   mitiru.save.write(slot, data, meta?)  Promise<void>
 *   mitiru.save.read(slot)               Promise<{data,meta}|null>
 *   mitiru.save.list()                   Promise<Array<{slot,exists,meta}>>
 *   mitiru.save.delete(slot)             Promise<void>
 *   mitiru.save.registerMigration(from, to, fn)
 *   mitiru.save.migrateAll()             Promise<void>  — migrate every occupied slot
 *   mitiru.save.backend()                'cef' | 'localStorage'
 *   mitiru.save.sizeBytes()              Promise<number>  — -1 in CEF
 *   mitiru.save.exportSlot(slot)         Promise<Blob>   — NF-06
 *   mitiru.save.exportAll()              Promise<Blob>   — NF-06
 *   mitiru.save.importSlot(blob, slot)   Promise<void>   — NF-06
 *   mitiru.save.importAll(blob)          Promise<{imported:number[],skipped:number[]}> — NF-06
 *   mitiru.save.triggerDownload(blob, filename?)  void  — NF-06
 *   mitiru.save.currentVersion()         number          — NF-06
 *
 * ── Storage keys (localStorage) ─────────────────────────────────────────────
 *   mitiru.save.slot.<N>.data      committed data blob (JSON)
 *   mitiru.save.slot.<N>.meta      committed meta blob (JSON)
 *   mitiru.save.slot.<N>.staging   atomic staging — present only during write
 *
 * ── CEF handler namespace ────────────────────────────────────────────────────
 *   save.write   {slot, payload: <full blob JSON string>}
 *   save.read    {slot}
 *   save.list    {}
 *   save.delete  {slot}
 *
 *   TODO(engine): proper disk-backed save handler in StateStore.
 *   C++ registration stub signature:
 *
 *     ctx.registerHandler("save.write",  [](const std::string& payload) -> std::string);
 *     ctx.registerHandler("save.read",   [](const std::string& payload) -> std::string);
 *     ctx.registerHandler("save.list",   [](const std::string& payload) -> std::string);
 *     ctx.registerHandler("save.delete", [](const std::string& payload) -> std::string);
 *
 *   All handlers receive a JSON string (payload from dispatch) and must return
 *   a JSON string.  write/delete return `{"ok":true}`.  read returns the stored
 *   blob JSON or `null`.  list returns an array of {slot,exists,meta} objects.
 *
 *   If the C++ handler is not registered, mitiru.dispatch rejects — this module
 *   catches that error and falls back to localStorage automatically.
 *
 * ── Supersedes ───────────────────────────────────────────────────────────────
 *   mitiru.state.save / .load / .listSlots (F-03) remain for per-key ephemeral
 *   state persistence.  For whole-game-state blobs (save-screen flow) use this
 *   module instead.
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.save) { return; }  // already loaded

	// ── constants ─────────────────────────────────────────────────
	const SCHEMA_VERSION = 1;
	const MAX_SLOTS      = 10;
	const KEY_PREFIX     = 'mitiru.save.slot.';

	// ── migration registry ────────────────────────────────────────
	// Each entry: { from: number, to: number, fn: (oldData) => newData }
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

		// Sort migrations ascending by `from` so chains apply in order.
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

	// ── backend detection ─────────────────────────────────────────
	function _isCef()
	{
		return typeof global.cefQuery === 'function';
	}

	// ── CEF dispatch with localStorage fallback ───────────────────
	// Returns Promise.  If the C++ handler rejects (not registered), falls back
	// to the localStorage implementation transparently.
	function _cefDispatch(action, args)
	{
		if (!_isCef() || typeof mitiru.dispatch !== 'function')
		{
			return Promise.reject(new Error('cef unavailable'));
		}
		return mitiru.dispatch(action, args)
			.catch(function(err)
			{
				// If the handler simply isn't registered on the C++ side the error
				// message will contain "unknown action".  Treat that as a signal to
				// use the localStorage fallback rather than bubbling.
				if (err && typeof err.message === 'string'
				    && err.message.indexOf('unknown action') !== -1)
				{
					return Promise.reject(new Error('cef-handler-missing'));
				}
				return Promise.reject(err);
			});
	}

	// ── localStorage implementation ───────────────────────────────

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

		// Atomic staging: write staging first, then commit, then clean up.
		try
		{
			global.localStorage.setItem(_stagingKey(slot), blobStr);
			global.localStorage.setItem(_dataKey(slot), blobStr);
			global.localStorage.setItem(_metaKey(slot), JSON.stringify(metaObj));
			global.localStorage.removeItem(_stagingKey(slot));
		}
		catch (e)
		{
			// Quota exceeded or similar — staging key remains for recovery.
			throw new Error('mitiru.save: write failed (slot ' + slot + '): ' + e.message);
		}
		return Promise.resolve();
	}

	function _lsRead(slot)
	{
		const dataStr    = global.localStorage.getItem(_dataKey(slot));
		const stagingStr = global.localStorage.getItem(_stagingKey(slot));

		// Corruption recovery: if staging exists but committed data does not
		// (or they differ), use staging and log a warning.
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
			if (d) { total += d.length * 2; }  // UTF-16: 2 bytes per char
			if (m) { total += m.length * 2; }
		}
		return Promise.resolve(total);
	}

	// ── CEF implementation (delegates to registered C++ handlers) ────

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
				// resp may be a pre-parsed object or a JSON string depending on
				// how the C++ handler was registered.
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

	// ── with-fallback wrappers ────────────────────────────────────
	// Try CEF first; on error ("unknown action" / cef unavailable), use localStorage.

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
	 * Write entire game state to a slot.
	 * @param {number}  slot  0 .. MAX_SLOTS-1
	 * @param {object}  data  JSON-serializable game state
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
	 * Read a slot.
	 * @param {number} slot
	 * @returns {Promise<{data:object,meta:object}|null>}  null if slot is empty
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
	 * List all slots.
	 * @returns {Promise<Array<{slot:number,exists:boolean,meta:object|null}>>}
	 *   Always exactly MAX_SLOTS entries.
	 */
	save.list = function()
	{
		return _withFallback(
			function() { return _cefList(); },
			function() { return _lsList(); }
		);
	};

	/**
	 * Delete a slot.  No-op if the slot is already empty.
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
	 * Register a migration from `fromVersion` to `toVersion`.
	 * Applied in ascending `from` order at read time when stored version < SCHEMA_VERSION.
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
	 * Apply all registered migrations to every occupied slot.
	 * Useful for a post-update migration sweep at game startup.
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
	 * Probe the active storage backend.
	 * @returns {'cef'|'localStorage'}
	 */
	save.backend = function()
	{
		return _isCef() ? 'cef' : 'localStorage';
	};

	/**
	 * Total bytes used by save data in localStorage.
	 * Returns -1 when running on the CEF backend (disk usage not queryable from JS).
	 * @returns {Promise<number>}
	 */
	save.sizeBytes = async function()
	{
		if (_isCef()) { return -1; }
		return _lsSizeBytes();
	};

	// ── NF-06: export / import / download ─────────────────────────

	/**
	 * Read the raw stored blob from localStorage without applying migrations.
	 * Returns the parsed blob object, or null if the slot is empty / corrupt.
	 * Used by exportSlot/exportAll so the exported bundle carries the actual
	 * stored shape — _applyMigrations runs at importSlot time, not export time.
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
	 * Export a single save slot as a portable Blob.
	 *
	 * Bundle format (JSON, application/octet-stream):
	 * {
	 *   engineVersion: number,   // SCHEMA_VERSION at export time
	 *   slots: {
	 *     "<N>": {               // the raw stored blob as-is from localStorage
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
	 * The raw blob is exported without running migrations so that
	 * _applyMigrations can execute on the receiving side at importSlot time.
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
	 * Export all occupied save slots as a single portable Blob.
	 *
	 * Bundle format is the same as exportSlot but `slots` may have 0..MAX_SLOTS
	 * entries.  Empty slots are omitted entirely.
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
	 * Import a bundle Blob into a specific slot, overwriting existing data.
	 *
	 * The bundle's stored blob for the matching slot is run through
	 * _applyMigrations before writing, so the saved slot is always at the
	 * current SCHEMA_VERSION after import.
	 *
	 * Throws RangeError synchronously for invalid slot (mirrors write() behaviour).
	 * Rejects if bundle.engineVersion > SCHEMA_VERSION.
	 * Resolves with void if the bundle contains no data for the given slot.
	 *
	 * @param {Blob}   blob
	 * @param {number} slot  0 .. MAX_SLOTS-1
	 * @returns {Promise<void>}
	 */
	save.importSlot = function(blob, slot)
	{
		// Validate synchronously — mirrors write() which calls _validateSlot
		// as its first statement before entering any async work.
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
			if (raw === undefined) { return undefined; }  // slot absent in bundle — no-op

			// Apply any registered migrations so the slot is written at current version.
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
	 * Import all slots from a bundle Blob.
	 *
	 * Slots present in the bundle overwrite local data.
	 * Slots absent in the bundle are left untouched.
	 *
	 * Rejects if bundle.engineVersion > SCHEMA_VERSION.
	 *
	 * @param {Blob} blob
	 * @returns {Promise<{imported: number[], skipped: number[]}>}
	 *   imported — slot indices that were written
	 *   skipped  — slot indices absent in the bundle
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
				// Capture loop variable via closure.
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
	 * Trigger a browser file-download for a Blob.  Synchronous.
	 *
	 * Creates a temporary object-URL, simulates an anchor click, then revokes
	 * the URL on the next macrotask (100 ms) to avoid leaking object URLs.
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
		// Revoke after the browser has had a chance to begin the download.
		setTimeout(function() { URL.revokeObjectURL(url); }, 100);
	};

	/**
	 * Return the current schema version number.
	 * Convenience accessor — same value as save.SCHEMA_VERSION.
	 * @returns {number}
	 */
	save.currentVersion = function()
	{
		return SCHEMA_VERSION;
	};

})(typeof window !== 'undefined' ? window : globalThis);
