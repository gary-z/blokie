"use strict";

// Sounds for playing by hand: a piece lifted off the deck, dropped onto the
// board, refusing to seat, and a row coming apart.
//
// Every clip is Kenney's, from the CC0 Impact Sounds pack. See
// web/sfx/README.md for where they came from and what was done to them.
//
// Only manual play makes any sound: these fire from the drag handlers, and the
// assist places pieces without touching those.

const PICKUP = 'impactWood_light_000';

// A semitone as a playback-rate multiplier: a clip played at STEP ** 4 comes
// out a major third higher, and a little shorter with it.
const STEP = 2 ** (1 / 12);

// Each event is a list of hits, so the clear can be a run rather than one clip:
// the pickup struck three times up a major triad, 70ms apart and tapering, a
// small marimba in the same piece of wood as everything else. The hits are
// spaced enough that they sum to -1 dBFS instead of clipping.
const SOUNDS = {
    pickup: [{ file: PICKUP, rate: 1, at: 0, gain: 1 }],
    place: [{ file: 'impactWood_medium_000', rate: 1, at: 0, gain: 1 }],
    reject: [{ file: 'impactSoft_medium_000', rate: 1, at: 0, gain: 1 }],
    clear: [
        { file: PICKUP, rate: STEP ** 0, at: 0.00, gain: 1.0 },
        { file: PICKUP, rate: STEP ** 4, at: 0.07, gain: 0.9 },
        { file: PICKUP, rate: STEP ** 7, at: 0.14, gain: 0.8 },
    ],
};

// The clips are all normalized to one level, so balance between the events
// lives here instead of in the files. Picking a piece up happens on every drag
// and wants to sit under the rest of it.
const EVENT_GAIN = { pickup: 0.45, place: 0.7, reject: 0.6, clear: 0.9 };

// Cleared cells shrink out over 0.2s. Landing the sound just after that starts
// reads as the clear causing it, rather than as part of the placement.
const CLEAR_DELAY_S = 0.06;

const AudioContextClass = window.AudioContext || window.webkitAudioContext;

let audio_ctx = null;
const fetched = new Map();   // file name -> Promise<ArrayBuffer>, until decoded
const decoded = new Map();   // file name -> AudioBuffer, once it is ready
let decode_failed = false;

const CLIPS = [...new Set(Object.values(SOUNDS).flat().map(h => h.file))];

function clipUrl(name) {
    return new URL(`sfx/${name}.wav`, import.meta.url);
}

// Safe to call on any user gesture, and cheap after the first one. An
// AudioContext built before one starts out suspended (and complains in the
// console), so the bytes are fetched up front and decoded once a gesture has
// actually arrived.
function warmUp() {
    if (decode_failed) return;
    if (audio_ctx === null) {
        audio_ctx = new AudioContextClass();
    }
    if (audio_ctx.state === 'suspended') {
        audio_ctx.resume();
    }
    for (const name of CLIPS) {
        if (!fetched.has(name)) continue;
        // Claim the slot before awaiting: decodeAudioData detaches the buffer
        // it is given, so the same bytes must not be decoded twice.
        const bytes = fetched.get(name);
        fetched.delete(name);
        bytes
            .then(b => audio_ctx.decodeAudioData(b))
            .then(buffer => decoded.set(name, buffer))
            .catch(() => {
                // Silence beats a broken game.
                decode_failed = true;
                console.warn('blokie: sound effects are off, this browser could not decode web/sfx/*.wav');
            });
    }
}

function playSfx(event) {
    if (audio_ctx === null) return;
    const hits = SOUNDS[event];
    // Still decoding, which only happens for the first sound of a session.
    if (!hits.every(h => decoded.has(h.file))) {
        warmUp();
        return;
    }
    if (audio_ctx.state === 'suspended') {
        audio_ctx.resume();
    }

    const base = audio_ctx.currentTime + (event === 'clear' ? CLEAR_DELAY_S : 0);
    for (const hit of hits) {
        const source = audio_ctx.createBufferSource();
        source.buffer = decoded.get(hit.file);
        source.playbackRate.value = hit.rate;
        const gain = audio_ctx.createGain();
        gain.gain.value = EVENT_GAIN[event] * hit.gain;
        source.connect(gain).connect(audio_ctx.destination);
        source.start(base + hit.at);
    }
}

function initSfx() {
    for (const name of CLIPS) {
        fetched.set(name, fetch(clipUrl(name)).then(r => r.arrayBuffer()));
    }
    // The first gesture anywhere pays for the decode, so the first sound the
    // game actually asks for is ready by the time it is asked for.
    document.addEventListener('pointerdown', warmUp, { once: true });
}

export { initSfx, playSfx };
