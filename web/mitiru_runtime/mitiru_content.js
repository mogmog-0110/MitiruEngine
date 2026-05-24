/*!
 * mitiru_content.js — content-script loader + schema registry (F-16)
 *
 * Thin convention layer on top of mitiru.loadJson for scene-specific content
 * files (novel scripts, quest definitions, dialogue trees, etc.).
 *
 * ── Path convention ────────────────────────────────────────────────────────
 *   Default base dir:  'assets/content/'
 *   Chapter path:      '<baseDir><scene>/<chapter>.json'
 *
 *     mitiru.content.path('cooking', 'ch01')
 *       → 'assets/content/cooking/ch01.json'
 *
 * ── Schema registry ────────────────────────────────────────────────────────
 *   registerSchema(name, validator) — validator is a function that receives
 *   the parsed JSON and must throw on invalid input. Built-in schemas:
 *
 *     'novel'    — validates { id?, lines: [...] } against the mitiru_novel
 *                  line-type shape (text / bg / sprite / wait / choice).
 *     'dialogue' — validates { lines: [{speaker,text}, ...] }  (superset of
 *                  novel minus bg/sprite/choice — suitable for quick cutscenes).
 *     'quest'    — validates { id, steps: [...], flags?: [...], rewards?: {} }.
 *
 * ── API ────────────────────────────────────────────────────────────────────
 *   mitiru.content.setBaseDir(path)                  default 'assets/content/'
 *   mitiru.content.baseDir()                         current base dir
 *   mitiru.content.path(scene, chapter)              → full URL string
 *   mitiru.content.load(scene, chapter, opts?)       → Promise<data>
 *   mitiru.content.loadPath(path, opts?)             → Promise<data>
 *   mitiru.content.loadManifest(path, opts?)         → Promise<{data, url, resolve}>  (NF-01)
 *   mitiru.content.registerSchema(name, validator)   register or override
 *   mitiru.content.hasSchema(name)                   boolean
 *   mitiru.content.schemas()                         string[]
 *
 *   opts: {
 *     schemaName: 'novel',           // registry lookup; throws on invalid data
 *     validator:  fn(data),          // custom validator fn; same semantics
 *     required:   ['lines'],         // dot-paths that must resolve (via loadJson)
 *     schemaVersion: '1.0.0',        // data.schema_version === ...
 *     freeze:     true,              // deep-freeze on load (default true)
 *   }
 *
 * ── Error handling ─────────────────────────────────────────────────────────
 *   Validation failures throw with a prefix identifying the source path:
 *     "mitiru.content: <path> — <schemaName|validator>: <reason>"
 *   so production log scraping can distinguish network vs schema failures.
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-16
 */
