#!/usr/bin/env node
// Generate the aspect diagram templates (src/<area>.mmd) from
// defs/_manifest.json: every class's def is %%included in its area
// diagram, inheritance edges are emitted from the manifest, and bases
// living in another diagram appear as bare stub nodes.
//
// Hand-curated content per diagram goes in src/extra/<area>.mmd and is
// appended verbatim (relations, notes, pseudo-classes like tapclean_api).
//
// Run:  node gen_templates.js && node build.js   (from docs/classes)

const fs = require('fs');
const path = require('path');

const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, 'defs', '_manifest.json'), 'utf8'));
const srcDir = path.join(__dirname, 'src');
const extraDir = path.join(srcDir, 'extra');
fs.mkdirSync(extraDir, { recursive: true });

const titles = {
    vfs_core:        'Core VFS: MStream/MFile/MFileSystem, brokers, sessions, iostream buffer',
    fs_device:       'Device filesystems: flash/SD (POSIX VFS), RAM, HIMEM',
    fs_network:      'Network filesystems: one MFile/MStream/MSession trio per protocol',
    fs_service:      'Service and codec filesystems: mDNS, JSON, QR, RetroPixels, SQLite, hash, ML',
    media_disk:      'Disk media: D64 family, CMD HD/FD partitions, IDE64 CFS, M2I index',
    media_tape:      'Tape media: TAP/DMP/HTAP pipeline over the TAPClean engine, T64/TCRT',
    media_container: 'Containers and file wrappers: archives, ISO, LBR/LNX/ARC/ARK, P00, carts',
    bus:             'IEC/GPIB bus: handlers, transfer protocols, fastloaders',
    devices:         'Virtual devices on the bus: drive, fuji, printer, modem, network, clock',
    web:             'Web server: httpd lifecycle, WebDAV, websocket, proxy',
    console:         'Serial/TCP console: command registry and executor task',
    hardware:        'Hardware managers: WiFi, UART, LEDs, keys, Bluetooth, display',
    utils:           'Utilities: string/URL/PETSCII helpers, config, buffers, misc libs',
};

const byArea = {};
for (const [cls, info] of Object.entries(manifest))
    (byArea[info.area] ||= []).push(cls);

let covered = 0;
for (const [area, classes] of Object.entries(byArea)) {
    classes.sort((a, b) => a.localeCompare(b));
    const inArea = new Set(classes);
    const out = [];
    out.push(`%% ${titles[area] || area}`);
    out.push('%% GENERATED template (gen_templates.js) - curated content lives in');
    out.push(`%% src/extra/${area}.mmd; class bodies live in defs/<Class>.mmd`);
    out.push('classDiagram');
    out.push('    direction LR');
    out.push('');
    for (const c of classes) out.push(`    %%include ${c}`);
    // stub nodes for out-of-area bases
    const stubs = new Set();
    for (const c of classes)
        for (const b of manifest[c].bases || [])
            if (!inArea.has(b) && b !== 'std' && manifest[b]) stubs.add(b);
    if (stubs.size) out.push('');
    for (const b of [...stubs].sort())
        out.push(`    class ${b} {\n        <<see ${manifest[b].area}>>\n    }`);
    out.push('');
    for (const c of classes)
        for (const b of manifest[c].bases || []) {
            if (b === 'std' || (!inArea.has(b) && !manifest[b])) continue;
            out.push(`    ${b} <|-- ${c}`);
        }
    const extra = path.join(extraDir, area + '.mmd');
    if (fs.existsSync(extra)) {
        out.push('');
        out.push(fs.readFileSync(extra, 'utf8').replace(/\s+$/, ''));
    }
    fs.writeFileSync(path.join(srcDir, area + '.mmd'), out.join('\n') + '\n');
    covered += classes.length;
    console.log(`src/${area}.mmd: ${classes.length} classes`);
}

if (covered !== Object.keys(manifest).length)
    throw new Error('coverage mismatch');
console.log(`coverage: ${covered}/${Object.keys(manifest).length} classes in aspect diagrams`);
