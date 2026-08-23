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

function register() {
    navigator.serviceWorker.register(new URL('../sw.js', import.meta.url))
        .then(watchForUpdates)
        .catch(() => { });
}

function registerServiceWorker() {
    if (!('serviceWorker' in navigator)) return;
    if (document.readyState === 'complete') {
        register();
    } else {
        window.addEventListener('load', register, { once: true });
    }
}

export { registerServiceWorker };
