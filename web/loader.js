// Loads soh.otr (shipped) and oot.otr (picked once by the user, kept in this browser only), then starts the game.
(function () {
  var logEl = document.getElementById('log');
  function log(m) { logEl.textContent = m; }
  var DB = 'oot-royale-web', STORE = 'files';

  function db() {
    return new Promise(function (res, rej) {
      var r = indexedDB.open(DB, 1);
      r.onupgradeneeded = function () { r.result.createObjectStore(STORE); };
      r.onsuccess = function () { res(r.result); };
      r.onerror = function () { rej(r.error); };
    });
  }
  function idbGet(k) {
    return db().then(function (d) {
      return new Promise(function (res, rej) {
        var q = d.transaction(STORE).objectStore(STORE).get(k);
        q.onsuccess = function () { res(q.result); };
        q.onerror = function () { rej(q.error); };
      });
    });
  }
  function idbPut(k, v) {
    return db().then(function (d) {
      return new Promise(function (res, rej) {
        var t = d.transaction(STORE, 'readwrite');
        t.objectStore(STORE).put(v, k);
        t.oncomplete = res;
        t.onerror = function () { rej(t.error); };
      });
    });
  }

  // The game binary is shipped gzipped (the host caps single files at 25 MB); unpack it in the browser.
  function loadWasm() {
    log('Loading game...');
    return fetch('soh.wasm.gz').then(function (r) {
      if (!r.ok) throw new Error('missing soh.wasm.gz');
      return new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
    }).catch(function () {
      return fetch('soh.wasm').then(function (r) { return r.arrayBuffer(); });
    });
  }

  function start(ootBytes) {
    document.getElementById('boot').style.display = 'none';
    document.getElementById('pad').style.display = 'block';
    Promise.all([fetch('soh.otr').then(function (r) { return r.arrayBuffer(); }), loadWasm()]).then(function (parts) {
      var sohBytes = parts[0];
      var wasmUrl = URL.createObjectURL(new Blob([parts[1]], { type: 'application/wasm' }));
      var canvas = document.getElementById('canvas');
      window.Module = {
        canvas: canvas,
        noInitialRun: true,
        locateFile: function (p) { return p.endsWith('.wasm') ? wasmUrl : p; },
        print: function (t) { console.log(t); },
        printErr: function (t) { console.warn(t); },
        preRun: [function () {
          Module.FS.writeFile('/oot.otr', new Uint8Array(ootBytes));
          Module.FS.writeFile('/soh.otr', new Uint8Array(sohBytes));
          Module.FS.chdir('/');
        }],
        onRuntimeInitialized: function () {
          window.__sohReady = true;
          try { Module.callMain([]); } catch (e) { if (e !== 'unwind') console.error(e); }
        }
      };
      var s = document.createElement('script');
      s.src = 'soh.js';
      document.body.appendChild(s);
    });
  }

  document.getElementById('file').addEventListener('change', function (e) {
    var f = e.target.files[0];
    if (!f) return;
    log('Reading ' + f.name + '...');
    f.arrayBuffer().then(function (b) {
      return idbPut('oot.otr', b).then(function () { start(b); });
    });
  });

  idbGet('oot.otr').then(function (b) {
    if (b) {
      document.getElementById('pick').hidden = true;
      var go = document.getElementById('go');
      go.hidden = false;
      go.onclick = function () { start(b); };
      log('oot.otr is saved on this device.');
      if (location.hash === '#auto') start(b);
    } else {
      log('Pick your oot.otr once. It stays on this device and is never uploaded.');
    }
  }).catch(function () { log('Browser storage unavailable. Pick oot.otr each time.'); });

  window.__sohStartWith = start; // for automated tests
})();
