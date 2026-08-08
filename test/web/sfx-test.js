"use strict";

// Plays web/sfx.js against an AudioContext that behaves like WebKit's, which
// is the only one where any of this is hard: a context only starts or resumes
// while a user gesture is being handled, and iOS takes the audio session away
// -- leaving the context 'interrupted' -- whenever the app goes to the
// background.
//
// That combination is what the sound was reported broken by. An app opened
// from the home screen launches with the setting already on and comes back
// from the app switcher to the page it left, so the two moments a browser tab
// mostly skips -- a launch with no button press coming, and a return holding a
// dead context -- are its normal ones. Both are played out below.
//
// Everything the module touches is faked here rather than run in a browser,
// which is the only way to get at 'interrupted' at all: no automation can put
// a real context into it.

import assert from 'node:assert';

let failures = 0;
function check(condition, description) {
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return;
    }
    console.log("ok - %s", description);
}

// Whether a user gesture is being handled right now. WebKit hangs everything
// on this: a context built outside one starts suspended, and resume() outside
// one leaves it where it was.
let in_gesture = false;

// Set for the run that models a context iOS never gives back, where resuming
// is not the answer and building another one is.
let never_resumes = false;

const contexts = [];

class FakeAudioContext {
    constructor() {
        this.state = in_gesture ? 'running' : 'suspended';
        this.currentTime = 0;
        this.started = [];
        this.destination = { name: 'destination' };
        contexts.push(this);
    }

    // The promise settles after the state has moved, as the specification
    // requires, so a caller can read the state to find out whether it worked.
    resume() {
        if (this.state === 'closed') return Promise.reject(new Error('closed'));
        if (in_gesture && !never_resumes) this.state = 'running';
        return Promise.resolve();
    }

    close() {
        this.state = 'closed';
        return Promise.resolve();
    }

    decodeAudioData(bytes) {
        if (this.state === 'closed') return Promise.reject(new Error('closed'));
        return Promise.resolve({ decoded_from: bytes.byteLength });
    }

    createBufferSource() {
        const context = this;
        return {
            buffer: null,
            playbackRate: { value: 1 },
            connect: (node) => node,
            start: (when) => context.started.push(when),
        };
    }

    createGain() {
        return { gain: { value: 1 }, connect: (node) => node };
    }

    // Not an API: what iOS does to a context when the app leaves the
    // foreground, or a call arrives, or another app starts playing.
    interrupt() {
        if (this.state === 'running') this.state = 'interrupted';
    }
}

function current() {
    return contexts[contexts.length - 1];
}

// ------------------------------------------------------------- a fake page

const listeners = new Map();

function addListener(type, handler) {
    if (!listeners.has(type)) listeners.set(type, []);
    listeners.get(type).push(handler);
}

function removeListener(type, handler) {
    const registered = listeners.get(type);
    if (registered === undefined) return;
    const at = registered.indexOf(handler);
    if (at >= 0) registered.splice(at, 1);
}

function listenerCount(type) {
    return (listeners.get(type) || []).length;
}

// Dispatched with the gesture flag set, since these are the events a browser
// would only give us off a real touch.
const GESTURES = new Set(['pointerdown', 'pointerup', 'touchend', 'click', 'keydown']);

function dispatch(type) {
    const was = in_gesture;
    if (GESTURES.has(type)) in_gesture = true;
    for (const handler of [...(listeners.get(type) || [])]) handler({ type });
    in_gesture = was;
}

let visibility = 'visible';
let cookie = '';
let fetches = 0;

globalThis.window = {
    AudioContext: FakeAudioContext,
    location: { protocol: 'https:' },
    addEventListener: addListener,
    removeEventListener: removeListener,
};
globalThis.document = {
    addEventListener: addListener,
    removeEventListener: removeListener,
    get visibilityState() { return visibility; },
    get cookie() { return cookie; },
    set cookie(value) { cookie = value.split(';')[0]; },
};
// Set to fail every request, standing in for a phone that has lost the network
// on the way into a cold launch.
let offline = false;

globalThis.fetch = () => {
    fetches++;
    if (offline) return Promise.reject(new TypeError('Load failed'));
    return Promise.resolve({ ok: true, arrayBuffer: () => Promise.resolve(new ArrayBuffer(64)) });
};

