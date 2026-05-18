/* card-tilt.js — subtle tilt micro-interaction for list cards.
 * Targets .guides-list__link only (chapter list now uses a flat hover).
 * Writes --tilt-x and --tilt-y CSS custom properties on the hovered
 * element; CSS handles the actual transform (perspective + rotateX/Y).
 * Max ±2.5deg. Fast 150ms transition on enter, 200ms on leave.
 * Disabled entirely under prefers-reduced-motion.
 */
(function () {
  var reduce = window.matchMedia &&
    window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (reduce) return;

  var MAX_DEG = 2.5;

  function attachTilt(el) {
    function onMove(e) {
      var rect = el.getBoundingClientRect();
      /* Normalised pointer position relative to card centre: range [-1, 1]. */
      var nx = ((e.clientX - rect.left) / rect.width - 0.5) * 2;
      var ny = ((e.clientY - rect.top) / rect.height - 0.5) * 2;
      /* rotateY tilts left/right; rotateX tilts up/down (inverted). */
      var rx = (-ny * MAX_DEG).toFixed(2);
      var ry = (nx * MAX_DEG).toFixed(2);
      el.style.setProperty('--tilt-x', rx + 'deg');
      el.style.setProperty('--tilt-y', ry + 'deg');
    }

    function onLeave() {
      el.style.setProperty('--tilt-x', '0deg');
      el.style.setProperty('--tilt-y', '0deg');
    }

    el.addEventListener('pointermove', onMove, { passive: true });
    el.addEventListener('pointerleave', onLeave, { passive: true });
    /* Also reset on blur for keyboard navigation. */
    el.addEventListener('blur', onLeave, { passive: true });
  }

  function init() {
    var selectors = [
      '.guides-list__link'
    ];
    selectors.forEach(function (sel) {
      var els = document.querySelectorAll(sel);
      for (var i = 0; i < els.length; i++) attachTilt(els[i]);
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
