"use strict";
import { saveSfxSetting, loadSfxSetting } from "./storage.js";

// A piece lifted off the deck, dropped onto the board, going back down unplayed,
// and a row coming apart. Played whether you or the assist is moving the pieces,
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

// Undefined in a browser with no Web Audio at all, which is checked for rather
// than thrown from: the unlock below listens for every gesture on the page, and
// a constructor that throws would do it once per tap for the whole game.
const AudioContextClass = window.AudioContext || window.webkitAudioContext;
const audio_supported = AudioContextClass !== undefined;

let audio_ctx = null;
let sound_on = false;
const fetched = new Map();   // file name -> Promise<ArrayBuffer>, until decoded
const decoded = new Map();   // file name -> AudioBuffer, once it is ready
let decode_failed = false;
// Set once a resume has been asked for and the context was still not running
// when it settled. The next gesture replaces the context instead of asking it
// again -- see isRunning() below for why one can stop answering.
let context_stuck = false;
let unlock_armed = false;

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

// WebKit has a state the specification doesn't: an AudioContext goes
// 'interrupted' whenever iOS takes the audio session away -- a call, another
// app starting to play, or the app merely going to the background -- and stays
// there until something asks for it back.
//
// That is what the installed app hits and a browser tab mostly doesn't. A tab
// is usually reloaded on the way back to it, which builds a fresh context; an
// app opened from the home screen returns to the page it left, still holding
// the context it left, interrupted. Sound then works until the first trip to
// the app switcher and never again for that launch, which is the shape of the
// bug this was reported as.
//
// So nothing here asks whether the context is suspended. It asks whether it is
// running, and treats every other answer as something to fix.
function isRunning() {
    return audio_ctx !== null && audio_ctx.state === 'running';
}

// resume() is only honoured while a user gesture is being handled, and WebKit
// is particular about which events count as one, so all of the plausible ones
// are listened for until one of them takes. They are removed again the moment
// the context is running, so the common case costs nothing.
//
// This was a single pointerdown listener with { once: true }, which spends the
// launch's only attempt on the first touch and has nothing left if that touch
// isn't accepted. An installed app is where that matters: it launches with the
// setting already on, so the player never clicks the button that used to be
// what really unlocked the sound.
const UNLOCK_EVENTS = ['pointerdown', 'pointerup', 'touchend', 'click', 'keydown'];

// Captured so a handler that stops the event on its way down can't cost the
// sound its gesture, and passive so that listening for a touch is never
// something the browser has to wait on before it scrolls.
const UNLOCK_OPTIONS = { capture: true, passive: true };

function onUnlockGesture() {
    warmUp(true);
}

function armUnlock() {
    if (unlock_armed || !sound_on || decode_failed || !audio_supported) return;
    unlock_armed = true;
    for (const type of UNLOCK_EVENTS) {
        document.addEventListener(type, onUnlockGesture, UNLOCK_OPTIONS);
    }
}

function disarmUnlock() {
    if (!unlock_armed) return;
    unlock_armed = false;
    for (const type of UNLOCK_EVENTS) {
        document.removeEventListener(type, onUnlockGesture, UNLOCK_OPTIONS);
    }
}

// resume() and close() are promises everywhere that matters and undefined in
// old WebKit, which is exactly the browser this file is careful about.
function settled(result) {
    return Promise.resolve(result).catch(() => { });
}

// Brings the context back if it can, and leaves a gesture listener behind if
// it can't. Safe to call from anywhere: outside a gesture the resume is a
// no-op, and the listener is then what actually does the work.
//
// Only a resume asked for during a gesture says anything when it fails, which
// is why callers have to say where they are calling from. Every other resume
// failing is just the browser's rule being applied.
function resumeContext(from_gesture) {
    if (!sound_on || audio_ctx === null) return;
    if (isRunning()) {
        context_stuck = false;
        disarmUnlock();
        return;
    }
    armUnlock();
    const ctx = audio_ctx;
    settled(ctx.resume()).then(() => {
        if (ctx !== audio_ctx) return;   // already replaced
        if (ctx.state === 'running') {
            context_stuck = false;
            disarmUnlock();
        } else if (from_gesture) {
            context_stuck = true;
        }
    });
}

// Safe to call on any user gesture, and cheap after the first one. An
// AudioContext built before one starts out suspended (and complains in the
// console), so it is built here rather than on load.
function warmUp(from_gesture) {
    if (!sound_on || decode_failed || !audio_supported) return;
    // An interrupted context that has been asked for and didn't come back is
    // not going to: iOS can leave one stuck for the life of the page. A new
    // one built during a gesture starts out running, which is the only way
    // back, and closing the old one keeps this under WebKit's limit on how
    // many a page may have.
    //
    // Checked against the state and not only the flag, since a context can
    // also come back by itself between the failed resume and this gesture,
    // and one that is running is never worth replacing.
    if (context_stuck && audio_ctx !== null && !isRunning()) {
        const stale = audio_ctx;
        audio_ctx = null;
        context_stuck = false;
        settled(stale.close());
    }
    if (audio_ctx === null) {
        audio_ctx = new AudioContextClass();
    }
    resumeContext(from_gesture);
    decodeClips();
}

// The buffers survive their context -- an AudioBuffer belongs to no context in
// particular, and one decoded before an interruption still plays after it --
// so this is only ever done once per clip, however many contexts come and go.
function decodeClips() {
    for (const name of CLIPS) {
        if (!fetched.has(name)) continue;
        const ctx = audio_ctx;
        // Claim the slot before awaiting: decodeAudioData detaches the buffer
        // it is given, so the same bytes must not be decoded twice.
        const bytes = fetched.get(name);
        fetched.delete(name);
        bytes
            .then(b => ctx.decodeAudioData(b))
            .then(buffer => decoded.set(name, buffer))
            .catch(() => {
                // A decode that was in flight when its context was replaced
                // says nothing about the browser's decoder: the bytes went
                // with the context, and the clip is simply fetched again.
                if (ctx !== audio_ctx) {
                    fetched.set(name, fetch(clipUrl(name)).then(r => r.arrayBuffer()));
                    return;
                }
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
        warmUp(false);
        return;
    }
    // Nothing is scheduled onto a context that isn't running. Its clock is
    // stopped, so every source queued while it is away would be due the
    // instant it comes back, and a minute of play spent interrupted would
    // return as one noise. Ask for it back and let this sound go.
    if (!isRunning()) {
        resumeContext(false);
        return;
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
    if (!on) {
        disarmUnlock();
        return;
    }
    fetchClips();
    if (from_gesture) {
        warmUp(true);
    } else {
        // Not a gesture, so a context built here would only start out
        // suspended. Wait for one instead.
        armUnlock();
    }
}

function initSfx(button) {
    if (button === null) return;
    // Restoring a setting is not a gesture, so this starts the download and
    // arms the wait for one. That path is the installed app's normal launch:
    // it opens with sound already on and no button press coming.
    setSoundOn(loadSfxSetting(), button, false);

    button.addEventListener('click', () => {
        setSoundOn(!sound_on, button, true);
        saveSfxSetting(sound_on);
    });

    // Coming back to the app is the moment the context was most likely taken
    // away: on iOS a home screen app returns from the app switcher to the same
    // page it left, holding the same interrupted context. Asking here often
    // gets it back on its own, and arms a gesture for when it doesn't.
    document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') resumeContext(false);
    });
    window.addEventListener('pageshow', () => resumeContext(false));
}

export { initSfx, playSfx };
