"use strict";
import { saveSfxSetting, loadSfxSetting } from "./storage.js";

// Lazy, opt-in sound effects. See web/sfx/README.md for asset details.

const PICKUP = 'impactWood_light_000';

// Semitone playback-rate multiplier.
const STEP = 2 ** (1 / 12);

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

const EVENT_GAIN = { pickup: 0.45, place: 0.9, reject: 1.0, clear: 0.9 };

/**
 * @typedef {keyof typeof SOUNDS} SoundEvent
 */

// Sync with the clear animation.
const CLEAR_DELAY_S = 0.06;

const AudioContextClass = window.AudioContext
    || /** @type {{webkitAudioContext?: typeof AudioContext}} */ (window).webkitAudioContext;

/** @type {AudioContext | null} */
let audio_ctx = null;
let sound_on = false;
/** @type {Map<string, Promise<ArrayBuffer>>} file name -> bytes, until decoded */
const fetched = new Map();
/** @type {Map<string, AudioBuffer>} file name -> clip, once it is ready */
const decoded = new Map();
let decode_failed = false;

const CLIPS = [...new Set(Object.values(SOUNDS).flat().map(h => h.file))];

/** @type {(name: string) => URL} */
function clipUrl(name) {
    return new URL(`sfx/${name}.wav`, import.meta.url);
}

function fetchClips() {
    for (const name of CLIPS) {
        if (!fetched.has(name) && !decoded.has(name)) {
            fetched.set(name, fetch(clipUrl(name)).then(r => r.arrayBuffer()));
        }
    }
}

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
        // Claim the buffer before decodeAudioData detaches it.
        const bytes = fetched.get(name);
        fetched.delete(name);
        if (bytes === undefined) continue;
        const ctx = audio_ctx;
        bytes
            .then(b => ctx.decodeAudioData(b))
            .then(buffer => decoded.set(name, buffer))
            .catch(() => {
                decode_failed = true;
                console.warn('blokie: sound effects are off, this browser could not decode web/sfx/*.wav');
            });
    }
}

/** @type {(event: SoundEvent) => void} */
function playSfx(event) {
    if (!sound_on || audio_ctx === null) return;
    const hits = SOUNDS[event];
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
        source.buffer = decoded.get(hit.file) ?? null;
        source.playbackRate.value = hit.rate;
        const gain = audio_ctx.createGain();
        gain.gain.value = EVENT_GAIN[event] * hit.gain;
        source.connect(gain).connect(audio_ctx.destination);
        source.start(base + hit.at);
    }
}

/** @type {(on: boolean, button: HTMLElement, from_gesture: boolean) => void} */
function setSoundOn(on, button, from_gesture) {
    sound_on = on;
    const icon = button.querySelector('.menu-icon');
    if (icon !== null) {
        icon.textContent = on ? '\u{1F50A}' : '\u{1F507}';
    }
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

/** @type {(button: HTMLElement | null) => void} */
function initSfx(button) {
    if (button === null) return;
    setSoundOn(loadSfxSetting(), button, false);

    button.addEventListener('click', () => {
        setSoundOn(!sound_on, button, true);
        saveSfxSetting(sound_on);
    });

    // Decode after the first gesture.
    document.addEventListener('pointerdown', warmUp, { once: true });
}

export { initSfx, playSfx };
