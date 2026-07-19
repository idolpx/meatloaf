#!/usr/bin/env node
// Generate defs/<Class>.mmd skeletons for every class in the source tree,
// plus defs/_manifest.json (class -> header, bases, area) used by
// gen_templates.js to assemble the aspect diagrams.
//
// Hand-curated def files are NEVER overwritten: delete one to regenerate.
// Heuristic C++ parsing - good enough for documentation diagrams.
//
// Run:  node gen_defs.js   (from docs/classes)

const fs = require('fs');
const path = require('path');

const repo = path.resolve(__dirname, '..', '..');
const defDir = path.join(__dirname, 'defs');

// area = which aspect diagram the class belongs to (by header path)
const areas = [
    [/lib[\\\/]meatloaf[\\\/]media[\\\/](disk|hd)[\\\/]/, 'media_disk'],
    [/lib[\\\/]meatloaf[\\\/]media[\\\/]tape[\\\/]/, 'media_tape'],
    [/lib[\\\/]meatloaf[\\\/]media[\\\/]/, 'media_container'],
    [/lib[\\\/]meatloaf[\\\/]network[\\\/]/, 'fs_network'],
    [/lib[\\\/]meatloaf[\\\/]device[\\\/]/, 'fs_device'],
    [/lib[\\\/]meatloaf[\\\/](service|codec|db|link|loader|scanner|wrapper|encoder)[\\\/]/, 'fs_service'],
    [/lib[\\\/]meatloaf[\\\/]/, 'vfs_core'],
    [/lib[\\\/]bus[\\\/]/, 'bus'],
    [/lib[\\\/]device[\\\/]/, 'devices'],
    [/lib[\\\/]www[\\\/]/, 'web'],
    [/lib[\\\/]console[\\\/]/, 'console'],
    [/lib[\\\/](hardware|display|led|keys)[\\\/]/, 'hardware'],
    [/lib[\\\/](utils|config-ml|encoding|ffmpeg|sam|audio|task)[\\\/]/, 'utils'],
    [/lib[\\\/]/, 'utils'],
];

const roots = ['lib'];
const skipDirs = /[\\\/](\.git|\.pio|node_modules|\.archive)[\\\/]/;

function* headers(dir) {
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (skipDirs.test(p + path.sep)) continue;
        if (e.isDirectory()) yield* headers(p);
        else if (e.name.endsWith('.h')) yield p;
    }
}

function stripComments(src) {
    return src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
}

