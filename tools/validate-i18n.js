const fs = require('fs');
const path = require('path');

const DIR = path.join(__dirname, '..', 'www', 'i18n');
const ref = JSON.parse(fs.readFileSync(path.join(DIR, 'en.json'), 'utf8'));
const refKeys = new Set(Object.keys(ref).filter(k => !k.startsWith('_')));
const files = fs.readdirSync(DIR).filter(f => f.endsWith('.json') && !f.startsWith('_') && f !== 'meta.json');

let ok = true;
for (const f of files) {
    const cur = JSON.parse(fs.readFileSync(path.join(DIR, f), 'utf8'));
    const curKeys = new Set(Object.keys(cur).filter(k => !k.startsWith('_')));
    const missing = [...refKeys].filter(k => !curKeys.has(k));
    const extra   = [...curKeys].filter(k => !refKeys.has(k));
    if (missing.length) { ok = false; console.log(`❌ ${f} missing ${missing.length}: ${missing.slice(0,10).join(', ')}${missing.length>10?'…':''}`); }
    if (extra.length)   { ok = false; console.log(`⚠️  ${f} has ${extra.length} extra: ${extra.slice(0,10).join(', ')}`); }
    if (!missing.length && !extra.length) console.log(`✅ ${f}`);
}
console.log(ok ? '\nAll language files consistent.' : '\nFix issues above.');
process.exit(ok ? 0 : 1);