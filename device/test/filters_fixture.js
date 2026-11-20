// Genera device/test/filters_fixture.zip per filters_test.cpp: i pezzi della
// schermata filtri come li esporta il configuratore web, le anteprime di riferimento
// (le stesse del simulatore) e le regole degli stadi calcolate dal configuratore web.
//
// Uso: servi la radice del repo (python3 -m http.server 8766), apri
// http://localhost:8766/companion/, incolla questo file nella console e salva lo
// zip scaricato in device/test/. Lo stato del configuratore web non viene toccato.
// makeFiltersFixture(false) restituisce lo zip invece di scaricarlo.
async function makeFiltersFixture(save = true) {
  const keep = state;
  try {
    state = migrate(JSON.parse(JSON.stringify(keep)));
    state.lenses = []; state.cards = [];
    state.auto = { lenschart: false, filters: true, filterlist: false };
    // Coppie di ND, una diottria da uno stadio e una split, un vetro da due stadi.
    state.filters = {
      ndFormat: 'density', ndType: 'IRND', nd: [0.3, 0.6, 0.9, 1.2, 1.8], ndCombos: [1.5, 2.1],
      dio: [{ v: 1, split: false }, { v: 0.5, split: false, slots: 1 }, { v: 2, split: true }],
      effects: [
        { id: 'bpm', name: 'Black Pro-Mist', strengths: [{ s: '1/8' }, { s: '1/4' }] },
        { id: null, name: 'Pola', strengths: [{ s: '' }] },
        { id: null, name: 'Grad ND 0.6', slots: 2, strengths: [{ s: '' }] },
      ],
      stages: [null, null, null, null, null], previewNd: 1, previewDio: 2,
    };
    const kit = kitGlasses(), key = i => (i < 0 ? null : kit[i].key), idx = k => (k ? kit.findIndex(g => g.key === k) : -1);
    const toKeys = a => a.map(key), list = a => a.map(idx).join(',');
    if (await document.fonts.load(`800 40px "Inter"`).then(f => !f.length)) throw new Error('font Inter non caricato');

    const zip = await buildZip(), lines = [];
    // Anteprime: ND, diottria, stadi (indici nel kit, -1 = nessuno) e stadio selezionato.
    const cases = [
      { name: 'grad-sel', nd: 1, dio: 1, stages: [-1, 3, -1, -1, -1], sel: 1 },  // vetro da due stadi selezionato
      { name: 'coppia', nd: 5, dio: 2, stages: [-1, -1, 2, -1, -1], sel: 2 },     // coppia di ND e diottria da due
      { name: 'dio-sel', nd: 0, dio: 3, stages: [0, -1, -1, -1, -1], sel: 1 },   // posto della diottria selezionato
      { name: 'pieno', nd: 0, dio: 0, stages: [3, -1, 2, 0, 1], sel: -1 },
      { name: 'grad-fondo', nd: 7, dio: 0, stages: [-1, -1, 0, 3, -1], sel: 3 },
    ];
    for (const c of cases) {
      for (const layout of ['land', 'port']) {
        const r = renderScreen({ kind: 'filters' }, layout, { preview: true, noHints: true, nd: c.nd, dio: c.dio, stages: toKeys(c.stages), sel: c.sel });
        zip.file(`ref/${c.name}.${layout}.bmp`, encodeBmp(r.bits, r.w, r.h));
      }
      lines.push(`case ${c.name} ${c.nd} ${c.dio} ${c.sel} ${list(toKeys(c.stages))}`);
    }

    // Tasti per verso, come li vede il simulatore.
    for (const rot of [0, 90, 180, 270]) {
      lines.push(`keys ${rot / 90} ${['A', 'B', 'C', 'D'].map(k => { const a = rockerAction(k, true, rot); return `${a.role || '-'}${a.role ? (a.dir > 0 ? '+' : '-') : ''}`; }).join(' ')}`);
      lines.push(`edit ${rot / 90} ${editNavStep('A', rot)} ${editNavStep('B', rot)} ${keyDir('C', rot)} ${keyDir('D', rot)}`);
    }

    // Regole degli stadi su stati a caso (generatore fisso, così lo zip non cambia).
    let seed = 12345;
    const rnd = n => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
    const nNd = ndSteps().length, nDio = dioSteps().length, n = state.filters.stages.length;
    for (let t = 0; t < 400; t++) {
      // Stadi validi: un vetro in un posto solo, quelli da due con lo stadio dopo libero.
      const st = Array(n).fill(null), free = kit.map(g => g.key);
      for (let i = 0; i < n; i++) {
        if (st[i] !== null || rnd(3) === 0 || !free.length) continue;
        const k = free.splice(rnd(free.length), 1)[0], g = kit.find(x => x.key === k);
        if (g.slots === 2) { if (i + 1 < n && st[i + 1] === null) { st[i] = k; st[i + 1] = undefined; } else free.push(k); }
        else st[i] = k;
      }
      for (let i = 0; i < n; i++) if (st[i] === undefined) st[i] = null;
      const nd0 = rnd(nNd), dio0 = rnd(nDio), nd1 = rnd(nNd), dio1 = rnd(nDio);
      // Prima gli stadi a posto per (nd0, dio0), come sul dispositivo; poi il riordino verso (nd1, dio1).
      const r0 = stageRange(n, nd0, dio0); fitStages(st, r0.lo, r0.hi);
      const before = list(st), was = ndUsed(nd0), now = ndUsed(nd1), r1 = stageRange(n, nd1, dio1);
      fitStages(st, r1.lo, r1.hi, Math.max(0, was - now));
      lines.push(`settle ${nd0} ${dio0} ${nd1} ${dio1} ${before} ${list(st)}`);
      const cells = stageCells(st, nd1, dio1);
      const kinds = { nd: 0, dio: 1, glass: 2, empty: 3, front: 4, cont: 5 };
      lines.push(`cells ${nd1} ${dio1} ${list(st)} ${cells.map(c => `${kinds[c.kind]}:${c.span || 1}:${c.kind === 'glass' ? idx(c.key) : -1}:${c.insert ? 1 : 0}`).join(' ')}`);
      const ok = editableStages(st, nd1, dio1), hi = stageRange(n, nd1, dio1).hi;
      lines.push(`editable ${nd1} ${dio1} ${list(st)} ${ok.join(',') || '-'}`);
      for (const i of ok) lines.push(`options ${nd1} ${dio1} ${list(st)} ${i} ${stageOptions(i, st, hi).map(idx).join(',')}`);
    }
    zip.file('cases.txt', lines.join('\n') + '\n');
    const blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE', compressionOptions: { level: 9 } });
    if (save) download(blob, 'filters_fixture.zip');
    return blob;
  } finally {
    state = keep;
  }
}
