"use strict";

// Checks the installed app hangs together: that everything sw.js promises to
// cache is really there, that the manifest describes icons that exist at the
// sizes it claims, and that index.html points at both.
//
// It is worth checking because the failure is quiet and total. A service
// worker fills its cache with one addAll, which is all-or-nothing: a single
// path that 404s fails the install, and the app that was meant to work with no
// network goes back to needing one, with nothing on screen to say so. Adding a
// module to the page and forgetting this list is all it takes.
//
// Run against the checkout by default, or against a staged copy of the site:
//
//   node test/web/pwa-test.js _site
//
// which is what the Pages workflow does, since a file can equally well be
// missing because it was never copied out of the repository.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

let failures = 0;
function check(condition, description) {
    if (!condition) {
        failures++;
        console.error("FAIL: %s", description);
        return;
    }
    console.log("ok - %s", description);
}

const repo_root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const staged = process.argv[2] !== undefined;
const site_root = staged ? path.resolve(process.argv[2]) : repo_root;

function read(relative) {
    return fs.readFileSync(path.join(site_root, relative), 'utf8');
}

// A URL as the browser would ask for it, back to the file that answers it. The
// shell is a directory, which is served as the index.html inside it.
function siteFile(url_path) {
    const relative = url_path.replace(/^\.\//, '');
    return path.join(site_root, relative === '' ? 'index.html' : relative);
}

function exists(url_path) {
    const file = siteFile(url_path);
    return fs.existsSync(file) && fs.statSync(file).isFile();
}

// The pixel size a PNG really is, off its header, so an icon cannot claim in
// the manifest to be a size it is not.
function pngSize(file) {
    const header = Buffer.alloc(24);
    const handle = fs.openSync(file, 'r');
    fs.readSync(handle, header, 0, 24, 0);
    fs.closeSync(handle);
    if (header.toString('hex', 0, 8) !== '89504e470d0a1a0a') return null;
    return `${header.readUInt32BE(16)}x${header.readUInt32BE(20)}`;
}

const sw_source = read('sw.js');

// ---------------------------------------------------------------- the worker

// The placeholder the Pages workflow substitutes. It has to survive in the
// repository for the workflow's sed to find, and it has to be gone from the
// site the workflow is about to upload -- a deployed worker still carrying it
// never changes, and so never replaces the cache it filled the first time.
if (staged) {
    check(!sw_source.includes('__BUILD_ID__'), 'the staged worker was stamped with a build id');
    const build_id = sw_source.match(/const BUILD_ID = '([^']*)'/);
    check(build_id !== null && build_id[1].length > 0, 'the build id it was stamped with is not empty');
} else {
    check(sw_source.includes("const BUILD_ID = '__BUILD_ID__'"),
        'the worker carries the placeholder the workflow stamps');
}

const shell_match = sw_source.match(/const SHELL = '([^']+)'/);
check(shell_match !== null, 'the worker names the page it serves navigations from');
const shell = shell_match === null ? './' : shell_match[1];

const precache_body = sw_source.match(/const PRECACHE = \[([^\]]*)\]/);
check(precache_body !== null, 'the worker has a precache list');

const precache = precache_body === null ? [] :
    [...precache_body[1].matchAll(/'([^']+)'/g)].map(match => match[1]);
check(/\bSHELL\b/.test(precache_body === null ? '' : precache_body[1]),
    'the precache list includes the page itself');
precache.push(shell);

check(precache.length > 1, `the precache list has entries (${precache.length})`);
for (const url_path of precache) {
    check(exists(url_path), `precached ${url_path} is there to be cached`);
}

const precached = new Set(precache);

// ------------------------------------------------------- everything it needs

