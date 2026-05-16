/* scroll-fade.js — adds .is-visible to elements with [data-fade] when
 * they enter the viewport. Pure CSS handles the actual transition.
 * Honours prefers-reduced-motion: just marks everything visible up front.
 */
(function () {
  var nodes = document.querySelectorAll('[data-fade]');
  if (!nodes.length) return;

  var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  if (reduce || !('IntersectionObserver' in window)) {
    for (var i = 0; i < nodes.length; i++) nodes[i].classList.add('is-visible');
    return;
  }

  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (entry) {
      if (entry.isIntersecting) {
        entry.target.classList.add('is-visible');
        io.unobserve(entry.target);
      }
    });
  }, {
    rootMargin: '0px 0px -8% 0px',
    threshold: 0.08
  });

  for (var n = 0; n < nodes.length; n++) io.observe(nodes[n]);

  /* Safety net: if any nodes haven't been revealed within 1.2s (e.g. headless
   * screenshot tools that never scroll, very tall pages, observer hiccups),
   * mark the rest visible so content never stays blank. */
  setTimeout(function () {
    for (var k = 0; k < nodes.length; k++) {
      if (!nodes[k].classList.contains('is-visible')) {
        nodes[k].classList.add('is-visible');
      }
    }
  }, 1200);
})();