function fakeButton() {
    return {
        querySelector: () => ({ textContent: '' }),
        classList: { toggle: () => { } },
        setAttribute: () => { },
        addEventListener: () => { },
        title: '',
    };
}

// Let the fetch and decode chains run out. Three turns covers the deepest of
// them (fetch -> decode -> store).
async function settle() {
    for (let i = 0; i < 5; ++i) await Promise.resolve();
    await new Promise(resolve => setTimeout(resolve, 0));
}

// The setting is already on before the module loads, which is how an installed
// app starts every time after the first.
cookie = 'blokie_sfx=on';

// Imported after the globals are in place: the module reads window.AudioContext
// as it loads.
const { initSfx, playSfx } = await import('../../web/sfx.js');

// ------------------------------------------- a launch with sound already on

// Straight into a lost network, which the installed app is the most exposed
// to: the clips are fetched at launch off the restored setting rather than off
// a button press, so nobody is watching when the request fails. On Chrome for
// iOS there is no service worker to answer from a cache either.
offline = true;

initSfx(fakeButton());
await settle();

check(contexts.length === 0, 'a launch builds no context of its own');
check(listenerCount('pointerdown') > 0, 'it waits for a gesture instead');
check(fetches > 0, 'and starts fetching the clips');

// The first touch of the launch. Under the old code this was the only attempt
// there would ever be.
dispatch('pointerdown');
await settle();

check(contexts.length === 1, 'the first gesture builds the context');
check(current().state === 'running', 'and it comes up running');

playSfx('place');
await settle();
check(current().started.length === 0, 'a clip that never arrived plays nothing');

// The next sound that wants the clip is what asks for it again: the context is
// running by now, so there are no gesture listeners left to hang this on.
const failed_fetches = fetches;
offline = false;
playSfx('place');
await settle();

check(fetches > failed_fetches, 'a lost clip is asked for again by the next sound');
check(contexts.length === 1, 'and losing it was not mistaken for a broken decoder');

const clips_fetched = fetches;
playSfx('place');
check(current().started.length === 1, 'a sound plays');
check(fetches === clips_fetched, 'without fetching anything again');

// ------------------------------------------ away to the app switcher and back

const first_context = current();
first_context.interrupt();
visibility = 'hidden';
dispatch('visibilitychange');

playSfx('place');
check(first_context.started.length === 1, 'nothing is scheduled onto an interrupted context');

visibility = 'visible';
dispatch('visibilitychange');
await settle();

check(first_context.state === 'interrupted',
    'coming back outside a gesture cannot resume it on its own');
check(listenerCount('pointerdown') > 0, 'so a gesture is armed again');

dispatch('pointerdown');
await settle();

check(current().state === 'running', 'and the next touch brings the sound back');

const before = current().started.length;
playSfx('clear');
check(current().started.length === before + 3, 'the clear plays all three of its hits');

// ------------------------------------------------ a context iOS never returns

// This time the session goes away without the app going anywhere -- a call, or
// another app starting to play -- so there is no visibility change to notice
// it. What notices is the next sound that tries to play and finds the context
// stopped, which is what has to put the gesture listeners back.
const second_context = current();
const contexts_before = contexts.length;
second_context.interrupt();
never_resumes = true;

check(listenerCount('pointerdown') === 0, 'nothing is listening while sound is working');
playSfx('place');
await settle();
check(listenerCount('pointerdown') > 0, 'a sound that finds the context stopped arms a gesture');

dispatch('pointerdown');
await settle();
check(contexts.length === contexts_before, 'a first failed attempt only asks the context it has');

// The failure is known by now, so this gesture replaces it.
const fetched_before_replacement = fetches;
never_resumes = false;
dispatch('pointerdown');
await settle();

check(contexts.length === contexts_before + 1, 'a context that will not resume is replaced');
check(second_context.state === 'closed', 'and the dead one is closed');
check(current().state === 'running', 'the replacement runs');
check(fetches === fetched_before_replacement,
    'the decoded clips outlive their context, so nothing is fetched twice');

playSfx('place');
check(current().started.length === 1, 'and sound plays out of the new context');

// Once it is running, the page is not left listening for a gesture it no
// longer needs.
check(listenerCount('pointerdown') === 0, 'the unlock listeners are removed again');
check(listenerCount('touchend') === 0, 'all of them');

assert(failures === 0, `${failures} check(s) failed`);
console.log('\nAll sound effect checks passed.');
