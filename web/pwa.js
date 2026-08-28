"use strict";

// Register and refresh the offline service worker.

const UPDATE_CHECK_INTERVAL_MS = 60 * 60 * 1000;

/** @type {(registration: ServiceWorkerRegistration) => void} */
function watchForUpdates(registration) {
    let last_check = Date.now();
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState !== 'visible') return;
        if (Date.now() - last_check < UPDATE_CHECK_INTERVAL_MS) return;
        last_check = Date.now();
        registration.update().catch(() => { });
    });
}

// A worker that skipped the wait takes charge of this page the moment it
// activates, which leaves the running script.js paired with an engine it was
// not built against: the assist is rebuilt from ai-worker.js on every move, so
// the next one would be planned by the new version and resolved by the old.
// Reloading puts the whole page back on one version, and the game survives it
// -- the board is in a cookie.
function reloadWhenANewWorkerTakesOver() {
    // A first visit has no worker to be replaced. The one being registered
    // claims a page that never had one, and that handover is not a new version
    // arriving, so it is watched for and let past rather than reloaded on --
    // which also leaves this page covered if a deploy lands while it is still
    // open.
    let controlled = navigator.serviceWorker.controller !== null;
    let reloading = false;
    navigator.serviceWorker.addEventListener('controllerchange', () => {
        if (!controlled) {
            controlled = true;
            return;
        }
        // reload() is a request, not a jump: the page goes on running, and
        // events already queued behind this one still arrive.
        if (reloading) return;
        reloading = true;
        window.location.reload();
    });
}

function register() {
    navigator.serviceWorker.register(new URL('../sw.js', import.meta.url))
        .then(watchForUpdates)
        .catch(() => { });
}

function registerServiceWorker() {
    if (!('serviceWorker' in navigator)) return;
    // Before register(), so the listener is up ahead of any handover
    // registering the worker sets off.
    reloadWhenANewWorkerTakesOver();
    if (document.readyState === 'complete') {
        register();
    } else {
        window.addEventListener('load', register, { once: true });
    }
}

export { registerServiceWorker };
