// Generates reference.hpp: ground-truth vectors from the original JS prototype
// (not in this repo). Kept for provenance.
// Usage: node make_reference.js <prototype.html> > reference.hpp
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
const i = src.indexOf('const TAU'), j = src.indexOf('/* ---------- Mode: Record');
const k0 = src.indexOf('/* ---------- Output stage'), k1 = src.indexOf('const toVolts');
if (i < 0 || j < 0 || k0 < 0 || k1 < 0) throw new Error('prototype markers not found');
const E = new Function(src.slice(i, j) + src.slice(k0, k1) + `; return { S, changed, recipeVals, makePart, mapV, CURATED, RECIPES,
  recipeRaw, opEval, combineRaw, combineParts, EXAMPLES, WAVES, ALGOS, RATIOS, treat, hash3, seedRng, defaultsOf, MODES, peakCache,
  currentType };`)();
const { S, changed, RECIPES, WAVES, ALGOS, RATIOS, EXAMPLES } = E;

const f17 = x => { if (!Number.isFinite(x)) throw new Error('non-finite'); return Object.is(x, -0) ? '0' : x.toPrecision(17); };
const str = s => JSON.stringify(s);
function dims(a) { const d = []; while (Array.isArray(a)) { d.push(a.length); a = a[0]; } return d; }
function emit(name, a, kind = 'double') {
  const d = dims(a);
  const body = (x, lvl) => Array.isArray(x)
    ? '{' + x.map(y => body(y, lvl + 1)).join(lvl >= d.length - 1 ? ', ' : ',\n') + '}'
    : (kind === 'double' ? f17(x) : str(x));
  const type = kind === 'double' ? 'double' : 'const char*';
  return `inline constexpr ${type} ${name}${d.map(n => '[' + n + ']').join('')} = ${body(a, 0)};\n`;
}
const range = (n, f) => Array.from({ length: n }, (_, k) => f(k));
const T = 0.99999;
const out = [];

// hash3 / seedRng
const hv = [-1, 0, 1, 2, 7, 100, -100, 12345, -98765, 2147483647, -2147483648, 4294967295, 65536, 77, 31, 5, 9, 70000, -3, 1000003];
const hash3Cases = range(20, n => { const a = hv[n], b = hv[(n * 7 + 3) % 20], c = hv[(n * 11 + 5) % 20]; return [a, b, c, E.hash3(a, b, c)]; });
const mulberryFirst = [[0, 10], [0, 11], [140, 10], [72, 11], [11, 10]].map(([s, t]) => { const g = E.seedRng(s, t); return range(8, () => g()); });

// recipes
S.mode = 'recipes';
const recipeIds = RECIPES.map(r => r.id);
const recipeSeed = RECIPES.map(rc => { S.v.recipes.recipe = rc.id; changed(); return E.currentType().seed; });
const resetRecipe = () => Object.assign(S.v.recipes, E.defaultsOf(E.MODES.recipes.params));
const recipeVals = [], recipeNorm = [], recipePeak = [];
RECIPES.forEach((rc, n) => {
  const seed = recipeSeed[n];
  recipeVals.push([0, .5, 1].map(V => { const v = E.recipeVals(rc, seed, 10, E.mapV(rc, seed, V), 0, 0); return v.concat(range(8 - v.length, () => 0)); }));
  const f = E.makePart(rc, seed, 10, E.mapV(rc, seed, .5), 0, 0).f;
  recipeNorm.push(range(65, k => f(k / 64)));
  recipePeak.push(Math.max(...range(512, k => Math.abs(f(k / 512)))));
});

// drift on 'nodes'
const driftNorm = range(4, c => {
  resetRecipe(); Object.assign(S.v.recipes, { recipe: 'nodes', variation: .5, drift: .7 }); changed();
  const rc = RECIPES.find(r => r.id === 'nodes'), seed = E.currentType().seed;
  return range(33, k => E.makePart(rc, seed, 10, E.mapV(rc, seed, .5), .7, c).f(k / 32));
});

