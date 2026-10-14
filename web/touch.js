// On-screen controls. They send keyboard events, which the game already maps by default.
(function () {
  var pad = document.getElementById('pad');
  var KEYS = {
    up: ['w', 87], down: ['s', 83], left: ['a', 65], right: ['d', 68],
    A: ['x', 88], B: ['c', 67], Z: ['z', 90], R: ['r', 82], L: ['e', 69], Start: [' ', 32],
    cl: ['ArrowLeft', 37], cr: ['ArrowRight', 39], cu: ['ArrowUp', 38], cd: ['ArrowDown', 40]
  };
  function fire(type, k) {
    var d = KEYS[k];
    var e = new KeyboardEvent(type, { key: d[0], code: d[0] === ' ' ? 'Space' : d[0].length === 1 ? 'Key' + d[0].toUpperCase() : d[0], keyCode: d[1], which: d[1], bubbles: true });
    document.dispatchEvent(e);
  }
  function button(label, k, x, y, size) {
    var b = document.createElement('div');
    b.className = 'b';
    b.textContent = label;
    b.style.cssText = 'right:' + x + 'px;bottom:' + y + 'px;' + (size ? 'width:' + size + 'px;height:' + size + 'px;' : '');
    b.addEventListener('touchstart', function (e) { e.preventDefault(); fire('keydown', k); }, { passive: false });
    b.addEventListener('touchend', function (e) { e.preventDefault(); fire('keyup', k); }, { passive: false });
    b.addEventListener('touchcancel', function () { fire('keyup', k); });
    pad.appendChild(b);
  }
  button('A', 'A', 24, 90, 76);
  button('B', 'B', 110, 60);
  button('Z', 'Z', 24, 190, 56);
  button('R', 'R', 100, 160, 56);
  button('Start', 'Start', 24, 270, 56);
  button('C←', 'cl', 190, 100, 48);
  button('C→', 'cr', 130, 230, 48);
  button('C↑', 'cu', 190, 160, 48);
  button('C↓', 'cd', 250, 100, 48);

  // Move stick: touch drag becomes the four arrow keys.
  var stick = document.createElement('div'); stick.id = 'stick';
  var nub = document.createElement('div'); nub.id = 'nub'; stick.appendChild(nub);
  pad.appendChild(stick);
  var held = {};
  function setKey(k, on) { if (!!held[k] !== on) { held[k] = on; fire(on ? 'keydown' : 'keyup', k); } }
  function move(e) {
    e.preventDefault();
    var t = e.touches[0], r = stick.getBoundingClientRect();
    var dx = t.clientX - (r.left + r.width / 2), dy = t.clientY - (r.top + r.height / 2);
    var m = Math.min(1, Math.hypot(dx, dy) / 60) || 0, a = Math.atan2(dy, dx);
    nub.style.transform = 'translate(' + Math.cos(a) * m * 45 + 'px,' + Math.sin(a) * m * 45 + 'px)';
    var dead = 14;
    setKey('left', dx < -dead); setKey('right', dx > dead); setKey('up', dy < -dead); setKey('down', dy > dead);
  }
  stick.addEventListener('touchstart', move, { passive: false });
  stick.addEventListener('touchmove', move, { passive: false });
  function rel() { nub.style.transform = ''; ['left', 'right', 'up', 'down'].forEach(function (k) { setKey(k, false); }); }
  stick.addEventListener('touchend', rel); stick.addEventListener('touchcancel', rel);
})();