(function(global)
{
	'use strict';

	const mitiru = global.mitiru = global.mitiru || {};
	if (mitiru.content) { return; }  // already loaded

	// ── internal state ──────────────────────────────────────────
	let   _baseDir = 'assets/content/';
	const _schemas = Object.create(null);   // name -> validator fn

	// ── helpers ─────────────────────────────────────────────────
	function _normalizeBase(p)
	{
		if (typeof p !== 'string' || p === '') { return 'assets/content/'; }
		return p[p.length - 1] === '/' ? p : (p + '/');
	}

	function _wrapValidator(pathStr, label, fn, data)
	{
		try { fn(data); }
		catch (e)
		{
			throw new Error(
				'mitiru.content: ' + pathStr + ' — ' + label + ': '
				+ ((e && e.message) || String(e))
			);
		}
	}

	// ── built-in schemas ────────────────────────────────────────
	function _assert(cond, msg) { if (!cond) { throw new Error(msg); } }

	function _validateNovel(data)
	{
		_assert(data && typeof data === 'object', 'expected an object');
		_assert(Array.isArray(data.lines),        'missing required array "lines"');
		const allowed = { text:1, dialogue:1, bg:1, sprite:1, wait:1, choice:1 };
		for (let i = 0; i < data.lines.length; ++i)
		{
			const line = data.lines[i];
			_assert(line && typeof line === 'object', 'lines[' + i + '] is not an object');
			const type = line.type || 'text';
			_assert(allowed[type], 'lines[' + i + '].type unknown: ' + type);
			if (type === 'text' || type === 'dialogue')
			{
				_assert(typeof line.text === 'string', 'lines[' + i + '].text must be a string');
			}
			else if (type === 'bg')
			{
				_assert(typeof line.path === 'string' && line.path, 'lines[' + i + '].path required');
			}
			else if (type === 'sprite')
			{
				_assert(typeof line.id === 'string' && line.id, 'lines[' + i + '].id required');
				if (!line.hide)
				{
					_assert(typeof line.path === 'string' && line.path, 'lines[' + i + '].path required when !hide');
				}
			}
			else if (type === 'wait')
			{
				_assert(typeof line.ms === 'number' && line.ms >= 0, 'lines[' + i + '].ms must be >= 0 number');
			}
			else if (type === 'choice')
			{
				_assert(Array.isArray(line.options) && line.options.length >= 1,
				        'lines[' + i + '].options must be non-empty array');
				for (let j = 0; j < line.options.length; ++j)
				{
					_assert(typeof line.options[j].label === 'string',
					        'lines[' + i + '].options[' + j + '].label required');
				}
			}
		}
	}

	function _validateDialogue(data)
	{
		_assert(data && typeof data === 'object', 'expected an object');
		_assert(Array.isArray(data.lines),        'missing required array "lines"');
		for (let i = 0; i < data.lines.length; ++i)
		{
			const line = data.lines[i];
			_assert(line && typeof line === 'object',        'lines[' + i + '] is not an object');
			_assert(typeof line.text === 'string',           'lines[' + i + '].text must be a string');
			if (line.speaker !== undefined)
			{
				_assert(typeof line.speaker === 'string',    'lines[' + i + '].speaker must be a string');
			}
		}
	}

	function _validateQuest(data)
	{
		_assert(data && typeof data === 'object',  'expected an object');
		_assert(typeof data.id === 'string' && data.id, 'missing required "id"');
		_assert(Array.isArray(data.steps) && data.steps.length >= 1, '"steps" must be non-empty array');
		for (let i = 0; i < data.steps.length; ++i)
		{
			const s = data.steps[i];
			_assert(s && typeof s === 'object',            'steps[' + i + '] is not an object');
			_assert(typeof s.id === 'string' && s.id,      'steps[' + i + '].id required');
			_assert(typeof s.goal === 'string' && s.goal,  'steps[' + i + '].goal required');
		}
		if (data.flags !== undefined)   { _assert(Array.isArray(data.flags),  '"flags" must be an array'); }
		if (data.rewards !== undefined) { _assert(data.rewards && typeof data.rewards === 'object', '"rewards" must be an object'); }
	}

	_schemas['novel']    = _validateNovel;
	_schemas['dialogue'] = _validateDialogue;
	_schemas['quest']    = _validateQuest;

	// ── public API ──────────────────────────────────────────────
	const content = {};

	content.setBaseDir = function(p)
	{
		_baseDir = _normalizeBase(p);
	};

	content.baseDir = function()
	{
		return _baseDir;
	};

	content.path = function(scene, chapter)
	{
		if (typeof scene   !== 'string' || !scene)   { throw new Error('mitiru.content.path: scene required'); }
		if (typeof chapter !== 'string' || !chapter) { throw new Error('mitiru.content.path: chapter required'); }
		return _baseDir + scene + '/' + chapter + '.json';
	};

	content.registerSchema = function(name, validator)
	{
		if (typeof name !== 'string' || !name)          { throw new Error('mitiru.content.registerSchema: name required'); }
		if (typeof validator !== 'function')            { throw new Error('mitiru.content.registerSchema: validator must be a function'); }
		_schemas[name] = validator;
	};

	content.hasSchema = function(name) { return !!_schemas[name]; };
	content.schemas   = function()     { return Object.keys(_schemas); };

	content.loadPath = async function(pathStr, opts)
	{
		if (typeof pathStr !== 'string' || !pathStr)
		{
			throw new Error('mitiru.content.loadPath: path required');
		}
		opts = opts || {};

		// Delegate I/O + basic validation to loadJson (E-04).
		if (!mitiru.loadJson)
		{
			throw new Error('mitiru.content.loadPath: mitiru.loadJson not loaded');
		}
		const loadOpts = {
			freeze:   opts.freeze !== false,
			required: opts.required || null,
		};
		if (opts.schemaVersion !== undefined) { loadOpts.schema = opts.schemaVersion; }
		// loadJson freezes by default; we want to validate BEFORE freezing.
		loadOpts.freeze = false;

		const data = await mitiru.loadJson(pathStr, loadOpts);

		// Registry schema.
		if (opts.schemaName)
		{
			const v = _schemas[opts.schemaName];
			if (!v) { throw new Error('mitiru.content.loadPath: unknown schemaName "' + opts.schemaName + '"'); }
			_wrapValidator(pathStr, opts.schemaName, v, data);
		}

		// Custom validator.
		if (typeof opts.validator === 'function')
		{
			_wrapValidator(pathStr, 'validator', opts.validator, data);
		}

		// Freeze after validation (validators may iterate freely).
		if (opts.freeze !== false) { _deepFreeze(data); }
		return data;
	};

	content.load = function(scene, chapter, opts)
	{
		return content.loadPath(content.path(scene, chapter), opts);
	};

	/**
	 * Load a manifest JSON and return a bound resolver (NF-01).
	 *
	 * Entries in a manifest typically reference other assets via paths that
	 * are relative to the MANIFEST's location — not the page's base URL.
	 * Resolving those paths against `document.baseURI` silently 404s when
	 * the page lives elsewhere (e.g. `/ui/novel.html` loading a manifest
	 * from `/ui/data/` whose entries target `/assets/scripts/…`).
	 *
	 * Usage:
	 *   const m = await mitiru.content.loadManifest('data/script_manifest.json');
	 *   for (const entry of m.data.chapters)
	 *   {
	 *       const url = m.resolve(entry.path);   // absolute URL
	 *       fetch(url);
	 *   }
	 *
	 * Returns { data, url, resolve(rel) } where url is the absolute URL the
	 * manifest was fetched from and resolve() rebases any relative path
	 * against that URL.
	 *
	 * All loadPath opts (schemaName, validator, required, schemaVersion,
	 * freeze) are forwarded.  The returned object is not frozen (the
	 * resolve function is attached); `data` IS frozen unless freeze:false.
	 */
	content.loadManifest = async function(pathStr, opts)
	{
		if (typeof pathStr !== 'string' || !pathStr)
		{
			throw new Error('mitiru.content.loadManifest: path required');
		}
		const absUrl = mitiru.resolveUrl
			? mitiru.resolveUrl(pathStr)
			: (new URL(pathStr, (typeof document !== 'undefined' && document.baseURI) || undefined).href);

		const data = await content.loadPath(pathStr, opts);

		return {
			data:    data,
			url:     absUrl,
			resolve: function(rel)
			{
				if (mitiru.resolveUrl) { return mitiru.resolveUrl(rel, absUrl); }
				return new URL(rel, absUrl).href;
			},
		};
	};

	function _deepFreeze(obj)
	{
		if (obj === null || typeof obj !== 'object') { return obj; }
		if (Object.isFrozen(obj)) { return obj; }
		Object.freeze(obj);
		const keys = Object.keys(obj);
		for (let i = 0; i < keys.length; ++i) { _deepFreeze(obj[keys[i]]); }
		return obj;
	}

	// ── export ──────────────────────────────────────────────────
	mitiru.content = content;

})(typeof window !== 'undefined' ? window : globalThis);