// transforms
const tcases = [
  ['bounce', .5, .4, 0, 0, 0, 'off'], ['spring', .3, -.5, .6, 0, 0, 'off'], ['ratchet', .7, 0, 0, .8, .3, 'off'],
  ['harm', .5, .2, -.5, .5, -.4, 'on'], ['fm', .9, -.8, 1, 1, 0, 'off'], ['glitch', .2, .9, -1, .3, 1, 'on'],
];
const transformCases = [], transformRaw = [];
for (const [id, variation, skew, curve, sym, tilt, rev] of tcases) {
  resetRecipe(); Object.assign(S.v.recipes, { recipe: id, variation, skew, curve, sym, tilt, rev }); changed();
  transformCases.push([recipeIds.indexOf(id), variation, skew, curve, sym, tilt, rev === 'on' ? 1 : 0]);
  transformRaw.push(range(65, k => E.recipeRaw(k / 64 * T)));
}

// combine
S.mode = 'combine';
const base = () => Object.assign(S.v.combine, E.defaultsOf(E.MODES.combine.params));
const waveRaw = WAVES.map(([w]) => [.2, .7].map(sh => {
  base(); Object.assign(S.v.combine, { Awave: w, Aratio: 1, Aphase: 0, Ashape: sh }); changed();
  return range(65, k => E.opEval('A', k / 64 * T));
}));
const presetNames = EXAMPLES.map(e => e[0]);
const presetValues = [], presetRatios = [], presetRaw = [], presetParts = [];
for (const [, ex] of EXAMPLES) {
  base(); Object.assign(S.v.combine, ex); changed();
  const row = [];
  for (const L of ['A', 'B', 'C']) {
    row.push(WAVES.findIndex(w => w[0] === ex[L + 'wave']), RATIOS.findIndex(r => r[0] === ex[L + 'ratio']), ex[L + 'amt'], ex[L + 'phase'], ex[L + 'shape']);
  }
  row.push(ALGOS.findIndex(a => a[0] === ex.algo), ex.win);
  presetValues.push(row);
  presetRatios.push(['A', 'B', 'C'].map(L => ex[L + 'ratio']));
  presetRaw.push(range(129, k => E.combineRaw(k / 64 * T)));
  presetParts.push(range(129, k => { const p = E.combineParts(k / 64 * T); return [p.rA, p.rB, p.rC]; }));
}

// treat (fit uses a fixed cycle peak of 1.6)
const treatIn = [-3, -1.6, -1, -.4, 0, .3, 1, 1.7, 2.9];
const treatOut = ['fit', 'clip', 'soft', 'fold', 'wrap'].map(m => {
  S.out.treat = m; changed(); E.peakCache.set(0, 1.6);
  return treatIn.map(x => E.treat(x, 0));
});

out.push('// Generated by make_reference.js from the original JS prototype. Do not edit.\n#pragma once\n\nnamespace ref {\n');
out.push(emit('hash3Cases', hash3Cases), emit('mulberryFirst', mulberryFirst));
out.push(emit('recipeIds', recipeIds, 'str'), emit('recipeSeed', recipeSeed), emit('recipeVals', recipeVals), emit('recipeNorm', recipeNorm));
out.push('// Peak of |f| on the 512-point grid the prototype normalizes on (the 65-point rows can miss it).\n', emit('recipePeak', recipePeak));
out.push(emit('driftNorm', driftNorm), emit('transformCases', transformCases), emit('transformRaw', transformRaw));
out.push(emit('waveRaw', waveRaw), emit('presetNames', presetNames, 'str'), emit('presetValues', presetValues));
out.push('// Actual ratio values per operator (a preset can use a ratio absent from the RATIOS table; its index is then -1).\n');
out.push(emit('presetRatios', presetRatios), emit('presetRaw', presetRaw), emit('presetParts', presetParts));
out.push(emit('treatIn', treatIn), emit('treatOut', treatOut));
out.push('\n}  // namespace ref\n');
process.stdout.write(out.join(''));