// Every module reachable from the page has to be in the list too, or the first
// import off the network is the one that fails. Only relative specifiers: a
// bare one is not something this site serves, and the WASM glue imports
// "module" for the Node build of the engine.
for (const url_path of precache) {
    if (!url_path.endsWith('.js')) continue;
    const source = read(url_path.replace(/^\.\//, ''));
    const directory = path.posix.dirname(url_path);
    const specifiers = [
        ...source.matchAll(/(?:^|[\s;{(])(?:import|from)\s*\(?\s*['"](\.[^'"]*)['"]/g),
    ].map(match => match[1]);
    for (const specifier of new Set(specifiers)) {
        const imported = path.posix.normalize(path.posix.join(directory, specifier));
        const as_listed = imported.startsWith('.') ? imported : `./${imported}`;
        check(precached.has(as_listed),
            `${url_path} imports ${specifier}, which is precached`);
    }
}

// Sound is fetched by name from whatever is in web/sfx, so a clip added there
// and not here is one that only plays with a network behind it.
const sfx_directory = path.join(site_root, 'web/sfx');
for (const clip of fs.readdirSync(sfx_directory).filter(name => name.endsWith('.wav'))) {
    check(precached.has(`./web/sfx/${clip}`), `sound effect ${clip} is precached`);
}

// --------------------------------------------------------------- the manifest

const MANIFEST = './web/manifest.webmanifest';
check(precached.has(MANIFEST), 'the manifest itself is precached');

let manifest = null;
try {
    manifest = JSON.parse(read(MANIFEST.replace(/^\.\//, '')));
} catch (e) {
    check(false, `the manifest is valid JSON (${e.message})`);
}

if (manifest !== null) {
    check(typeof manifest.name === 'string' && manifest.name.length > 0, 'the manifest names the app');
    // What ends up under the icon on a home screen. Long ones get truncated.
    check(typeof manifest.short_name === 'string' && manifest.short_name.length > 0
        && manifest.short_name.length <= 12,
        `the short name fits under an icon ("${manifest.short_name}")`);
    check(['standalone', 'fullscreen', 'minimal-ui'].includes(manifest.display),
        `it asks to open in its own window (display: ${manifest.display})`);
    check(/^#[0-9a-f]{6}$/i.test(manifest.background_color), 'it has a splash colour');
    check(/^#[0-9a-f]{6}$/i.test(manifest.theme_color), 'it has a theme colour');

    // Both are relative to the manifest, which is a directory down from the
    // page. Getting these wrong is what makes a launch open the browser at the
    // wrong address, or drop out of the app the moment the game is opened.
    const manifest_directory = path.posix.dirname(MANIFEST);
    for (const field of ['start_url', 'scope']) {
        const resolved = path.posix.join(manifest_directory, manifest[field]).replace(/\/$/, '');
        check(resolved === '.', `${field} (${manifest[field]}) resolves to the site root`);
    }

    const icons = Array.isArray(manifest.icons) ? manifest.icons : [];
    for (const icon of icons) {
        const url_path = path.posix.join(manifest_directory, icon.src);
        check(exists(url_path), `icon ${icon.src} is there`);
        check(precached.has(`./${url_path}`), `icon ${icon.src} is precached`);
        if (exists(url_path)) {
            check(pngSize(siteFile(url_path)) === icon.sizes,
                `icon ${icon.src} really is ${icon.sizes}`);
        }
    }
    // What a browser wants before it will offer to install: something small
    // enough for a list, something large enough for a splash screen, and one
    // it may crop to a circle without cutting the mark.
    const has = (test) => icons.some(test);
    check(has(icon => icon.sizes === '192x192'), 'there is a 192px icon');
    check(has(icon => icon.sizes === '512x512'), 'there is a 512px icon');
    check(has(icon => (icon.purpose || '').split(/\s+/).includes('maskable')),
        'there is a maskable icon');

    for (const shot of manifest.screenshots || []) {
        const url_path = path.posix.join(manifest_directory, shot.src);
        check(exists(url_path), `screenshot ${shot.src} is there`);
        if (exists(url_path)) {
            check(pngSize(siteFile(url_path)) === shot.sizes,
                `screenshot ${shot.src} really is ${shot.sizes}`);
        }
    }
}

// ------------------------------------------------------------------ the page

const html = read('index.html');
check(/<link[^>]+rel="manifest"[^>]+href="web\/manifest\.webmanifest"/.test(html),
    'the page links the manifest');
check(/<meta[^>]+name="viewport"[^>]+width=device-width/.test(html),
    'the page is measured against the screen it is on');
check(/<link[^>]+rel="apple-touch-icon"/.test(html), 'the page offers iOS an icon');

const theme = html.match(/<meta[^>]+name="theme-color"[^>]+content="([^"]+)"/);
check(theme !== null, 'the page has a theme colour');
if (theme !== null && manifest !== null) {
    check(theme[1] === manifest.theme_color,
        `the page and the manifest agree on it (${theme[1]})`);
}

// The name that ends up under the icon on a home screen. iOS takes it from
// the page and everywhere else takes it from the manifest, so the two saying
// different things is an app called one thing on half the phones it is on.
const ios_name = html.match(/<meta[^>]+name="apple-mobile-web-app-title"[^>]+content="([^"]+)"/);
check(ios_name !== null, 'the page names the app for iOS');
if (ios_name !== null && manifest !== null) {
    check(ios_name[1] === manifest.short_name,
        `and calls it what the manifest calls it ("${ios_name[1]}")`);
}

// The page is where registration starts. Without this the worker is a file
// nobody ever asks for, and everything above it is decoration.
check(/registerServiceWorker\(\)/.test(read('web/script.js')),
    'the page registers the worker');

if (failures > 0) {
    console.error("%d check(s) failed", failures);
    process.exit(1);
}
console.log("All PWA checks passed.");
