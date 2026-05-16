/* konami-mascot.js — Konami code easter egg (home page only).
 * Sequence: ArrowUp ArrowUp ArrowDown ArrowDown ArrowLeft ArrowRight
 *           ArrowLeft ArrowRight b a
 * On match: a small SVG frog mascot appears in the bottom-right corner,
 * bounces gently for ~6 seconds, then fades and removes itself.
 * Honours prefers-reduced-motion (mascot appears but does not bounce).
 * No external dependencies. No emojis.
 */
(function () {
  var SEQ = [
    'ArrowUp', 'ArrowUp', 'ArrowDown', 'ArrowDown',
    'ArrowLeft', 'ArrowRight', 'ArrowLeft', 'ArrowRight',
    'b', 'a'
  ];
  var pos = 0;
  var cooldown = false;

  var reduce = window.matchMedia &&
    window.matchMedia('(prefers-reduced-motion: reduce)').matches;

  function buildMascot() {
    var el = document.createElement('div');
    el.id = 'konami-mascot';
    el.setAttribute('aria-hidden', 'true');
    el.className = reduce ? 'konami-mascot konami-mascot--still' : 'konami-mascot';
    /* Inline SVG — simple geometric frog, no emoji, no bitmap.
       Body: dark-green ellipse. Eyes: two small white circles with pupils.
       Legs: short arc strokes on each side. */
    el.innerHTML = [
      '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 56"',
      '     width="64" height="56" aria-hidden="true">',
      '  <!-- body -->',
      '  <ellipse cx="32" cy="34" rx="22" ry="17" fill="#2d6a2d"/>',
      '  <!-- head bump -->',
      '  <ellipse cx="32" cy="20" rx="16" ry="12" fill="#2d6a2d"/>',
      '  <!-- left eye socket -->',
      '  <ellipse cx="22" cy="14" rx="6" ry="6" fill="#4caf50"/>',
      '  <!-- right eye socket -->',
      '  <ellipse cx="42" cy="14" rx="6" ry="6" fill="#4caf50"/>',
      '  <!-- left pupil -->',
      '  <circle cx="22" cy="14" r="3" fill="#1a1a1a"/>',
      '  <!-- right pupil -->',
      '  <circle cx="42" cy="14" r="3" fill="#1a1a1a"/>',
      '  <!-- left pupil highlight -->',
      '  <circle cx="23.5" cy="12.5" r="1" fill="#ffffff"/>',
      '  <!-- right pupil highlight -->',
      '  <circle cx="43.5" cy="12.5" r="1" fill="#ffffff"/>',
      '  <!-- mouth -->',
      '  <path d="M 25 28 Q 32 33 39 28" stroke="#1a3d1a" stroke-width="1.5"',
      '        fill="none" stroke-linecap="round"/>',
      '  <!-- left front leg -->',
      '  <path d="M 12 38 Q 6 44 8 50" stroke="#2d6a2d" stroke-width="4"',
      '        fill="none" stroke-linecap="round"/>',
      '  <!-- right front leg -->',
      '  <path d="M 52 38 Q 58 44 56 50" stroke="#2d6a2d" stroke-width="4"',
      '        fill="none" stroke-linecap="round"/>',
      '  <!-- left back leg (smaller, tucked) -->',
      '  <path d="M 16 46 Q 10 52 14 55" stroke="#2d6a2d" stroke-width="3"',
      '        fill="none" stroke-linecap="round"/>',
      '  <!-- right back leg -->',
      '  <path d="M 48 46 Q 54 52 50 55" stroke="#2d6a2d" stroke-width="3"',
      '        fill="none" stroke-linecap="round"/>',
      '</svg>'
    ].join('\n');
    return el;
  }

  function showMascot() {
    if (document.getElementById('konami-mascot')) return; // already showing
    cooldown = true;

    var mascot = buildMascot();
    document.body.appendChild(mascot);

    /* Trigger entrance — RAF ensures the initial opacity:0 is painted first. */
    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        mascot.classList.add('konami-mascot--visible');
      });
    });

    /* Auto-dismiss after 6 seconds. */
    setTimeout(function () {
      mascot.classList.remove('konami-mascot--visible');
      mascot.addEventListener('transitionend', function onEnd() {
        mascot.removeEventListener('transitionend', onEnd);
        if (mascot.parentNode) mascot.parentNode.removeChild(mascot);
        cooldown = false;
      });
      /* Safety fallback in case transitionend never fires (reduced-motion). */
      setTimeout(function () {
        if (mascot.parentNode) mascot.parentNode.removeChild(mascot);
        cooldown = false;
      }, 700);
    }, 6000);
  }

  document.addEventListener('keydown', function (e) {
    /* Ignore when user is typing in a field. */
    var tag = (e.target && e.target.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'textarea' || tag === 'select') {
      pos = 0;
      return;
    }
    /* Ignore modifier combos. */
    if (e.metaKey || e.ctrlKey || e.altKey) return;

    if (e.key === SEQ[pos]) {
      pos += 1;
      if (pos === SEQ.length) {
        pos = 0;
        if (!cooldown) showMascot();
      }
    } else {
      /* Partial reset: check if current key starts a new sequence. */
      pos = (e.key === SEQ[0]) ? 1 : 0;
    }
  });
})();