// crude but effective: find top-level class bodies
function findClasses(src) {
    const out = [];
    const re = /(template\s*<[^>]*>\s*)?class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*([^\{;]+))?\{/g;
    let m;
    while ((m = re.exec(src)) !== null) {
        const isTemplate = !!m[1];
        const name = m[2];
        const basesRaw = m[3] || '';
        // find matching closing brace
        let depth = 1, i = re.lastIndex;
        while (i < src.length && depth > 0) {
            const c = src[i];
            if (c === '{') depth++;
            else if (c === '}') depth--;
            i++;
        }
        const body = src.slice(re.lastIndex, i - 1);
        const bases = basesRaw
            .split(',')
            .map(b => b.replace(/\b(public|protected|private|virtual)\b/g, '').trim())
            .map(b => b.replace(/<.*$/, '').replace(/^std::.*/, 'std'))
            .filter(b => b.length);
        out.push({ name, bases, body, isTemplate });
        re.lastIndex = i;
    }
    return out;
}

function esc(s) {
    return s
        .replace(/</g, '~').replace(/>/g, '~')
        .replace(/::/g, '.')
        .replace(/[&"]/g, '')
        .replace(/\s+/g, ' ')
        .trim();
}

function parseMembers(body, defaultVis) {
    const members = { '-': [], '#': [], '+': [] };
    let vis = defaultVis;
    // split body into top-level statements
    let depth = 0, cur = '';
    const stmts = [];
    for (const ch of body) {
        if (ch === '{') { depth++; if (depth === 1) { stmts.push(cur + '{}'); cur = ''; continue; } }
        else if (ch === '}') { depth--; continue; }
        if (depth > 0) continue;
        if (ch === ';') { stmts.push(cur); cur = ''; continue; }
        cur += ch;
    }
    for (let s of stmts) {
        s = s.replace(/\s+/g, ' ').trim();
        const vm = s.match(/(public|protected|private)\s*:\s*(.*)$/);
        if (vm) { vis = vm[1] === 'public' ? '+' : vm[1] === 'protected' ? '#' : '-'; s = vm[2].trim(); }
        if (!s) continue;
        if (/^(friend|using|typedef|enum|struct|class|template|#|\[\[)/.test(s)) continue;
        if (/^[A-Za-z_][A-Za-z0-9_]*\s*\(/.test(s) && !/ /.test(s.split('(')[0])) {
            // constructor / destructor style
        }
        const isStatic = /^static /.test(s);
        const isPureVirtual = /=\s*0\s*$/.test(s) || /=\s*0\s*\{\}$/.test(s);
        if (/^~/.test(s)) continue;
        s = s.replace(/\)\s*:\s*[A-Za-z_][^;]*$/, ')');   // ctor initializer list
        s = s.replace(/^static /, '').replace(/^virtual /, '').replace(/^inline /, '')
             .replace(/\s*(override|final|const|noexcept)\s*(\{\})?\s*$/g, '')
             .replace(/=\s*0\s*$/, '').replace(/\{\}$/, '').trim();
        if (!s) continue;
        const fm = s.match(/^(.*?)\b([A-Za-z_~][A-Za-z0-9_]*)\s*\((.*)\)\s*$/);
        let line;
        if (fm) {
            const ret = esc(fm[1]);
            const fname = fm[2];
            let args = esc(fm[3]);
            if (args.length > 40) args = '...';
            // drop default values
            args = args.replace(/\s*=\s*[^,]+/g, '');
            line = `${fname}(${args})${ret ? ' ' + ret : ''}`;
        } else {
            // field: "type name" possibly with initializer
            s = s.replace(/\s*=\s*.*$/, '').replace(/\s*\{.*$/, '');
            if (!s || / operator/.test(s)) continue;
            line = esc(s);
        }
        if (!line || line.length > 90) continue;
        line = line.replace(/:/g, '-');
        line += isPureVirtual ? '*' : '';
        line += isStatic ? '$' : '';
        members[vis].push(line);
    }
    return members;
}

const manifest = {};
let generated = 0, kept = 0;

for (const rootDir of roots) {
    for (const h of headers(path.join(repo, rootDir))) {
        const src = stripComments(fs.readFileSync(h, 'utf8'));
        for (const cls of findClasses(src)) {
            if (!/^[A-Z]|^iec|^fn|^cbuf|^mfilebuf|^idirbuf|^csstreambuf|^basic_fstream|^driveMemory|^systemBus|^parallelBus|^printerlist|^samlib|^oiecstream|^wwwModules|^tapclean/.test(cls.name)) continue;
            const rel = path.relative(repo, h).replace(/\\/g, '/');
            if (manifest[cls.name]) continue; // first definition wins
            let area = 'utils';
            for (const [re2, a] of areas) if (re2.test(h)) { area = a; break; }
            manifest[cls.name] = { header: rel, bases: cls.bases, area };

            const defFile = path.join(defDir, cls.name + '.mmd');
            if (fs.existsSync(defFile)) { kept++; continue; }

            const mem = parseMembers(cls.body, '-');
            const lines = ['%%gen', `    class ${cls.name} {`];
            if (cls.isTemplate) lines.push('        <<template>>');
            for (const v of ['-', '#', '+'])
                for (const l of mem[v].slice(0, 40)) lines.push(`        ${v}${l}`);
            if (lines.length === 1) lines.push(`        <<${rel}>>`);
            lines.push('    }');
            fs.writeFileSync(defFile, lines.join('\n') + '\n');
            generated++;
        }
    }
}

fs.writeFileSync(path.join(defDir, '_manifest.json'), JSON.stringify(manifest, null, 1));
console.log(`classes: ${Object.keys(manifest).length}, defs generated: ${generated}, hand-curated kept: ${kept}`);
