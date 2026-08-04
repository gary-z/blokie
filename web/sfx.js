"use strict";
import { saveSfxSetting, loadSfxSetting } from "./storage.js";

// A piece lifted off the deck, dropped onto the board, refusing to seat, and a
// row coming apart. Played whether you or the assist is moving the pieces,
// except at the assist's top speed, where the moves come too fast to hear as
// anything but noise.
//
// Off until someone turns it on, and remembered after that. Nothing is even
// downloaded while it is off.
//
// Every clip is Kenney's, from the CC0 Impact Sounds pack. See
// web/sfx/README.md for where they came from and what was done to them.

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
//
// The reject is the exception to that normalization being enough: it levelled
// to -18 dB RMS before it reached -1 dB peak, and it is a third the length of
// the wood at a fifth of the pitch, all of which the ear counts. It needs to be
// louder than the others on paper to land beside them, and at 1.0 it measures
// within a third of a dB of the place. Peak is still 0.58, so nothing clips.
const EVENT_GAIN = { pickup: 0.45, place: 0.9, reject: 1.0, clear: 0.9 };

// Cleared cells shrink out over 0.2s. Landing the sound just after that starts
// reads as the clear causing it, rather than as part of the placement.
const CLEAR_DELAY_S = 0.06;

const AudioContextClass = window.AudioContext || window.webkitAudioContext;

let audio_ctx = null;
let sound_on = false;
const fetched = new Map();   // file name -> Promise<ArrayBuffer>, until decoded
const decoded = new Map();   // file name -> AudioBuffer, once it is ready
let decode_failed = false;

const CLIPS = [...new Set(Object.values(SOUNDS).flat().map(h => h.file))];

function clipUrl(name) {
    return new URL(`sfx/${name}.wav`, import.meta.url);
}

// Nothing is fetched until sound is turned on, so a player who leaves it off
// never pays for the clips at all.
function fetchClips() {
    for (const name of CLIPS) {
        if (!fetched.has(name) && !decoded.has(name)) {
            fetched.set(name, fetch(clipUrl(name)).then(r => r.arrayBuffer()));
        }
    }
}

// Safe to call on any user gesture, and cheap after the first one. An
// AudioContext built before one starts out suspended (and complains in the
// console), so it is built here rather than on load.
function warmUp() {
    if (!sound_on || decode_failed) return;
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
    if (!sound_on || audio_ctx === null) return;
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

// `from_gesture` is what makes it safe to build an AudioContext: doing that
// without one leaves it suspended and has the browser complain, once here and
// again on every attempt to resume it.
function setSoundOn(on, button, from_gesture) {
    sound_on = on;
    // Only the icon: the button reads "Sound" beside it either way, and
    // aria-pressed is what carries the state to a screen reader.
    button.querySelector('.menu-icon').textContent = on ? '\u{1F50A}' : '\u{1F507}';
    button.classList.toggle('sound-on', on);
    button.setAttribute('aria-pressed', String(on));
    button.title = on ? 'Turn sound off' : 'Turn sound on';
    if (on) {
        fetchClips();
        if (from_gesture) {
            warmUp();
        }
    }
}

function initSfx(button) {
    if (button === null) return;
    // Restoring a setting is not a gesture, so this only starts the download.
    setSoundOn(loadSfxSetting(), button, false);

    button.addEventListener('click', () => {
        setSoundOn(!sound_on, button, true);
        saveSfxSetting(sound_on);
    });

    // Left on from a previous visit: the clips are already on their way, and
    // the first gesture anywhere is what pays for the decode.
    document.addEventListener('pointerdown', warmUp, { once: true });
}

export { initSfx, playSfx };
