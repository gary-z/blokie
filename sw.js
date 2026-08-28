"use strict";

// The service worker behind the installed app: it keeps a copy of everything
// the game is made of, so a launch costs no network and a session survives
// losing one. The game itself already survives a refresh -- the board is in a
// cookie -- so all that was missing to play on a plane was the files.
//
// This sits at the root rather than in web/ with the rest of the app's assets,
// which is not a choice: a worker can only take charge of pages inside its own
// directory, so one served out of web/ could not control the index.html beside
// it. The Service-Worker-Allowed header would widen that, and GitHub Pages
// serves what is in the repository with no way to add headers to it.

// `self` in here is a service worker's scope, which is where clients.claim()
// and the install, activate and fetch events below come from. Nothing at
// runtime depends on this line -- it is the same object either way -- but it is
// what lets the type checker tell an event this worker really receives from one
// it never will. tsconfig.worker.json is what checks this file.
/** @type {ServiceWorkerGlobalScope & typeof globalThis} */
const worker = /** @type {any} */ (self);

// Substituted for a digest of the deployed files by the Pages workflow. It is
// what makes a deploy reach a player who already has the app: the browser
// reinstalls a worker whose bytes have changed, and a changed digest means a
// fresh cache filled from the network and the old one dropped. Left as the
// placeholder when the site is served straight out of a checkout, where the
// files move around under it and devtools' "Update on reload" is the answer.
const BUILD_ID = '__BUILD_ID__';
const CACHE_NAME = `blokie-${BUILD_ID}`;

// The page itself, under the address it is actually visited at rather than
// index.html, since that is what a navigation asks for.
const SHELL = './';

// Everything needed to play a full game with nothing behind it. The sounds are
// in here even though they are only fetched once sound is turned on: they are
// 68KB the once, against a player who turns sound on offline getting silence.
// web/tests/pwa-test.js checks this list against what is on disk and against
// what the workflow stages, since a single 404 fails the install as a whole
// and takes offline down with it.
const PRECACHE = [
    SHELL,
    './web/styles.css',
    './web/script.js',
    './web/storage.js',
    './web/sfx.js',
    './web/pwa.js',
    './web/ai-worker.js',
    './web/favicon.ico',
    './web/manifest.webmanifest',
    './web/icons/icon-192.png',
    './web/icons/icon-512.png',
    './web/icons/icon-maskable-512.png',
    './web/icons/apple-touch-icon.png',
    './web/sfx/impactSoft_medium_000.wav',
    './web/sfx/impactWood_light_000.wav',
    './web/sfx/impactWood_medium_000.wav',
    './engine/js/blokie.js',
    './engine/wasm/blokie-solver.js',
    './engine/wasm/blokie-solver.wasm',
];

worker.addEventListener('install', (event) => {
    event.waitUntil((async () => {
        const cache = await caches.open(CACHE_NAME);
        // Past the HTTP cache: Pages serves these with a lifetime of its own,
        // and a worker that filled a new cache from stale copies would install
        // the version it was meant to replace.
        await cache.addAll(PRECACHE.map(path => new Request(path, { cache: 'reload' })));
        // Take charge as soon as the new files are all in hand, instead of
        // waiting for every tab to be closed. Last, and inside the waitUntil,
        // so a failed addAll leaves the old worker serving a cache that is
        // whole rather than promoting one that is missing a file.
        await worker.skipWaiting();
    })());
});

worker.addEventListener('activate', (event) => {
    event.waitUntil((async () => {
        const names = await caches.keys();
        await Promise.all(names
            .filter(name => name.startsWith('blokie-') && name !== CACHE_NAME)
            .map(name => caches.delete(name)));
        // Takes charge of the page that registered it, which otherwise plays
        // its first visit uncontrolled -- and so with nothing cached if it is
        // closed before it is ever loaded again.
        await worker.clients.claim();
    })());
});

// skipWaiting is only half of it. Taking charge of a page that is already
// running leaves the script.js loaded from the old version driving whatever
// the new one serves, and the assist is rebuilt from ai-worker.js on every
// move -- so the next move would be planned by one version of the engine and
// resolved by another. web/pwa.js reloads the page to close that gap, and the
// two belong together: skipping the wait without the reload is the one
// arrangement that can hand a game a plan it cannot read.
//
// What makes a reload cheap enough to do under a game in progress is that the
// board is in a cookie, so the game comes back on the other side of it. A save
// the new version no longer reads is the one thing that does not survive,
// which is what SAVE_VERSION in web/storage.js is there to catch.

// `key` is what the cache is looked up under, which is not always the request
// itself: a navigation to any address inside the scope is answered with the
// shell, and so looked up under that path instead.
/** @type {(request: Request, key: RequestInfo) => Promise<Response>} */
async function serveFromCache(request, key) {
    const cache = await caches.open(CACHE_NAME);
    const cached = await cache.match(key);
    if (cached !== undefined) return cached;
    // Not precached, or dropped by a browser short of room. Either way the
    // network is the only place left to ask.
    return fetch(request);
}

worker.addEventListener('fetch', (event) => {
    const request = event.request;
    if (request.method !== 'GET') return;

    // Analytics and anything else off-site is left to the browser, which knows
    // to let it fail quietly when there is no network.
    if (new URL(request.url).origin !== worker.location.origin) return;

    // Every address in scope is the same single page, so a navigation is
    // answered with the shell whatever it asked for.
    const key = request.mode === 'navigate' ? SHELL : request;
    event.respondWith(serveFromCache(request, key));
});
