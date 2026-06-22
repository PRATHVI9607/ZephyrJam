/* JamShield PWA service worker - offline app shell. */
var CACHE = "jamshield-v4";
var ASSETS = ["./", "./index.html", "./app.js", "./mqtt.min.js",
              "./manifest.webmanifest", "./icon.svg"];

self.addEventListener("install", function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) {
    return Promise.all(ASSETS.map(function (a) {
      return c.add(a).catch(function () {});  // tolerate a missing asset
    }));
  }));
  self.skipWaiting();
});

self.addEventListener("activate", function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k !== CACHE; })
                           .map(function (k) { return caches.delete(k); }));
  }));
  self.clients.claim();
});

// Network-first: always serve the freshest code when online; cache is only an
// offline fallback. Avoids the stale-app trap for a live dashboard.
self.addEventListener("fetch", function (e) {
  if (e.request.method !== "GET") return;
  e.respondWith(
    fetch(e.request).then(function (resp) {
      var cp = resp.clone();
      caches.open(CACHE).then(function (c) { c.put(e.request, cp); });
      return resp;
    }).catch(function () {
      return caches.match(e.request).then(function (r) { return r || caches.match("./index.html"); });
    })
  );
});
