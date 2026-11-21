// Rigenera le schermate del manuale dal simulatore del configuratore web, con un
// progetto di prova: dopo ogni cambio di layout basta rifarle. Due lingue, card e
// scritte dell'X3 nella stessa: 'it' per companion/manuale/, 'en' per companion/manual/.
//
// Uso: servi la radice del repo (python3 -m http.server 8766), apri
// http://localhost:8766/companion/, incolla questo file nella console ed esegui
// makeManualShots('it') e makeManualShots('en'): ognuna scarica
// manuale-schermate-<lingua>.zip, da scompattare nella cartella del suo manuale
// sopra i PNG che ci sono. Lo stato del configuratore web non viene toccato (né
// salvato). makeManualShots(lingua, url) invece manda ogni PNG con un PUT a
// url + nome.png, per chi ha un piccolo server che li scrive su disco.
async function makeManualShots(lang = 'it', putUrl = null) {
  const keep = state, keepSim = { ...sim };
  const files = [];
  try {
    if (await document.fonts.load(`800 40px "Inter"`).then(f => !f.length)) throw new Error('font Inter non caricato');
    for (let i = 0; i < 50 && !lensDb; i++) await new Promise(r => setTimeout(r, 100));
    if (!lensDb) throw new Error('database lenti non caricato');

    // Progetto di prova: sei prime, un kit filtri con ND, diottria e vetri, una crew.
    state = migrate(freshState());
    state.lang = state.deviceLang = lang;
    state.production = 'DEMO';
    for (const f of ['18', '25', '35', '47', '75', '125']) addDbLens(lensById.get(`arri-signature-prime-${f}`), 'LPL');
    state.filters = {
      ndFormat: 'density', ndType: 'IRND', nd: [0.3, 0.6, 0.9, 1.2, 1.8], ndCombos: [],
      dio: [{ v: 0.5, split: false }, { v: 1, split: false }, { v: 2, split: false }],
      effects: [{ id: null, name: 'Black Pro-Mist', strengths: [{ s: '1/8' }, { s: '1/4' }] }, { id: null, name: 'Pola', strengths: [{ s: '' }] }],
      stages: [null, 'c:Black Pro-Mist|1/4', null, null], previewNd: 2, previewDio: 1,
    };
    state.cards = [DEFAULTS.crew()];
    state.off = { ...freshOff(), text: 'CAMERA A' };
    fitStart();

    const cv = $('#simScreen');
    const shot = async (name, o = {}) => {
      Object.assign(sim, { sig: '', i: o.i ?? 0, rot: o.rot ?? 0, locked: false, off: !!o.off, keyLock: !!o.keyLock, edit: null, msg: '' });
      renderSim();
      if (o.after) { o.after(); renderSim(); }
      if (o.draw) o.draw(cv.getContext('2d'), cv.width, cv.height);
      files.push([`${name}.png`, await new Promise(r => cv.toBlob(r, 'image/png'))]);
    };

    await shot('lens-port', { i: 0 });
    await shot('lens-land', { i: 0, rot: 90 });
    await shot('filters-port', { i: 1 });
    await shot('filters-land', { i: 1, rot: 90 });
    await shot('filters-edit', { i: 1, after: () => { sim.edit = { i: editableStages(sim.stages, sim.ndShown, sim.dioShown)[0] }; } });
    await shot('crew-port', { i: 2 });
    await shot('lock', { i: 0, keyLock: true });
    await shot('off', { off: true });
    // Schermata di ricezione: come drawReceive nel firmware (il simulatore non la mostra).
    await shot('bluetooth', { draw: (ctx, W, H) => {
      const mid = H / 2;
      ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, W, H); ctx.fillStyle = '#000';
      strokeBox(ctx, 16, 16, W - 32, H - 32, 4);
      tinyCentered(ctx, W, mid - 150, 'BLUETOOTH', 5);
      tinyCentered(ctx, W, mid - 60, 'CAMTAG-1A2B', 3);
      const en = lang === 'en';
      tinyCentered(ctx, W, mid + 20, en ? 'WAITING' : 'IN ATTESA', 3);
      tinyCentered(ctx, W, mid + 80, en ? 'OPEN CAMTAG ON YOUR PHONE' : 'APRI CAMTAG SUL TELEFONO', 2);
      tinyCentered(ctx, W, mid + 130, en ? 'X OR Y TO EXIT' : 'X O Y PER USCIRE', 2);
      tinyCentered(ctx, W, H - 100, `FIRMWARE ${FW_VERSION}`, 2);
      tinyCentered(ctx, W, H - 76, en ? 'DEVELOPED BY IACOPO BOCCALARI' : 'SVILUPPATO DA IACOPO BOCCALARI', 2);
      tinyCentered(ctx, W, H - 52, 'TRUNK STUDIO - TRUNKSTUDIO.IT', 2);
    } });
    // Conferma dell'aggiornamento del firmware: come drawFirmwareAsk nel firmware.
    await shot('firmware', { draw: (ctx, W, H) => {
      const mid = H / 2, en = lang === 'en';
      ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, W, H); ctx.fillStyle = '#000';
      tinyCentered(ctx, W, mid - 170, 'FIRMWARE', 5);
      tinyCentered(ctx, W, mid - 80, en ? 'UPDATE THE FIRMWARE?' : 'AGGIORNARE IL FIRMWARE?', 3);
      tinyCentered(ctx, W, mid - 20, en ? `FROM 0.3.0 TO ${FW_VERSION}` : `DA 0.3.0 A ${FW_VERSION}`, 3);
      tinyCentered(ctx, W, mid + 60, en ? 'A B C OR D: UPDATE' : 'A B C O D: AGGIORNA', 2);
      tinyCentered(ctx, W, mid + 100, en ? 'X OR Y: CANCEL' : 'X O Y: ANNULLA', 2);
      drawKeyLabels(ctx, 0);
    } });
    // Scelta della lingua al primo avvio: come showLangChoice nel firmware.
    await shot('lang', { draw: (ctx, W, H) => {
      const mid = H / 2, sc = 4, lh = 7 * sc, gap = 24, pad = 26, l1 = 'A / B  ENGLISH', l2 = 'C / D  ITALIANO';
      ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.fillStyle = '#fff'; ctx.fillRect(0, 0, W, H); ctx.fillStyle = '#000';
      tinyCentered(ctx, W, mid - 170, 'CAMTAG', 6);
      tinyCentered(ctx, W, mid - 80, 'LANGUAGE - LINGUA', 3);
      const bw = Math.max(tinyWidth(l1, sc), tinyWidth(l2, sc)) + 2 * pad, bh = 2 * lh + gap + 2 * pad, bx = Math.trunc((W - bw) / 2), by = mid - 30;
      strokeBox(ctx, bx, by, bw, bh, 4);
      tinyText(ctx, bx + pad, by + pad, l1, sc); tinyText(ctx, bx + pad, by + pad + lh + gap, l2, sc);
      drawKeyLabels(ctx, 0);
    } });
    // Senza card: come showMessage nel firmware.
    state.auto = { lenschart: false, filters: false, filterlist: false }; state.cards = [];
    await shot('nocard');
  } finally {
    state = keep; Object.assign(sim, keepSim, { sig: '' }); renderSim();
  }

  if (putUrl) {
    for (const [name, blob] of files) await fetch(putUrl + name, { method: 'PUT', body: blob });
    return files.map(([n]) => n);
  }
  const zip = new JSZip();
  for (const [name, blob] of files) zip.file(name, blob);
  const a = document.createElement('a');
  a.href = URL.createObjectURL(await zip.generateAsync({ type: 'blob' }));
  a.download = `manuale-schermate-${lang}.zip`; a.click();
  return files.map(([n]) => n);
}
