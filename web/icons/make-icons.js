"use strict";

// Draws the icons a phone shows for an installed Blokie: on the home screen,
// in the app switcher, and on the splash screen while the page loads. Run it
// with `npm run icons` and commit what it writes, the way the WASM under
// engine/ is built and committed rather than built on the way out the door.
//
// The mark is the one in web/favicon.ico -- a piece with its top right corner
// missing -- redrawn here rather than rescaled from it, so every size comes out
// on whole pixels instead of resampled off a 192px original. It is redrawn and
// not traced: the favicon carries the shape, this carries the measurements.
//
// Everything is axis-aligned rectangles of flat colour, which is the whole
// reason this can be a hundred lines of Node with nothing installed. The PNGs
// it writes are 8-bit RGB with no alpha, since an icon is composited against a
// launcher's wallpaper and a transparent one comes out black on iOS.

import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

// The game's own blue, the pink the mark answers it with, and the board's
// white behind both.
const BLUE = [54, 112, 232];
const PINK = [255, 91, 160];
const INK = [0, 0, 0];
const PAPER = [255, 255, 255];

// Which cells the piece fills, and in what. The hole in the top right is what
// makes the mark a piece rather than a grid.
const CELLS = [
    [BLUE, PINK, null],
    [BLUE, PINK, PINK],
    [BLUE, BLUE, BLUE],
];

// A cell is five units of colour, and every black line between or around them
// is one -- so three cells and the four lines they need come to 19 across.
const STROKE = 1;
const CELL = 5;
const SPAN = 3 * CELL + 4 * STROKE;

// How much of the icon the mark itself takes up. A launcher draws an ordinary
// icon about as it is given it, so that one only wants enough margin not to
// look wedged in. A maskable icon is cropped to whatever shape the platform
// prefers, and only a circle across 80% of the width is promised to survive:
// the largest square inside that circle is 80/sqrt(2), a hair over 56%.
const PLAIN_FRACTION = 0.76;
const MASKABLE_FRACTION = 0.56;

const ICONS = [
    { file: 'icon-192.png', size: 192, fraction: PLAIN_FRACTION },
    { file: 'icon-512.png', size: 512, fraction: PLAIN_FRACTION },
    { file: 'icon-maskable-512.png', size: 512, fraction: MASKABLE_FRACTION },
    // iOS reads this one instead of the manifest, and rounds the corners off
    // it itself. 180 is what a 3x phone asks for; smaller screens shrink it.
    { file: 'apple-touch-icon.png', size: 180, fraction: PLAIN_FRACTION },
];

// How much of [a0, a1) the span [b0, b1) covers, which for a pixel against a
// rectangle is how much of that pixel the rectangle paints. Sizes below are
// picked so the edges land on whole pixels and this only ever returns 0 or 1,
// but it keeps the drawing honest at a size that doesn't divide as neatly.
function overlap(a0, a1, b0, b1) {
    return Math.max(0, Math.min(a1, b1) - Math.max(a0, b0));
}

// One flat rectangle, blended over whatever is under it.
function fillRect(pixels, size, x0, y0, x1, y1, colour) {
    for (let y = Math.max(0, Math.floor(y0)); y < Math.min(size, Math.ceil(y1)); ++y) {
        const rows = overlap(y, y + 1, y0, y1);
        for (let x = Math.max(0, Math.floor(x0)); x < Math.min(size, Math.ceil(x1)); ++x) {
            const covered = rows * overlap(x, x + 1, x0, x1);
            if (covered <= 0) continue;
            const at = (y * size + x) * 3;
            for (let c = 0; c < 3; ++c) {
                pixels[at + c] = Math.round(pixels[at + c] * (1 - covered) + colour[c] * covered);
            }
        }
    }
}

// The mark, centred on a square of paper. Each filled cell is drawn as its
// colour on a black rectangle a stroke larger all round, so the outline falls
// out of the cells that are there rather than being drawn as its own shape --
// which is what leaves the notch in the top right corner clean.
function drawIcon(size, fraction) {
    const unit = Math.max(1, Math.round(size * fraction / SPAN));
    const origin = Math.round((size - unit * SPAN) / 2);

    const pixels = Buffer.alloc(size * size * 3);
    fillRect(pixels, size, 0, 0, size, size, PAPER);

    for (let row = 0; row < 3; ++row) {
        for (let col = 0; col < 3; ++col) {
            const colour = CELLS[row][col];
            if (colour === null) continue;
            // The cell's own corner, a stroke in from where its backing starts.
            const x = origin + (STROKE + col * (CELL + STROKE)) * unit;
            const y = origin + (STROKE + row * (CELL + STROKE)) * unit;
            const side = CELL * unit;
            const edge = STROKE * unit;
            fillRect(pixels, size, x - edge, y - edge, x + side + edge, y + side + edge, INK);
            fillRect(pixels, size, x, y, x + side, y + side, colour);
        }
    }
    return pixels;
}

const CRC_TABLE = Array.from({ length: 256 }, (_, n) => {
    let c = n;
    for (let k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
    }
    return c >>> 0;
});

function crc32(buffer) {
    let c = 0xFFFFFFFF;
    for (const byte of buffer) {
        c = CRC_TABLE[(c ^ byte) & 0xFF] ^ (c >>> 8);
    }
    return (c ^ 0xFFFFFFFF) >>> 0;
}

function chunk(type, data) {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const typed = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(typed));
    return Buffer.concat([length, typed, crc]);
}

// A PNG of flat colour, with every scanline left unfiltered: deflate has an
// easy enough time of the runs that picking a filter per row would save bytes
// that aren't there.
function encodePng(pixels, size) {
    const stride = size * 3;
    const raw = Buffer.alloc(size * (1 + stride));
    for (let y = 0; y < size; ++y) {
        pixels.copy(raw, y * (1 + stride) + 1, y * stride, (y + 1) * stride);
    }

    const header = Buffer.alloc(13);
    header.writeUInt32BE(size, 0);
    header.writeUInt32BE(size, 4);
    header[8] = 8;   // bits per channel
    header[9] = 2;   // truecolour, no alpha

    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        chunk('IHDR', header),
        chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
        chunk('IEND', Buffer.alloc(0)),
    ]);
}

const here = path.dirname(fileURLToPath(import.meta.url));
for (const icon of ICONS) {
    const png = encodePng(drawIcon(icon.size, icon.fraction), icon.size);
    fs.writeFileSync(path.join(here, icon.file), png);
    console.log('%s - %dx%d, %d bytes', icon.file, icon.size, icon.size, png.length);
}
