import fs from 'node:fs';
import assert from 'node:assert/strict';
const codes=['en','zh','hi','es','ar'];
const catalogs=Object.fromEntries(codes.map(code=>[code,JSON.parse(fs.readFileSync(`locale/${code}.json`,'utf8'))]));
const keys=Object.keys(catalogs.en).sort();
const placeholders=text=>[...text.matchAll(/\{([a-z_]+)\}/g)].map(m=>m[1]).sort();
for(const code of codes){
 assert.deepEqual(Object.keys(catalogs[code]).sort(),keys,code+' message coverage');
 for(const key of keys){const text=catalogs[code][key];assert.equal(typeof text,'string');assert(text.trim(),code+' '+key);assert(!text.includes(')lfrjson"'));assert.deepEqual(placeholders(text),placeholders(catalogs.en[key]),code+' '+key+' placeholders');}
 assert.deepEqual(placeholders(catalogs[code]['restore.warning']),['token']);
}
for(const dir of ['src','include'])for(const file of fs.readdirSync(dir).filter(f=>/\.(cpp|hpp)$/.test(f))){
 const source=fs.readFileSync(`${dir}/${file}`,'utf8');
 for(const m of source.matchAll(/\btr\("([a-z_.]+)"/g))assert(keys.includes(m[1]),file+' unknown translation '+m[1]);
}
console.log(`${keys.length} messages × ${codes.length} languages: complete coverage, matching placeholders and invariant confirmation token.`);
