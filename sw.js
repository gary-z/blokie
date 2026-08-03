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
// test/web/pwa-test.js checks this list against what is on disk and against
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

self.addEventListener('install', (event) => {
    event.waitUntil((async () => {
        const cache = await caches.open(CACHE_NAME);
        // Past the HTTP cache: Pages serves these with a lifetime of its own,
        // and a worker that filled a new cache from stale copies would install
        // the version it was meant to replace.
        await cache.addAll(PRECACHE.map(path => new Request(path, { cache: 'reload' })));
    })());
});

self.addEventListener('activate', (event) => {
    event.waitUntil((async () => {
        const names = await caches.keys();
        await Promise.all(names
            .filter(name => name.startsWith('blokie-') && name !== CACHE_NAME)
            .map(name => caches.delete(name)));
        // Takes charge of the page that registered it, which otherwise plays
        // its first visit uncontrolled -- and so with nothing cached if it is
        // closed before it is ever loaded again.
        await self.clients.claim();
    })());
});

// Nothing calls skipWaiting: a new worker waits for the old app to be closed
// rather than swapping the files under a game in progress. The cost is a
// player kept on the previous version until they close the tab, and closing it
// is how anyone leaves a game anyway.

async function serveFromCache(request, key) {
    const cache = await caches.open(CACHE_NAME);
    const cached = await cache.match(key);
    if (cached !== undefined) return cached;
    // Not precached, or dropped by a browser short of room. Either way the
    // network is the only place left to ask.
    return fetch(request);
}

self.addEventListener('fetch', (event) => {
    const request = event.request;
    if (request.method !== 'GET') return;

    // Analytics and anything else off-site is left to the browser, which knows
    // to let it fail quietly when there is no network.
    if (new URL(request.url).origin !== self.location.origin) return;

    // Every address in scope is the same single page, so a navigation is
    // answered with the shell whatever it asked for.
    const key = request.mode === 'navigate' ? SHELL : request;
    event.respondWith(serveFromCache(request, key));
});
