/*!
 * mitiru_transitions.js — named scene / overlay transition library (F-05)
 *
 * Provides a menu of reusable CSS-animated transitions powered by the
 * Web Animations API (WAAPI).  Works both within a scene (overlay open/close)
 * and between scenes when wired into mitiru.router's `in`/`out` hooks:
 *
 *   router.register('mainmenu', {
 *     url: 'scenes/mainmenu.html',
 *     out: () => mitiru.transitions.run(document.body, 'iris-out', { duration: 500 }),
 *     in:  () => mitiru.transitions.run(document.body, 'iris-in',  { duration: 500 }),
 *   });
 *
 * API:
 *   mitiru.transitions.run(el, name, options?)  → Promise<void>
 *   mitiru.transitions.register(name, fn, opts?)    — fn(el, options) => Promise<void>
 *   mitiru.transitions.list()                   → string[]
 *   mitiru.transitions.chain(el, pairs)         → Promise<void>
 *     pairs: [[name, options?], ...]
 *
 * Options (all optional):
 *   duration  {number}  ms, default 400
 *   easing    {string}  CSS easing, default 'ease'
 *   direction {string}  'left'|'right'|'up'|'down' (slide/pan), default 'left'
 *   intensity {number}  0..1 (blur magnitude for walk-through), default 0.5
 *   reverse   {boolean} run the transition backwards (used by fade), default false
 *   from      {Element} outgoing element (crossfade)
 *   to        {Element} incoming element (crossfade)
 *   color     {string}  fill color for ink-dot overlay, default '#000'
 *   override  {boolean} allow re-registering an existing name (register only)
 *
 * Built-ins (12):
 *   fade, instant (alias: cut), crossfade,
 *   slide-in, slide-out,
 *   iris-in, iris-out,
 *   pan,
 *   ink-dot,
 *   page-fold, card-fold,
 *   walk-through
 *
 * Implements spec: docs/feedback-from-kaerucrape/2026-04-24.md F-05
 */
