// Loads soh.otr (shipped) and oot.otr (picked once by the user, kept in this browser only), then starts the game.
(function () {
  var logEl = document.getElementById('log');
  function log(m) { logEl.textContent = m; }
  var DB = 'oot-royale-web', STORE = 'files';
  var VER = 'x'; // this build's number, from version.txt (changes with every build, so cached files are never reused)

  function fetchVersion() {
    return fetch('version.txt', { cache: 'no-store' }).then(function (r) { return r.ok ? r.text() : 'x'; })
      .then(function (t) { return t.trim() || 'x'; }).catch(function () { return 'x'; });
  }
  // While playing, check now and then; if a newer build is up, offer a one-tap reload (oot.otr stays saved).
  function watchForUpdates() {
    setInterval(function () {
      fetchVersion().then(function (v) {
        if (v !== 'x' && v !== VER && !document.getElementById('upd')) {
          var b = document.createElement('div');
          b.id = 'upd';
          b.textContent = 'New build ' + v + ' ready: tap to update';
          b.style.cssText = 'position:fixed;top:8px;left:50%;transform:translateX(-50%);z-index:9;background:#2a6;color:#fff;padding:10px 16px;border-radius:12px;font:600 15px system-ui';
          b.onclick = function () { location.reload(); };
          document.body.appendChild(b);
        }
      });
    }, 120000);
  }

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
    return fetch('soh.wasm.gz?v=' + VER).then(function (r) {
      if (!r.ok) throw new Error('missing soh.wasm.gz');
      return new Response(r.body.pipeThrough(new DecompressionStream('gzip'))).arrayBuffer();
    }).catch(function () {
      return fetch('soh.wasm?v=' + VER).then(function (r) { return r.arrayBuffer(); });
    });
  }

  function start(ootBytes) {
    watchForUpdates();
    document.getElementById('boot').style.display = 'none';
    document.getElementById('pad').style.display = 'block';
    Promise.all([fetch('soh.otr?v=' + VER).then(function (r) { return r.arrayBuffer(); }), loadWasm()]).then(function (parts) {
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
      s.src = 'soh.js?v=' + VER;
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

  fetchVersion().then(function (v) {
    VER = v;
    document.getElementById('title').textContent = 'OOT Royale test build ' + v;
    try { localStorage.setItem('seen-build', v); } catch (e) {}
    return idbGet('oot.otr');
  }).then(function (b) {
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
