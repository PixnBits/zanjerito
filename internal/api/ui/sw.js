const CACHE = "zanjerito-shell-v1";
const SHELL = [
  "/",
  "/manifest.webmanifest",
  "/icons/icon-192.png",
  "/icons/icon-512.png",
  "/icons/icon-512-maskable.png",
  "/icons/apple-touch-icon.png",
  "/icons/icon.svg",
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(CACHE).then((cache) => cache.addAll(SHELL)).then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((key) => key !== CACHE).map((key) => caches.delete(key)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET") return;
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return;
  if (url.pathname.startsWith("/api/")) return;
  const accept = req.headers.get("Accept") || "";
  if (accept.indexOf("text/event-stream") !== -1) return;

  const path = url.pathname;
  const isNav = req.mode === "navigate" || path === "/" || path === "/index.html";
  if (isNav) {
    event.respondWith(networkFirst(req));
    return;
  }
  if (SHELL.indexOf(path) !== -1) {
    event.respondWith(cacheFirst(req, path));
  }
});

function networkFirst(req) {
  return fetch(req).then((res) => {
    if (res && res.ok) {
      const copy = res.clone();
      caches.open(CACHE).then((cache) => cache.put("/", copy));
    }
    return res;
  }).catch(() => caches.match("/").then((hit) => hit || caches.match(req)));
}

function cacheFirst(req, path) {
  return caches.match(req).then((hit) => {
    const fetched = fetch(req).then((res) => {
      if (res && res.ok && SHELL.indexOf(path) !== -1) {
        const copy = res.clone();
        caches.open(CACHE).then((cache) => cache.put(req, copy));
      }
      return res;
    });
    if (hit) {
      fetched.catch(() => {});
      return hit;
    }
    return fetched;
  });
}