(function (global)
{
    'use strict';

    var mitiru = global.mitiru = global.mitiru || {};
    if (mitiru.transitions) { return; }   // already loaded

    // ── constants ──────────────────────────────────────────────────────────
    var DEFAULT_DURATION = 400;
    var DEFAULT_EASING   = 'ease';

    // ── internal registry ──────────────────────────────────────────────────
    var _registry = Object.create(null);  // name → fn(el, opts) => Promise<void>

    // ── WAAPI helper ───────────────────────────────────────────────────────
    /**
     * Run a WAAPI animation on `el`.
     * When duration === 0 the animation is skipped entirely and a resolved
     * Promise is returned — this satisfies the "duration:0 is a no-op" rule
     * without handing a zero-duration animation to the browser.
     *
     * @param  {Element}  el
     * @param  {object[]} keyframes  — WAAPI keyframes array
     * @param  {object}   opts       — { duration, easing, fill? }
     * @returns {Promise<void>}
     */
    function _animate(el, keyframes, opts)
    {
        var duration = (opts && opts.duration != null) ? opts.duration : DEFAULT_DURATION;
        var easing   = (opts && opts.easing)           ? opts.easing   : DEFAULT_EASING;
        var fill     = (opts && opts.fill)             ? opts.fill     : 'forwards';

        if (duration <= 0)
        {
            // Apply the last keyframe's properties so the element ends up in
            // the correct visual state even without actually animating.
            var last = keyframes[keyframes.length - 1];
            if (last)
            {
                Object.keys(last).forEach(function (prop)
                {
                    el.style[prop] = last[prop];
                });
            }
            return Promise.resolve();
        }

        var animation = el.animate(keyframes, {
            duration : duration,
            easing   : easing,
            fill     : fill,
        });
        return animation.finished;
    }

    // ── direction helpers ──────────────────────────────────────────────────
    function _slideTranslate(direction, inward)
    {
        // Returns [fromTransform, toTransform].
        // inward = true → element enters. inward = false → element exits.
        var dir = direction || 'left';
        var start;
        if      (dir === 'left')  { start = inward ? 'translateX(-100%)' : 'translateX(0)'; }
        else if (dir === 'right') { start = inward ? 'translateX(100%)'  : 'translateX(0)'; }
        else if (dir === 'up')    { start = inward ? 'translateY(-100%)' : 'translateY(0)'; }
        else                      { start = inward ? 'translateY(100%)'  : 'translateY(0)'; }

        var end;
        if      (dir === 'left')  { end = inward ? 'translateX(0)' : 'translateX(100%)'; }
        else if (dir === 'right') { end = inward ? 'translateX(0)' : 'translateX(-100%)'; }
        else if (dir === 'up')    { end = inward ? 'translateY(0)' : 'translateY(100%)'; }
        else                      { end = inward ? 'translateY(0)' : 'translateY(-100%)'; }

        return [start, end];
    }

    // ── built-in transition implementations ───────────────────────────────

    function _builtinFade(el, opts)
    {
        var reverse  = opts && opts.reverse;
        var keyframes = reverse
            ? [{ opacity: '0' }, { opacity: '1' }]
            : [{ opacity: '1' }, { opacity: '0' }];
        return _animate(el, keyframes, opts);
    }

    function _builtinInstant(_el, _opts)
    {
        return Promise.resolve();
    }

    function _builtinCrossfade(el, opts)
    {
        // `from` fades out; `to` fades in. `el` is ignored when from/to present.
        var from = (opts && opts.from) ? opts.from : el;
        var to   = (opts && opts.to)   ? opts.to   : el;

        var fadeOut = _animate(from, [{ opacity: '1' }, { opacity: '0' }], opts);
        var fadeIn  = _animate(to,   [{ opacity: '0' }, { opacity: '1' }], opts);
        return Promise.all([fadeOut, fadeIn]).then(function () {});
    }

    function _builtinSlideIn(el, opts)
    {
        var pair = _slideTranslate(opts && opts.direction, true);
        return _animate(el, [
            { transform: pair[0] },
            { transform: pair[1] },
        ], opts);
    }

    function _builtinSlideOut(el, opts)
    {
        var pair = _slideTranslate(opts && opts.direction, false);
        return _animate(el, [
            { transform: pair[0] },
            { transform: pair[1] },
        ], opts);
    }

    function _builtinIrisIn(el, opts)
    {
        return _animate(el, [
            { clipPath: 'circle(0% at 50% 50%)'   },
            { clipPath: 'circle(150% at 50% 50%)' },
        ], opts);
    }

    function _builtinIrisOut(el, opts)
    {
        return _animate(el, [
            { clipPath: 'circle(150% at 50% 50%)' },
            { clipPath: 'circle(0% at 50% 50%)'   },
        ], opts);
    }

    function _builtinPan(el, opts)
    {
        var dir      = (opts && opts.direction) || 'left';
        var isHoriz  = (dir === 'left' || dir === 'right');
        var sign     = (dir === 'right' || dir === 'down') ? 1 : -1;
        var amount   = sign * 5;   // 5% parallax shift
        var translate = isHoriz
            ? 'translateX(' + amount + '%)'
            : 'translateY(' + amount + '%)';

        return _animate(el, [
            { transform: 'scale(1) translateX(0)'           },
            { transform: 'scale(1.02) ' + translate },
        ], opts);
    }

    function _builtinInkDot(el, opts)
    {
        // Expands a radial clip from the element's top-left corner by default.
        // If opts.color is set, a cover overlay is created and removed on completion.
        var color  = (opts && opts.color) || null;
        var origin = (opts && opts.origin) || '50% 50%';

        if (color)
        {
            // Create a temporary full-cover overlay and animate it.
            var cover = document.createElement('div');
            cover.style.cssText = [
                'position:fixed', 'inset:0',
                'background:' + color,
                'pointer-events:none',
                'z-index:99999',
            ].join(';');
            if (el.parentNode) { el.parentNode.insertBefore(cover, el.nextSibling); }
            else if (typeof document !== 'undefined') { document.body.appendChild(cover); }

            return _animate(cover, [
                { clipPath: 'circle(0% at ' + origin + ')'   },
                { clipPath: 'circle(150% at ' + origin + ')' },
            ], opts).then(function ()
            {
                if (cover.parentNode) { cover.parentNode.removeChild(cover); }
            });
        }

        return _animate(el, [
            { clipPath: 'circle(0% at ' + origin + ')'   },
            { clipPath: 'circle(150% at ' + origin + ')' },
        ], opts);
    }

    function _builtinPageFold(el, opts)
    {
        // Book-page flip around vertical axis.
        return _animate(el, [
            { transform: 'perspective(1200px) rotateY(0deg)'    },
            { transform: 'perspective(1200px) rotateY(-180deg)' },
        ], opts);
    }

    function _builtinCardFold(el, opts)
    {
        // Card flip around horizontal axis (downward).
        return _animate(el, [
            { transform: 'perspective(1200px) rotateX(0deg)'   },
            { transform: 'perspective(1200px) rotateX(90deg)'  },
        ], opts);
    }

    function _builtinWalkThrough(el, opts)
    {
        var intensity = (opts && opts.intensity != null) ? opts.intensity : 0.5;
        var blurPx    = Math.round(intensity * 20);

        return _animate(el, [
            { filter: 'blur(0px)',        transform: 'scale(1)'   },
            { filter: 'blur(' + blurPx + 'px)', transform: 'scale(1.1)' },
        ], opts);
    }

    // ── register built-ins ─────────────────────────────────────────────────
    var _BUILTINS = {
        'fade'        : _builtinFade,
        'instant'     : _builtinInstant,
        'cut'         : _builtinInstant,       // alias
        'crossfade'   : _builtinCrossfade,
        'slide-in'    : _builtinSlideIn,
        'slide-out'   : _builtinSlideOut,
        'iris-in'     : _builtinIrisIn,
        'iris-out'    : _builtinIrisOut,
        'pan'         : _builtinPan,
        'ink-dot'     : _builtinInkDot,
        'page-fold'   : _builtinPageFold,
        'card-fold'   : _builtinCardFold,
        'walk-through': _builtinWalkThrough,
    };

    Object.keys(_BUILTINS).forEach(function (name)
    {
        _registry[name] = _BUILTINS[name];
    });

    // Canonical list excludes the 'cut' alias so list() returns clean names.
    var _BUILTIN_NAMES = [
        'fade', 'instant', 'crossfade',
        'slide-in', 'slide-out',
        'iris-in', 'iris-out',
        'pan', 'ink-dot',
        'page-fold', 'card-fold',
        'walk-through',
    ];

    // ── public API ─────────────────────────────────────────────────────────
    var transitions = mitiru.transitions = Object.create(null);

    /**
     * Run a named transition on `el`.
     * Returns a Promise that resolves when the transition finishes.
     *
     * @param  {Element} el
     * @param  {string}  name
     * @param  {object=} options
     * @returns {Promise<void>}
     */
    transitions.run = function (el, name, options)
    {
        if (!el || typeof el.animate !== 'function' && typeof el !== 'object')
        {
            return Promise.reject(
                new Error('mitiru.transitions.run: el must be a DOM element'));
        }
        if (typeof name !== 'string' || name === '')
        {
            return Promise.reject(
                new Error('mitiru.transitions.run: name must be a non-empty string'));
        }
        var fn = _registry[name];
        if (!fn)
        {
            return Promise.reject(
                new Error('mitiru.transitions.run: unknown transition "' + name + '"'));
        }
        var opts = Object.assign({ duration: DEFAULT_DURATION, easing: DEFAULT_EASING }, options);
        try
        {
            var result = fn(el, opts);
            if (result && typeof result.then === 'function') { return result; }
            return Promise.resolve();
        }
        catch (e)
        {
            return Promise.reject(e);
        }
    };

    /**
     * Register a custom transition (or override an existing one).
     * fn signature: fn(el, options) => Promise<void>
     *
     * @param  {string}   name
     * @param  {Function} fn
     * @param  {object=}  opts   — { override: boolean }
     */
    transitions.register = function (name, fn, opts)
    {
        if (typeof name !== 'string' || name === '')
        {
            throw new Error('mitiru.transitions.register: name must be a non-empty string');
        }
        if (typeof fn !== 'function')
        {
            throw new Error('mitiru.transitions.register: fn must be a function');
        }
        var allowOverride = opts && opts.override;
        if (_registry[name] && !allowOverride)
        {
            throw new Error(
                'mitiru.transitions.register: "' + name + '" is already registered. '
                + 'Pass { override: true } to replace it.');
        }
        _registry[name] = fn;
    };

    /**
     * Return a sorted array of all registered transition names.
     * Built-in alias 'cut' is omitted from the list (it is still runnable).
     *
     * @returns {string[]}
     */
    transitions.list = function ()
    {
        var names = Object.keys(_registry).filter(function (n) { return n !== 'cut'; });
        return names.slice().sort();
    };

    /**
     * Run multiple transitions sequentially on `el`.
     * Each pair is [name, options?].  Returns a Promise resolving after all
     * transitions have completed in order.
     *
     * @param  {Element}   el
     * @param  {Array}     pairs  — [[name, opts?], ...]
     * @returns {Promise<void>}
     */
    transitions.chain = function (el, pairs)
    {
        if (!Array.isArray(pairs))
        {
            return Promise.reject(
                new Error('mitiru.transitions.chain: pairs must be an array'));
        }
        return pairs.reduce(function (promise, pair)
        {
            return promise.then(function ()
            {
                var name = pair[0];
                var opts = pair[1] || {};
                return transitions.run(el, name, opts);
            });
        }, Promise.resolve());
    };

})(typeof window !== 'undefined' ? window : globalThis);
