/* Spatial navigation for DualSense / D-pad / keyboard (Orbit-style 10-foot UI). */
(function (global) {
  "use strict";

  var KEY = {
    LEFT: ["ArrowLeft", "Left"],
    RIGHT: ["ArrowRight", "Right"],
    UP: ["ArrowUp", "Up"],
    DOWN: ["ArrowDown", "Down"],
    ENTER: ["Enter", "NumpadEnter"],
    BACK: ["Escape", "Backspace"],
    TAB_PREV: ["KeyL", "PageUp"],
    TAB_NEXT: ["KeyR", "PageDown"],
    OPTIONS: ["KeyO", "F1"],
    PLAY: [" ", "MediaPlayPause", "KeyP"],
  };

  function visible(el) {
    if (!el || el.disabled) return false;
    if (el.getAttribute("data-nav") === "off") return false;
    var style = window.getComputedStyle(el);
    if (style.display === "none" || style.visibility === "hidden" || style.opacity === "0") return false;
    var rect = el.getBoundingClientRect();
    return rect.width > 0 && rect.height > 0;
  }

  function candidates(root) {
    root = root || document;
    return Array.prototype.filter.call(root.querySelectorAll("[data-nav]"), visible);
  }

  function center(el) {
    var r = el.getBoundingClientRect();
    return { x: r.left + r.width / 2, y: r.top + r.height / 2, r: r };
  }

  function scrollAncestors(el) {
    var node = el.parentElement;
    while (node && node !== document.body && node !== document.documentElement) {
      var style = window.getComputedStyle(node);
      var ox = /(auto|scroll)/.test(style.overflowX);
      var oy = /(auto|scroll)/.test(style.overflowY);
      if (ox || oy) {
        var er = el.getBoundingClientRect();
        var pr = node.getBoundingClientRect();
        var pad = 28;
        if (ox) {
          if (er.left < pr.left + pad) {
            node.scrollBy({ left: er.left - pr.left - pad, behavior: "smooth" });
          } else if (er.right > pr.right - pad) {
            node.scrollBy({ left: er.right - pr.right + pad, behavior: "smooth" });
          }
        }
        if (oy) {
          if (er.top < pr.top + pad) {
            node.scrollBy({ top: er.top - pr.top - pad, behavior: "smooth" });
          } else if (er.bottom > pr.bottom - pad) {
            node.scrollBy({ top: er.bottom - pr.bottom + pad, behavior: "smooth" });
          }
        }
      }
      node = node.parentElement;
    }
  }

  function focusEl(el, scroll) {
    if (!el) return;
    if (document.activeElement && document.activeElement !== el) {
      try {
        document.activeElement.blur();
      } catch (err) {}
    }
    el.focus({ preventScroll: true });
    if (scroll !== false) {
      scrollAncestors(el);
      el.scrollIntoView({ block: "nearest", inline: "nearest", behavior: "smooth" });
    }
  }

  function pick(dir, from, list) {
    var a = center(from);
    var best = null;
    var bestScore = Infinity;
    list.forEach(function (el) {
      if (el === from) return;
      var b = center(el);
      var dx = b.x - a.x;
      var dy = b.y - a.y;
      var aligned = 0;
      if (dir === "left" && dx < -8) aligned = 1;
      if (dir === "right" && dx > 8) aligned = 1;
      if (dir === "up" && dy < -8) aligned = 1;
      if (dir === "down" && dy > 8) aligned = 1;
      if (!aligned) return;
      var primary = dir === "left" || dir === "right" ? Math.abs(dx) : Math.abs(dy);
      var secondary = dir === "left" || dir === "right" ? Math.abs(dy) : Math.abs(dx);
      var overlap = 0;
      if (dir === "left" || dir === "right") {
        overlap = Math.max(0, Math.min(a.r.bottom, b.r.bottom) - Math.max(a.r.top, b.r.top));
      } else {
        overlap = Math.max(0, Math.min(a.r.right, b.r.right) - Math.max(a.r.left, b.r.left));
      }
      var score = primary + secondary * 3 - overlap * 2;
      if (score < bestScore) {
        bestScore = score;
        best = el;
      }
    });
    return best;
  }

  function current(root) {
    var active = document.activeElement;
    if (active && active.getAttribute && active.hasAttribute("data-nav") && visible(active)) return active;
    var marked = (root || document).querySelector("[data-nav].is-focused, [data-nav]:focus");
    if (marked && visible(marked)) return marked;
    return candidates(root)[0] || null;
  }

  function move(dir, root) {
    var list = candidates(root);
    if (!list.length) return null;
    var from = current(root) || list[0];
    var next = pick(dir, from, list) || from;
    focusEl(next, true);
    return next;
  }

  function first(root) {
    var el = candidates(root)[0];
    if (el) focusEl(el, true);
    return el;
  }

  function match(set, event) {
    return set.indexOf(event.key) >= 0 || set.indexOf(event.code) >= 0;
  }

  var padPrev = {};
  var padRepeatAt = {};
  function touchX(pad) {
    var list = pad.touches;
    if (!list || !list.length) return null;
    var pos = list[0].position;
    if (!pos || pos.length < 1) return null;
    var x = pos[0];
    if (x < 0) return 0;
    if (x > 1) return 1;
    return x;
  }
  function pollGamepad(onButton) {
    var pads = navigator.getGamepads ? navigator.getGamepads() : [];
    var now = Date.now();
    for (var i = 0; i < pads.length; i += 1) {
      var pad = pads[i];
      if (!pad) continue;
      var map = {
        12: "up",
        13: "down",
        14: "left",
        15: "right",
        0: "enter",
        1: "back",
        4: "tab-prev",
        5: "tab-next",
        9: "options",
      };
      for (var b = 0; b < pad.buttons.length; b += 1) {
        var pressed = !!(pad.buttons[b] && pad.buttons[b].pressed);
        var key = i + ":" + b;
        var edge = pressed && !padPrev[key];
        if (edge) onButton("activity");
        if (map[b] && edge) onButton(map[b]);
        if (map[b] && pressed && (b === 4 || b === 5)) {
          var stamp = padRepeatAt[key] || 0;
          if (!edge && now - stamp > 420) onButton(map[b], { repeat: true });
          if (edge || now - stamp > 420) padRepeatAt[key] = now;
        }
        padPrev[key] = pressed;
      }
      var x = touchX(pad);
      var touchKey = i + ":touch";
      if (x != null) {
        onButton("touch", x);
        padPrev[touchKey] = true;
      } else if (padPrev[touchKey]) {
        padPrev[touchKey] = false;
      }
    }
  }

  global.TvNav = {
    KEY: KEY,
    match: match,
    move: move,
    first: first,
    current: current,
    focus: focusEl,
    candidates: candidates,
    pollGamepad: pollGamepad,
  };
})(window);
