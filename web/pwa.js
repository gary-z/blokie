"use strict";

// Signs the page up to the service worker in sw.js, which is what turns the
// site into something a phone will install and run with no network. Nothing on
// screen waits on any of this: a browser that refuses, or has never heard of
// service workers, gets exactly the game it got before.

// A browser looks for a new worker on its own when the page is navigated to,
// which for an app opened from a home screen and left open can be a long time
// between. Coming back to it is the other moment worth checking, at a spacing
// where a player switching apps all afternoon still only costs one request.
const UPDATE_CHECK_INTERVAL_MS = 60 * 60 * 1000;

function watchForUpdates(registration) {
    let last_check = Date.now();
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState !== 'visible') return;
        if (Date.now() - last_check < UPDATE_CHECK_INTERVAL_MS) return;
        last_check = Date.now();
        registration.update().catch(() => { });
    });
}

function register() {
    // Resolved against this file rather than the page, so it stays the worker
    // one directory up whoever imports it. That directory is also its scope,
    // which is the whole reason sw.js sits at the root instead of in here.
    navigator.serviceWorker.register(new URL('../sw.js', import.meta.url))
        .then(watchForUpdates)
        .catch(() => {
            // No offline, and no install prompt. Both are extras, and a game
            // that logged an error over them would be the only thing anyone
            // noticed. Opening the page off a file:// URL lands here.
        });
}

function registerServiceWorker() {
    if (!('serviceWorker' in navigator)) return;
    // Registering costs a request for the worker and another for everything it
    // precaches, all of it competing with the files the board is drawn from.
    // Waiting for the load event puts it behind them.
    if (document.readyState === 'complete') {
        register();
    } else {
        window.addEventListener('load', register, { once: true });
    }
}

export { registerServiceWorker };
