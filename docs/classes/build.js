#!/usr/bin/env node
// Assemble the class diagrams in docs/classes/*.mmd from:
//   src/<name>.mmd  - diagram template: comments, layout, bare nodes,
//                     relationships, and "%%include <ClassName>" lines
//   defs/<Class>.mmd - one canonical, complete class body per file
//
// An include line is replaced verbatim with the def file's contents.
// Run:  node build.js        (from docs/classes)
//       node build.js check  (fail if any generated file is out of date)

const fs = require('fs');
const path = require('path');

const root = __dirname;
const srcDir = path.join(root, 'src');
const defDir = path.join(root, 'defs');
const check = process.argv[2] === 'check';

let failed = false;

for (const tpl of fs.readdirSync(srcDir).filter(f => f.endsWith('.mmd'))) {
    const lines = fs.readFileSync(path.join(srcDir, tpl), 'utf8').split(/\r?\n/);
    const out = [];
    for (const line of lines) {
        const m = line.match(/^\s*%%include\s+(\S+)\s*$/);
        if (m) {
            const defFile = path.join(defDir, m[1] + '.mmd');
            if (!fs.existsSync(defFile)) {
                console.error(`${tpl}: missing defs/${m[1]}.mmd`);
                failed = true;
                continue;
            }
            out.push(fs.readFileSync(defFile, 'utf8').replace(/\s+$/, ''));
        } else {
            out.push(line);
        }
    }
    const result = out.join('\n').replace(/\n+$/, '') + '\n';
    const target = path.join(root, tpl);
    const current = fs.existsSync(target) ? fs.readFileSync(target, 'utf8') : '';
    if (check) {
        if (current !== result) {
            console.error(`${tpl}: out of date (run: node build.js)`);
            failed = true;
        }
    } else if (current !== result) {
        fs.writeFileSync(target, result);
        console.log(`generated ${tpl}`);
    }
}

process.exit(failed ? 1 : 0);
