/* keyboard-shortcuts.js — lightweight global hotkeys.
 *  ?         — open the shortcuts help modal
 *  Esc       — close help modal
 *  g h       — go to /
 *  g f       — go to /features.html (機能リファレンス)
 *  g a       — go to /architecture/
 *  g t       — go to /tutorial.html (はじめてのゲーム)
 *  g s       — go to /install.html (インストール)
 * Ignored when the user is typing in an input/textarea or has a modifier
 * key held (so Cmd-?, Ctrl-G, etc. keep their browser meaning).
 */
(function () {
  var dialog = document.getElementById('shortcuts-dialog');
  var openers = document.querySelectorAll('[data-shortcuts-open]');
  var closers = dialog ? dialog.querySelectorAll('[data-shortcuts-close]') : [];

  // Read the site base path from <html data-base="..."> so a deploy under
  // /MitiruEngine/ keeps working. Falls back to "/".
  var base = (document.documentElement.getAttribute('data-base') || '/').replace(/\/+$/, '') + '/';
  function gotoPath(suffix) {
    var target = (base + suffix).replace(/\/+/g, '/');
    window.location.href = target;
  }

  function isTypingTarget(el) {
    if (!el) return false;
    var tag = (el.tagName || '').toLowerCase();
    if (tag === 'input' || tag === 'textarea' || tag === 'select') return true;
    if (el.isContentEditable) return true;
    return false;
  }

  var lastFocus = null;
  function openDialog() {
    if (!dialog) return;
    lastFocus = document.activeElement;
    dialog.hidden = false;
    requestAnimationFrame(function () {
      dialog.classList.add('is-open');
      var first = dialog.querySelector('[data-shortcuts-close]');
      if (first) first.focus();
    });
  }
  function closeDialog() {
    if (!dialog) return;
    dialog.classList.remove('is-open');
    setTimeout(function () {
      if (!dialog.classList.contains('is-open')) dialog.hidden = true;
      if (lastFocus && typeof lastFocus.focus === 'function') lastFocus.focus();
    }, 180);
  }
  if (dialog) {
    closers.forEach(function (el) {
      el.addEventListener('click', closeDialog);
    });
  }
  openers.forEach(function (el) {
    el.addEventListener('click', function (e) {
      e.preventDefault();
      openDialog();
    });
  });

  // "g <key>" sequence state. Resets after 1.2s or after a match.
  var gMode = false;
  var gTimer = 0;
  function armG() {
    gMode = true;
    if (gTimer) window.clearTimeout(gTimer);
    gTimer = window.setTimeout(function () { gMode = false; }, 1200);
  }
  function disarmG() {
    gMode = false;
    if (gTimer) window.clearTimeout(gTimer);
    gTimer = 0;
  }

  document.addEventListener('keydown', function (e) {
    if (e.defaultPrevented) return;
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    if (isTypingTarget(e.target)) return;

    // Close modal on Escape — but the search dialog has its own handler too.
    if (e.key === 'Escape' && dialog && dialog.classList.contains('is-open')) {
      e.preventDefault();
      closeDialog();
      return;
    }

    // "?" opens the help modal. Shift+/ on US layouts is "?".
    if (e.key === '?' || (e.key === '/' && e.shiftKey)) {
      e.preventDefault();
      if (dialog && dialog.classList.contains('is-open')) closeDialog(); else openDialog();
      return;
    }

    if (gMode) {
      var key = (e.key || '').toLowerCase();
      var routes = {
        h: '',
        f: 'features.html',
        a: 'architecture/',
        t: 'tutorial.html',
        s: 'install.html'
      };
      if (Object.prototype.hasOwnProperty.call(routes, key)) {
        e.preventDefault();
        disarmG();
        gotoPath(routes[key]);
        return;
      }
      disarmG();
      return;
    }

    if (e.key === 'g' || e.key === 'G') {
      armG();
    }
  });
})();
