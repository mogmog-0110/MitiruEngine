/* playful-canvas.js — small particle demo for the home hero.
 * No deps, no module loader. ~16 particles, canvas2d. Auto-pauses when
 * the canvas leaves the viewport so it never burns cycles off-screen.
 * Honours prefers-reduced-motion (renders a single static frame).
 */
(function () {
  var canvas = document.getElementById('playful-canvas');
  if (!canvas || !canvas.getContext) return;

  var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
  var ctx = canvas.getContext('2d', { alpha: true });
  if (!ctx) return;

  // Logical size — kept fixed in attributes for SSR-friendly aspect ratio.
  var dpr = Math.max(1, Math.min(window.devicePixelRatio || 1, 2));
  var cssW = 0;
  var cssH = 0;
  var W = 0;
  var H = 0;

  function fitToBox() {
    var rect = canvas.getBoundingClientRect();
    cssW = Math.max(1, Math.floor(rect.width));
    cssH = Math.max(1, Math.floor(rect.height));
    W = cssW * dpr;
    H = cssH * dpr;
    if (canvas.width !== W) canvas.width = W;
    if (canvas.height !== H) canvas.height = H;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
  fitToBox();

  // Brand-ish palette. Pulled from the CSS variables so dark/light read well.
  var styles = getComputedStyle(document.documentElement);
  function readVar(name, fallback) {
    var v = styles.getPropertyValue(name).trim();
    return v || fallback;
  }
  var accent = readVar('--c-accent', '#2563eb');
  var accent2 = readVar('--c-accent-grad-2', '#0ea5e9');
  var soft = readVar('--c-fg-mute', '#8c95a3');

  var COUNT = 16;
  var particles = [];
  function rand(min, max) { return min + Math.random() * (max - min); }
  function reset(p) {
    p.x = rand(0, cssW);
    p.y = rand(0, cssH);
    p.vx = rand(-0.18, 0.18);
    p.vy = rand(-0.12, 0.12);
    p.r = rand(2.2, 4.6);
    p.hue = Math.random() < 0.5 ? accent : accent2;
    p.alpha = rand(0.35, 0.7);
  }
  for (var i = 0; i < COUNT; i++) {
    var p = {};
    reset(p);
    particles.push(p);
  }

  var pointer = { x: cssW * 0.5, y: cssH * 0.5, active: false };
  canvas.addEventListener('pointermove', function (e) {
    var rect = canvas.getBoundingClientRect();
    pointer.x = e.clientX - rect.left;
    pointer.y = e.clientY - rect.top;
    pointer.active = true;
  });
  canvas.addEventListener('pointerleave', function () {
    pointer.active = false;
  });
  canvas.addEventListener('click', function (e) {
    var rect = canvas.getBoundingClientRect();
    var cx = e.clientX - rect.left;
    var cy = e.clientY - rect.top;
    // Nudge every particle outward from the click point.
    for (var k = 0; k < particles.length; k++) {
      var q = particles[k];
      var dx = q.x - cx;
      var dy = q.y - cy;
      var d = Math.sqrt(dx * dx + dy * dy) || 1;
      q.vx += (dx / d) * 1.4;
      q.vy += (dy / d) * 1.4;
    }
  });

  function step() {
    ctx.clearRect(0, 0, cssW, cssH);

    // Optional subtle link lines between close particles.
    ctx.lineWidth = 1;
    ctx.strokeStyle = soft;
    ctx.globalAlpha = 0.12;
    for (var i = 0; i < particles.length; i++) {
      var a = particles[i];
      for (var j = i + 1; j < particles.length; j++) {
        var b = particles[j];
        var dx = a.x - b.x, dy = a.y - b.y;
        var d2 = dx * dx + dy * dy;
        if (d2 < 90 * 90) {
          ctx.beginPath();
          ctx.moveTo(a.x, a.y);
          ctx.lineTo(b.x, b.y);
          ctx.stroke();
        }
      }
    }

    // Particles.
    for (var n = 0; n < particles.length; n++) {
      var p = particles[n];
      // Pointer attraction (gentle).
      if (pointer.active) {
        var dxp = pointer.x - p.x;
        var dyp = pointer.y - p.y;
        var dp = Math.sqrt(dxp * dxp + dyp * dyp) || 1;
        if (dp < 140) {
          p.vx += (dxp / dp) * 0.025;
          p.vy += (dyp / dp) * 0.025;
        }
      }
      // Damping so velocities don't blow up.
      p.vx *= 0.97;
      p.vy *= 0.97;
      p.x += p.vx;
      p.y += p.vy;
      // Wrap to keep the field populated.
      if (p.x < -8) p.x = cssW + 8;
      if (p.x > cssW + 8) p.x = -8;
      if (p.y < -8) p.y = cssH + 8;
      if (p.y > cssH + 8) p.y = -8;

      ctx.globalAlpha = p.alpha;
      ctx.fillStyle = p.hue;
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.globalAlpha = 1;
  }

  // Visibility gating: only animate while the canvas is on-screen
  // and the document is visible. Saves significant battery on mobile.
  var visible = true;
  var running = false;
  var rafId = 0;

  function loop() {
    if (!visible || document.hidden) {
      running = false;
      return;
    }
    step();
    rafId = window.requestAnimationFrame(loop);
  }
  function start() {
    if (running) return;
    running = true;
    rafId = window.requestAnimationFrame(loop);
  }
  function stop() {
    running = false;
    if (rafId) window.cancelAnimationFrame(rafId);
  }

  if ('IntersectionObserver' in window) {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) {
        visible = e.isIntersecting;
        if (visible && !reduce) start(); else stop();
      });
    }, { threshold: 0.01 });
    io.observe(canvas);
  } else {
    visible = true;
  }

  document.addEventListener('visibilitychange', function () {
    if (document.hidden) stop(); else if (visible && !reduce) start();
  });

  window.addEventListener('resize', function () {
    fitToBox();
  });

  if (reduce) {
    // Render exactly one frame for users who don't want motion.
    fitToBox();
    step();
  } else {
    start();
  }
})();
