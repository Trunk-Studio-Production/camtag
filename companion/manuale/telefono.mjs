// Rigenera le schermate del configuratore web visto da telefono, per la sezione
// "Mandare le card via Bluetooth" dei due manuali (phone-*.png in manuale/ e manual/).
// Usa Chrome senza finestra con lo stesso progetto di prova di schermate.js; il
// Bluetooth non serve: gli stati della finestra si disegnano con bleUi().
//
// Uso, dalla radice del repo con il server acceso (python3 -m http.server 8766):
//   node companion/manuale/telefono.mjs [http://localhost:8766/companion/]
// CHROME=/percorso/del/browser per usarne un altro (Chromium, Edge, Brave).
import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, writeFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const url = process.argv[2] || 'http://localhost:8766/companion/';
const chrome = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const OUT = { it: join(here), en: join(here, '..', 'manual') };
// Telefono di misura comune, a densità 2: abbastanza nitido, file leggeri.
const PHONE = { width: 390, height: 844, deviceScaleFactor: 2, mobile: true };

const profile = mkdtempSync(join(tmpdir(), 'camtag-tel-'));
const proc = spawn(chrome, ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`,
  '--no-first-run', '--no-default-browser-check', '--hide-scrollbars', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));

try {
  let port;
  for (let i = 0; i < 100 && !port; i++) {
    const f = join(profile, 'DevToolsActivePort');
    if (existsSync(f)) port = readFileSync(f, 'utf8').split('\n')[0];
    else await sleep(100);
  }
  if (!port) throw new Error('Chrome non risponde');
  const page = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((res, rej) => { ws.onopen = res; ws.onerror = rej; });
  let id = 0; const wait = new Map(), events = [];
  ws.onmessage = m => {
    const d = JSON.parse(m.data);
    if (d.id && wait.has(d.id)) { const [res, rej] = wait.get(d.id); wait.delete(d.id); d.error ? rej(new Error(d.error.message)) : res(d.result); }
    else if (d.method) events.push(d.method);
  };
  const cdp = (method, params = {}) => new Promise((res, rej) => { const n = ++id; wait.set(n, [res, rej]); ws.send(JSON.stringify({ id: n, method, params })); });
  const js = async expr => {
    const r = await cdp('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
    return r.result.value;
  };
  const load = async u => {
    events.length = 0;
    await cdp('Page.navigate', { url: u });
    for (let i = 0; i < 200 && !events.includes('Page.loadEventFired'); i++) await sleep(50);
  };
  const shot = async (dir, name) => {
    await sleep(250);
    const { data } = await cdp('Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(dir, `${name}.png`), Buffer.from(data, 'base64'));
    console.log(join(dir, `${name}.png`));
  };

  await cdp('Page.enable');
  await cdp('Emulation.setDeviceMetricsOverride', PHONE);
  await cdp('Emulation.setTouchEmulationEnabled', { enabled: true });
  const demo = readFileSync(join(here, 'schermate.js'), 'utf8');

  for (const lang of ['it', 'en']) {
    // Profilo vuoto: solo la lingua dell'interfaccia, anteprima aperta, niente modalità sviluppatore.
    await load(url);
    await js(`localStorage.clear(); localStorage.setItem('camtag.ui.v1', JSON.stringify({ lang: '${lang}' })); true`);
    await load(url);
    await js(`(async () => {
      await document.fonts.load('800 40px "Inter"'); await document.fonts.load('600 16px "Inter"');
      for (let i = 0; i < 100 && !lensDb; i++) await new Promise(r => setTimeout(r, 100));
      if (!lensDb) throw new Error('database lenti non caricato');
      ${demo}
      state = manualDemoState('${lang}'); sel = { kind: 'lenschart' }; renderLangMenu(); refresh();
      // Il tasto da toccare, cerchiato come le indicazioni del manuale.
      document.head.append(Object.assign(document.createElement('style'), { textContent:
        '.mark { outline: 3px solid #fff !important; outline-offset: 3px; box-shadow: 0 0 0 9px rgba(255,255,255,.18) !important; }' }));
      window.mark = sel => { document.querySelectorAll('.mark').forEach(e => e.classList.remove('mark')); if (sel) document.querySelector(sel).classList.add('mark'); };
      scrollTo(0, 0);
      return true;
    })()`);
    const dir = OUT[lang];
    // Porta un elemento dell'editor subito sotto la parte fissa (barra, anteprima, striscia).
    await js(`window.showEl = el => { const fixed = $('.scr-row').getBoundingClientRect().bottom;
      scrollTo(0, Math.max(0, el.getBoundingClientRect().top + scrollY - fixed - 12)); }; true`);

    // Primi passi: lenti dal database, kit dei filtri, schede manuali.
    await js(`dbSearch = { q: 'signature', brand: '', type: '' }; dbOpen.add('ARRI Signature Prime'); renderEditor();
      showEl([...document.querySelectorAll('#editor input[type=search]')][0]); true`);
    await shot(dir, 'phone-lenses');

    await js(`selectScreen({ kind: 'filters' }); scrollTo(0, 0); true`);
    await shot(dir, 'phone-filters');

    await js(`selectScreen({ kind: 'lenschart' }); scrollTo(0, 0); $('.scr-row').scrollLeft = 0; true`);
    await sleep(400);  // lo scorrimento chiude i menu: si apre dopo
    await js(`$('#addBtn').click(); true`);
    await shot(dir, 'phone-add');
    await js(`hidePop(); selectScreen({ kind: 'lenschart' }); scrollTo(0, 0); $('.scr-row').scrollLeft = 0; true`);

    await js(`mark('#bleBtn')`);
    await shot(dir, 'phone-home');

    await js(`mark(null); $('#bleBtn').click(); mark('#bleStart'); document.activeElement?.blur(); true`);
    await shot(dir, 'phone-ble');

    await js(`mark('#bleFwBtn')`);
    await shot(dir, 'phone-fwbtn');

    await js(`mark(null); bleUi(t('Invio {} di {}…', 4, 9), { busy: true, progress: 0.56,
      detail: t('{} · {} di {} KB{}', '/camtag/02_filters.p.bmp', 238, 425, '') }); true`);
    await shot(dir, 'phone-send');

    await js(`bleUi(t("Conferma sull'X3: premi A, B, C o D per installare la {}.", FW_VERSION), { busy: true, detail: t('X o Y annullano.') }); true`);
    await shot(dir, 'phone-fw');
  }
  ws.close();
} finally {
  proc.kill();
  await sleep(300);
  rmSync(profile, { recursive: true, force: true });
}
