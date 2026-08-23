# Sound effects

`web/sfx.js` defines pickup, placement, rejection, and clear sounds. Sound is
disabled until the user enables it, and clips are loaded only when needed.
Assist moves use sound at speeds where individual moves are also animated.

| event | source clip | behavior |
|---|---|---|
| pickup | `impactWood_light_000.wav` | starts a drag or assist move |
| place | `impactWood_medium_000.wav` | completes a placement |
| reject | `impactSoft_medium_000.wav` | ends a drag without a move |
| clear | pickup clip pitched as a three-note chord | clears a line or region |

Event levels are set in `EVENT_GAIN`. The clear sound is synthesized with three
short, staggered uses of the pickup clip.

The source clips come from Kenney's
[Impact Sounds](https://kenney.nl/assets/impact-sounds) pack and are CC0. The
files in this repository are normalized, 16-bit mono WAV assets.
