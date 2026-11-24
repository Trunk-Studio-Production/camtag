#!/usr/bin/env python3
"""Prova guidata dell'X3 dal terminale, con riscontro dal vivo.

Legge la seriale di un firmware compilato con CAMTAG_DEBUG e mostra subito ogni
tasto premuto e i valori dell'accelerometro, un passo alla volta e senza limiti
di tempo. Con la USB collegata a un computer l'X3 resta sveglio; se lo spegni
aspetta che torni.

    .venv/bin/python scripts/taratura.py tasti
    .venv/bin/python scripts/taratura.py versi [--carica] [--out risultato.json]
    .venv/bin/python scripts/taratura.py card
    .venv/bin/python scripts/taratura.py filtri
    .venv/bin/python scripts/taratura.py blocco
    .venv/bin/python scripts/taratura.py reattivita
    .venv/bin/python scripts/taratura.py log [--file x3.log]
    .venv/bin/python scripts/taratura.py batteria [--azzera] [--leggi registro.csv] [--bianco N]
    .venv/bin/python scripts/taratura.py usb [--prove N]

`card` prova con le card vere sopra: verso della card e direzione dei bilancieri
sulla lens chart nelle quattro posizioni, poi il blocco della rotazione.

`filtri` prova la schermata filtri (card con la schermata filtri v3 sopra):
indicazioni e bilancieri di ND e diottria nelle quattro posizioni, riordino
degli stadi, modifica degli stadi e indicazioni con la rotazione bloccata.

`reattivita` confronta ogni pressione sentita dall'hardware con quelle lette dal
firmware: una pressione persa durante un aggiornamento dello schermo è colpa del
firmware, una persa a schermo fermo va cercata nell'hardware.

`log` mostra dal vivo quello che l'X3 scrive (tasti, versi, card, ricezione) e
con `--file` lo salva anche, accelerometro compreso.

`batteria` scarica il registro dei consumi che l'X3 tiene sulla microSD
(docs/batteria.md), lo salva in misure/ e fa i conti: corrente media da sveglio,
a riposo e da spento, e quanto durerebbe la batteria. La misura vale solo per i
tratti a batteria: con la USB collegata si carica. `--azzera` cancella il registro
sull'X3 dopo averlo salvato; `--leggi` rifà i conti su un file già salvato
(anche quello scaricato via Bluetooth con "Registro batteria" nel configuratore web);
`--bianco N` porta a N secondi l'attesa prima del bianco da spento, per provarla
(0 rimette i tre giorni). Le righe D delle diagnosi da spento (0.5.6-0.5.11) si
leggono ancora nei registri vecchi.

`usb` prova il cavo collegato con l'X3 a riposo: fa staccare e riattaccare il cavo
N volte (10), misura dopo quanto compare la porta e legge dall'X3 perché si è
svegliato e in quanto il computer lo ha riconosciuto (docs/batteria.md).

`--carica` compila e carica il firmware prima di cominciare.
"""
import argparse
import collections
import glob
import json
import re
import select
import statistics
import subprocess
import sys
import time
from pathlib import Path

import serial

REPO = Path(__file__).resolve().parent.parent
B, DIM, OK, BAD, WARN, X = '\033[1m', '\033[2m', '\033[32m', '\033[31m', '\033[33m', '\033[0m'

# Tasto logico dell'SDK -> tasto fisico secondo kKeyA…kKeyD, kKeyX, kKeyY in device/src/main.cpp.
KEY_NAMES = {0: 'A', 1: 'B', 2: 'C', 3: 'D', 4: 'X', 5: 'Y', 6: 'power'}
KEY_STEPS = [
    ('A', 'bilanciere in basso a sinistra, capo sinistro'),
    ('B', 'bilanciere in basso a sinistra, capo destro'),
    ('C', 'bilanciere in basso a destra, capo sinistro'),
    ('D', 'bilanciere in basso a destra, capo destro'),
    ('X', 'tasto sul lato sinistro'),
    ('Y', 'tasto sul lato destro'),
]
POSITIONS = [
    'verticale, tasti in basso',
    'un quarto di giro in senso orario: tasti a sinistra',
    'capovolto: tasti in alto',
    'un quarto di giro in senso antiorario dalla prima: tasti a destra',
]
READINGS = {
    '1': 'dritta',
    '2': 'capovolta',
    '3': 'di lato, cima delle lettere a sinistra',
    '4': 'di lato, cima delle lettere a destra',
}
FLAT_G = 0.85  # come kFlatG nel firmware


class X3:
    def __init__(self, port):
        self.fixed_port = port
        self.ser = None
        self.buf = b''
        self.imu = collections.deque(maxlen=4)
        self.verso = None
        self.keys = collections.deque()
        self.card = None    # (indice, nome del file) dell'ultima card disegnata
        self.boots = 0      # collegamenti: ognuno riavvia l'X3
        self.events = collections.deque()  # righe [camtag] rotation …
        self.on_line = None  # chiamata per ogni riga ricevuta (modalità log)
        self.got_key = None  # ultimo tasto visto da press_filters
        self.flt_cfg = None  # riepilogo di screen.ini: {'nd': passi, 'dio': passi, 'vetri', 'stadi'}
        self.flt = None      # ultimo stato della schermata filtri
        self.flt_events = collections.deque()  # 'nd', 'diottria', 'riordino', 'modifica', 'fine modifica', 'scaduta'
        self.posizione = None  # posizione vera con la rotazione bloccata

    def wait_port(self):
        told = False
        while True:
            found = [self.fixed_port] if self.fixed_port else glob.glob('/dev/cu.usbmodem*')
            if found and Path(found[0]).exists():
                return found[0]
            if not told:
                print(f'\r\033[K{WARN}X3 non visibile su USB: premi un tasto; se è spento tieni premuto il power.{X}')
                told = True
            time.sleep(0.2)

    def connect(self):
        port = self.wait_port()
        time.sleep(0.5)
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.dtr = False
        self.ser.rts = False
        self.ser.open()
        self.buf = b''
        self.imu.clear()
        self.card = None  # dopo un riavvio la card a schermo può non essere ridisegnata
        self.boots += 1
        print(f'{DIM}collegato a {port}: aspetto che lo schermo si fermi…{X}')

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except OSError:
                pass
            self.ser = None

    def pump(self):
        """Legge quello che è arrivato. La porta sparisce quando l'X3 si spegne o va a riposo."""
        try:
            self.buf += self.ser.read(4096)
        except (serial.SerialException, OSError):
            self.close()
            print(f'\r\033[K{WARN}X3 spento o scollegato.{X}')
            self.connect()
            return
        while b'\n' in self.buf:
            raw, self.buf = self.buf.split(b'\n', 1)
            line = raw.decode(errors='replace').strip()
            if self.on_line:
                self.on_line(line)
            m = re.match(r'\[imu\] ax=(-?[\d.]+) ay=(-?[\d.]+) az=(-?[\d.]+)', line)
            if m:
                self.imu.append(tuple(float(v) for v in m.groups()))
                continue
            m = re.match(r'\[camtag\] verso (\d)', line)
            if m:
                self.verso = int(m.group(1))
                continue
            m = re.match(r'\[camtag\] tasto logico (\d)', line)
            if m:
                self.keys.append(int(m.group(1)))
                continue
            m = re.match(r'\[camtag\] card (\d+) rot \d+: \S*/([^/]+)$', line)
            if m:
                self.card = (int(m.group(1)), m.group(2))
                continue
            m = re.match(r'\[camtag\] rotation (\w+)', line)
            if m:
                self.events.append(m.group(1))
                continue
            m = re.match(r'\[camtag\] posizione (\d)', line)
            if m:
                self.posizione = int(m.group(1))
                continue
            m = re.match(r'\[filtri\] screen\.ini: nd=(\d+) dio=(\d+) vetri=(\d+) stadi=(\d+)', line)
            if m:
                self.flt_cfg = dict(zip(('nd', 'dio', 'vetri', 'stadi'), map(int, m.groups())))
                continue
            m = re.match(r'\[filtri\] (.+?) nd=(\d+) dio=(\d+) stadi_nd=\d+ stadi_dio=\d+ sel=(-?\d+) stadi=(\S*)', line)
            if m:
                self.flt = {'nd': int(m.group(2)), 'dio': int(m.group(3)), 'sel': int(m.group(4)), 'stadi': m.group(5)}
                self.flt_events.append(m.group(1))
                continue
            if line.startswith('[filtri] modifica chiusa'):
                self.flt_events.append('scaduta')

    def sample(self):
        if not self.imu:
            return None
        return tuple(round(statistics.median(v[i] for v in self.imu), 2) for i in range(3))

    def wait_key(self, status):
        """Aspetta un tasto mostrando una riga di stato che si aggiorna da sola."""
        self.keys.clear()
        while not self.keys:
            self.pump()
            sys.stdout.write(f'\r\033[K   {status()}')
            sys.stdout.flush()
        sys.stdout.write('\r\033[K')
        return self.keys.popleft()


def upload(x3):
    port = x3.wait_port()
    print(f'{DIM}compilo e carico il firmware (lo schermo lampeggia)…{X}')
    # Da un worktree il .venv è quello del checkout principale: pio sta accanto al Python in uso.
    pio = REPO / '.venv/bin/pio'
    if not pio.exists():
        pio = Path(sys.executable).parent / 'pio'
    run = subprocess.run([str(pio), 'run', '-e', 'x3', '-t', 'upload', '--upload-port', port],
                         cwd=REPO / 'device', capture_output=True, text=True)
    if run.returncode:
        print(run.stdout[-1500:])
        sys.exit(f'{BAD}caricamento fallito{X}')
    print(f'{OK}firmware caricato{X}')
    time.sleep(2)


def ask(question, answers):
    while True:
        a = input(question).strip()
        if a in answers:
            return a
        print(f'   rispondi con uno tra: {", ".join(answers)}')


def test_keys(x3):
    print(f'\n{B}Prova dei tasti{X}: premi il tasto indicato, una pressione breve.\n')
    wrong = 0
    for i, (name, where) in enumerate(KEY_STEPS, 1):
        print(f'{B}{i}/6  Premi {name}{X} — {where}')
        got = x3.wait_key(lambda: f'{DIM}in attesa…{X}')
        seen = KEY_NAMES.get(got, '?')
        if seen == name:
            print(f'   {OK}✓ ricevuto {name}{X} (tasto logico {got})')
        else:
            wrong += 1
            print(f'   {BAD}✗ il firmware ha letto {seen}{X} (tasto logico {got}), mi aspettavo {name}')
    print(f'\n{OK}Tutti i tasti corrispondono.{X}' if not wrong else f'\n{BAD}{wrong} tasti da correggere.{X}')


def test_positions(x3, out):
    print(f'\n{B}Prova dei versi{X}: tieni l\'X3 dritto davanti a te, non appoggiato.')
    print('In ogni posizione aspetta che lo schermo si fermi, poi premi A.\n')
    results = []
    for i, where in enumerate(POSITIONS, 1):
        print(f'{B}{i}/4  {where}{X}')

        def status():
            s = x3.sample()
            if not s:
                return f'{DIM}aspetto l\'accelerometro…{X}'
            flat = f'  {WARN}troppo piatto: raddrizzalo{X}' if abs(s[2]) > FLAT_G else ''
            return f'ax {s[0]:+.2f}  ay {s[1]:+.2f}  az {s[2]:+.2f}   verso scelto {x3.verso}{flat}   {DIM}premi A{X}'

        while True:
            x3.wait_key(status)
            s = x3.sample()
            if s and abs(s[2]) <= FLAT_G:
                break
            print(f'   {WARN}era troppo piatto: raddrizzalo e premi di nuovo A{X}')
        print(f'   {OK}✓ letto{X}  ax {s[0]:+.2f}  ay {s[1]:+.2f}  az {s[2]:+.2f}   verso scelto {x3.verso}')
        for k, text in READINGS.items():
            print(f'      {k} = {text}')
        a = ask('   Come si legge la scritta sullo schermo? ', READINGS)
        results.append({'posizione': i, 'ax': s[0], 'ay': s[1], 'az': s[2], 'verso': x3.verso, 'scritta': READINGS[a]})
        print()
    print(f'{B}Riepilogo{X}')
    for r in results:
        print(f'   {r["posizione"]}  ax {r["ax"]:+.2f}  ay {r["ay"]:+.2f}  verso {r["verso"]}  scritta {r["scritta"]}')
    if out:
        Path(out).write_text(json.dumps(results, indent=1, ensure_ascii=False))
    print(f'\n{OK}Fatto.{X}')


class Restart(Exception):
    """L'X3 si è riavviato (spento o porta riaperta): si rifà il passo."""


def ask_live(x3, question, answers):
    """Come ask, ma intanto legge la seriale: se l'X3 si spegne lo si vede subito."""
    boots = x3.boots
    while True:
        sys.stdout.write(question)
        sys.stdout.flush()
        while not select.select([sys.stdin], [], [], 0.02)[0]:
            x3.pump()
            if x3.boots != boots:
                raise Restart
        a = sys.stdin.readline().strip()
        if a in answers:
            return a
        print(f'   rispondi con uno tra: {", ".join(answers)}')


def wait_until(x3, done, status, restart=True):
    boots = x3.boots
    while not done():
        x3.pump()
        if restart and x3.boots != boots:
            sys.stdout.write('\r\033[K')
            raise Restart
        sys.stdout.write(f'\r\033[K   {status()}')
        sys.stdout.flush()
        time.sleep(0.02)
    sys.stdout.write('\r\033[K')


def is_lens(card):
    return card is not None and re.match(r'\d+_lenschart(-\d+)?\.', card[1]) is not None


# Spostamento sulla lens chart: +1 avanti, -1 indietro (anche girando in tondo).
def page_step(before, after):
    d = after - before
    return 0 if d == 0 else (1 if d == 1 or d < -1 else -1)


def press_rocker(x3, key, expected, where):
    before = x3.card
    print(f'   Premi {B}{key}{X}  {DIM}({where}){X}')
    x3.keys.clear()
    wait_until(x3, lambda: x3.card != before or bool(x3.keys and KEY_NAMES.get(x3.keys[-1]) != key),
               lambda: f'{DIM}in attesa di {key}…{X}')
    if x3.card == before:
        print(f'   {BAD}✗ nessun cambio di pagina{X}')
        return False
    got = page_step(before[0], x3.card[0])
    ok = got == expected
    word = {1: 'avanti', -1: 'indietro'}
    print(f'   {OK + "✓" if ok else BAD + "✗"} {key}: {word.get(got, "?")} → {x3.card[1]}{X}'
          + ('' if ok else f'  (doveva andare {word[expected]})'))
    return ok


# Posizioni come POSITIONS: verso atteso dal firmware e dove stanno i bilancieri per chi guarda.
# Specifica: lensPageDir nel configuratore web (avanza il bilanciere a destra o in basso).
CARD_STEPS = [
    (3, 'A|B a sinistra, C|D a destra', -1, 1),
    (2, 'A|B in alto, C|D in basso', -1, 1),
    (1, 'A|B a destra, C|D a sinistra', 1, -1),
    (0, 'A|B in basso, C|D in alto', 1, -1),
]


def test_cards(x3, start=1):
    print(f'\n{B}Prova delle card{X}: tieni l\'X3 in mano, non appoggiato.\n')
    bad = []
    for i, ((verso, rockers, ab, cd), where) in enumerate(zip(CARD_STEPS, POSITIONS), 1):
        if i < start:
            continue
        while True:
            try:
                bad += card_position(x3, i, verso, rockers, ab, cd, where)
                break
            except Restart:
                print(f'   {WARN}l\'X3 si è riavviato: rifaccio la posizione {i}{X}')
        print()

    while True:
        try:
            bad += lock_test(x3)
            break
        except Restart:
            print(f'   {WARN}l\'X3 si è riavviato: rifaccio la prova del blocco{X}')
            # dopo il riavvio il blocco resta salvato: lo si toglie con Y tenuto
            print(f'   Se lo schermo dice ROTAZIONE BLOCCATA, tieni Y per sbloccare prima di ripartire.')
    print(f'\n{OK}Tutto come previsto.{X}' if not bad else f'\n{BAD}Da correggere:{X}\n   ' + '\n   '.join(bad))


def to_lens_page(x3):
    if not is_lens(x3.card):
        print('   Premi X o Y fino a una pagina della lens chart.')
        wait_until(x3, lambda: is_lens(x3.card), lambda: f'{DIM}in attesa…{X}')


def card_position(x3, i, verso, rockers, ab, cd, where):
    bad = []
    print(f'{B}{i}/4  {where}{X}')
    wait_until(x3, lambda: x3.verso == verso,
               lambda: f'{DIM}aspetto che la card giri (ora verso {x3.verso}){X}')
    to_lens_page(x3)
    time.sleep(1.5)  # lascia finire il disegno
    x3.pump()
    if ask_live(x3, f'   La card {x3.card[1]} si legge dritta e intera? [s/n] ', ['s', 'n']) == 'n':
        bad.append(f'{where}: card storta o tagliata')
    if not press_rocker(x3, 'A', ab, rockers):
        bad.append(f'{where}: A nel verso sbagliato')
    if not press_rocker(x3, 'C', cd, rockers):
        bad.append(f'{where}: C nel verso sbagliato')
    return bad


def lock_test(x3):
    bad = []
    print(f'{B}Blocco della rotazione{X}')
    print('   Rimetti l\'X3 verticale con i tasti in basso.')
    wait_until(x3, lambda: x3.verso == 3, lambda: f'{DIM}aspetto il verso verticale (ora {x3.verso}){X}')
    print(f'   Tieni premuto {B}Y{X} finché compare ROTAZIONE BLOCCATA.')
    x3.events.clear()
    wait_until(x3, lambda: 'locked' in x3.events, lambda: f'{DIM}in attesa…{X}')
    print(f'   {OK}✓ bloccata{X}')
    print('   Gira l\'X3 di un quarto in senso orario (tasti a sinistra) e aspetta un paio di secondi.')
    if ask_live(x3, '   La card è rimasta com\'era, senza girare? [s/n] ', ['s', 'n']) == 'n':
        bad.append('blocco: la card gira lo stesso')
    to_lens_page(x3)
    print(f'   {DIM}Con la rotazione bloccata i bilancieri restano quelli del verso bloccato (A|B arretra).{X}')
    if not press_rocker(x3, 'A', -1, 'A|B, come in verticale'):
        bad.append('blocco: A nel verso sbagliato')
    print(f'   Tieni premuto {B}Y{X} per sbloccare.')
    wait_until(x3, lambda: 'auto' in x3.events, lambda: f'{DIM}in attesa…{X}')
    x3.verso = None
    wait_until(x3, lambda: x3.verso == 2, lambda: f'{DIM}aspetto che la card giri…{X}')
    print(f'   {OK}✓ sbloccata, la card ha seguito il verso{X}')

    return bad


# --- Schermata filtri -----------------------------------------------------------------
# Per posizione (come POSITIONS): verso del firmware, tasti come keys.<gradi> in
# screen.ini (rockerAction nel configuratore web) e indicazioni in ordine per chi guarda.
FILTER_STEPS = [
    (3, {'A': 'dio-', 'B': 'dio+', 'C': 'nd-', 'D': 'nd+'}, 'in basso, da sinistra: -DIO +DIO -ND +ND'),
    (2, {'A': 'nd+', 'B': 'nd-', 'C': 'dio+', 'D': 'dio-'}, 'a sinistra, dall\'alto: +ND -ND +DIO -DIO'),
    (1, {'A': 'dio+', 'B': 'dio-', 'C': 'nd+', 'D': 'nd-'}, 'in alto, da sinistra: -ND +ND -DIO +DIO'),
    (0, {'A': 'dio-', 'B': 'dio+', 'C': 'nd-', 'D': 'nd+'}, 'a destra, dall\'alto: +ND -ND +DIO -DIO'),
]


def is_filters(card):
    return card is not None and re.match(r'\d+_filters\.', card[1]) is not None


def to_filters_page(x3):
    if not is_filters(x3.card):
        print('   Premi X o Y fino alla schermata filtri.')
        wait_until(x3, lambda: is_filters(x3.card), lambda: f'{DIM}in attesa…{X}')


# Reazioni a un tasto; il riordino e la fine della modifica per tempo arrivano da soli.
KEY_EVENTS = {'nd', 'diottria', 'modifica', 'fine modifica'}


def press_filters(x3, key, where, done=None, wait=1.5):
    """Fa premere un tasto e aspetta la reazione della schermata filtri (o il tempo `wait`
    dopo la pressione, se il firmware non cambia nulla, per esempio a fine scala)."""
    before = dict(x3.flt or {})
    print(f'   Premi {B}{key}{X}  {DIM}({where}){X}')
    x3.keys.clear()
    x3.flt_events.clear()
    seen = []

    def finished():
        if x3.keys and not seen:
            seen.append(time.time())
        if done:
            return done()
        return bool(KEY_EVENTS & set(x3.flt_events)) or bool(seen and time.time() - seen[0] > wait)

    wait_until(x3, finished, lambda: f'{DIM}in attesa di {key}…{X}')
    got = KEY_NAMES.get(x3.keys[0]) if x3.keys else None
    x3.got_key = got
    if got and got != key:
        print(f'   {WARN}hai premuto {got}, non {key}{X}')
    return before, dict(x3.flt or {})


def leave_edit(x3):
    """Un A o B tenuto troppo apre la modifica degli stadi: si esce con Y."""
    print(f'   {WARN}hai tenuto premuto: si è aperta la modifica degli stadi.{X} Premi {B}Y{X} per uscire.')
    x3.flt_events.clear()
    wait_until(x3, lambda: 'fine modifica' in x3.flt_events, lambda: f'{DIM}in attesa di Y…{X}')


def check_value(x3, key, role, where, sign):
    name = {'nd': 'ND', 'dio': 'diottria'}[role]
    top = x3.flt_cfg[role] - 1
    while True:
        before, after = press_filters(x3, key, where)
        if 'modifica' in x3.flt_events and 'fine modifica' not in x3.flt_events:
            leave_edit(x3)
        elif x3.got_key and x3.got_key != key:
            pass
        else:
            break
        print(f'   {DIM}rifacciamo: pressione breve su {key}{X}')
    if not before:
        print(f'   {WARN}stato dei filtri non ancora letto: riprova{X}')
        return False
    want = max(0, min(top, before[role] + sign))
    ok = after.get(role) == want
    word = 'su' if sign > 0 else 'giù'
    note = '' if want != before[role] else f' {DIM}(era già in fondo alla scala){X}'
    print(f'   {OK + "✓" if ok else BAD + "✗"} {key}: {name} {before[role]} → {after.get(role)}{X}{note}'
          + ('' if ok else f'  (doveva andare {word}, a {want})'))
    return ok


def filters_position(x3, i, verso, keys, hints, where):
    bad = []
    print(f'{B}{i}/4  {where}{X}')
    wait_until(x3, lambda: x3.verso == verso, lambda: f'{DIM}aspetto che la schermata giri (ora verso {x3.verso}){X}')
    to_filters_page(x3)
    time.sleep(1.5)
    x3.pump()
    roles = ['nd'] + (['dio'] if x3.flt_cfg['dio'] else [])
    shown = hints if x3.flt_cfg['dio'] else re.sub(r' ?[-+]DIO', '', hints)
    if ask_live(x3, f'   Indicazioni dei tasti {shown}, dritte e accanto ai tasti? [s/n] ', ['s', 'n']) == 'n':
        bad.append(f'{where}: indicazioni sbagliate')
    for role in roles:
        for sign in (1, -1):
            key = next(k for k, a in keys.items() if a == f'{role}{"+" if sign > 0 else "-"}')
            if not check_value(x3, key, role, f'{"+" if sign > 0 else "-"}{role.upper()}', sign):
                bad.append(f'{where}: {key} non fa {"+" if sign > 0 else "-"}{role.upper()}')
    return bad


def edit_test(x3):
    bad = []
    print(f'{B}Modifica degli stadi{X}')
    print('   Rimetti l\'X3 verticale con i tasti in basso.')
    wait_until(x3, lambda: x3.verso == 3, lambda: f'{DIM}aspetto il verso verticale (ora {x3.verso}){X}')
    to_filters_page(x3)
    if not x3.flt_cfg['stadi'] or not x3.flt_cfg['vetri']:
        print(f'   {WARN}niente stadi o kit dei vetri vuoto: niente da modificare{X}')
        return bad
    # ND e diottria occupano stadi: a "—" li lasciano tutti liberi per i vetri.
    for role, key, name in (('nd', 'C', '-ND'), ('dio', 'A', '-DIO')):
        if role == 'dio' and not x3.flt_cfg['dio']:
            continue
        if (x3.flt or {}).get(role, 0) > 0:
            print(f'   Porta {"ND" if role == "nd" else "diottria"} a — : premi {B}{key}{X} finché non arriva in fondo.')
        while (x3.flt or {}).get(role, 0) > 0:
            press_filters(x3, key, name)
    print(f'   Tieni premuto {B}A{X} finché una riga degli stadi va in negativo.')
    x3.flt_events.clear()
    wait_until(x3, lambda: 'modifica' in x3.flt_events, lambda: f'{DIM}in attesa…{X}')
    print(f'   {OK}✓ modifica aperta{X} sullo stadio {x3.flt["sel"] + 1}; le indicazioni ora sono ▲ ▼ − +')
    if ask_live(x3, '   Sotto ci sono ▲ ▼ − + e la riga selezionata è in negativo? [s/n] ', ['s', 'n']) == 'n':
        bad.append('modifica: indicazioni o selezione non visibili')
    before, after = press_filters(x3, 'B', '▼, stadio dopo')
    if after['sel'] < before['sel']:
        bad.append('modifica: B torna indietro invece di andare avanti')
        print(f'   {BAD}✗ la selezione è tornata indietro{X}')
    else:
        print(f'   {OK}✓{X} stadio {before["sel"] + 1} → {after["sel"] + 1}' + ('' if after['sel'] > before['sel'] else f' {DIM}(era l\'ultimo selezionabile){X}'))
    before, after = press_filters(x3, 'D', '+, vetro dopo')
    ok = after['stadi'] != before['stadi']
    print(f'   {OK + "✓" if ok else BAD + "✗"} stadi {before["stadi"]} → {after["stadi"]}{X}')
    if not ok:
        bad.append('modifica: D non cambia il vetro')
    print(f'   Ora {B}non toccare nulla{X}: dopo 5 secondi la modifica si chiude da sola.')
    x3.flt_events.clear()
    t0 = time.time()
    wait_until(x3, lambda: 'fine modifica' in x3.flt_events or time.time() - t0 > 12,
               lambda: f'{DIM}{time.time() - t0:.0f} s…{X}')
    if 'scaduta' in x3.flt_events:
        print(f'   {OK}✓ chiusa da sola dopo {time.time() - t0:.0f} s{X}')
    else:
        bad.append('modifica: non si chiude da sola')
        print(f'   {BAD}✗ la modifica non si è chiusa{X}')
    print(f'   Tieni premuto {B}B{X} per riaprirla, poi premi {B}Y{X} per uscire.')
    x3.flt_events.clear()
    wait_until(x3, lambda: 'modifica' in x3.flt_events, lambda: f'{DIM}in attesa di B tenuto…{X}')
    card = x3.card
    _, after = press_filters(x3, 'Y', 'esce dalla modifica', done=lambda: 'fine modifica' in x3.flt_events)
    ok = x3.card == card and after['sel'] == -1
    print(f'   {OK + "✓ uscito, stessa schermata" if ok else BAD + "✗ Y ha cambiato schermata"}{X}')
    if not ok:
        bad.append('modifica: Y cambia schermata invece di uscire')
    return bad


def filters_lock_test(x3):
    bad = []
    print(f'{B}Rotazione bloccata{X}')
    print(f'   Con l\'X3 verticale (tasti in basso) tieni premuto {B}Y{X} finché compare ROTAZIONE BLOCCATA.')
    x3.events.clear()
    wait_until(x3, lambda: 'locked' in x3.events, lambda: f'{DIM}in attesa…{X}')
    print('   Gira l\'X3 di un quarto in senso orario (tasti a sinistra).')
    x3.posizione = None
    wait_until(x3, lambda: x3.posizione == 2, lambda: f'{DIM}aspetto la nuova posizione…{X}')
    time.sleep(1.5)
    to_filters_page(x3)
    if ask_live(x3, '   Schermata e indicazioni sono rimaste ferme, girate insieme all\'X3 (nessuna si è spostata)? [s/n] ',
                ['s', 'n']) == 'n':
        bad.append('bloccata: le indicazioni non restano ferme con la schermata')
    # Il blocco ferma anche i bilancieri: valgono quelli della posizione verticale.
    if not check_value(x3, 'D', 'nd', '+ND: D come in verticale, anche se ora è in alto', 1):
        bad.append('bloccata: D non fa più +ND')
    print(f'   Tieni premuto {B}Y{X} per sbloccare.')
    wait_until(x3, lambda: 'auto' in x3.events, lambda: f'{DIM}in attesa…{X}')
    print(f'   {OK}✓ sbloccata{X}')
    return bad


def test_filters(x3, start=1, only=None):
    print(f'\n{B}Prova della schermata filtri{X}: tieni l\'X3 in mano, non appoggiato.')
    t0 = time.time()
    wait_until(x3, lambda: x3.flt_cfg is not None or time.time() - t0 > 10,
               lambda: f'{DIM}aspetto che l\'X3 legga screen.ini…{X}', restart=False)
    cfg = x3.flt_cfg
    if not cfg:
        print(f'{BAD}Sulle card non c\'è una schermata filtri v3{X}: mandale dal configuratore web (Bluetooth) e riprova.')
        return
    print(f'   screen.ini: {cfg["nd"]} passi di ND, {cfg["dio"]} di diottria, {cfg["vetri"]} vetri, {cfg["stadi"]} stadi\n')
    bad = []
    for i, ((verso, keys, hints), where) in enumerate(zip(FILTER_STEPS, POSITIONS), 1):
        if i < start or only:
            continue
        while True:
            try:
                bad += filters_position(x3, i, verso, keys, hints, where)
                break
            except Restart:
                print(f'   {WARN}l\'X3 si è riavviato: rifaccio la posizione {i}{X}')
        print()
    for step in (filters_lock_test,) if only == 'blocco' else (settle_test, edit_test, filters_lock_test):
        while True:
            try:
                bad += step(x3)
                break
            except Restart:
                print(f'   {WARN}l\'X3 si è riavviato: rifaccio il passo{X}')
        print()
    print(f'{OK}Tutto come previsto.{X}' if not bad else f'{BAD}Da correggere:{X}\n   ' + '\n   '.join(bad))


def settle_test(x3):
    print(f'{B}Riordino degli stadi{X}')
    to_filters_page(x3)
    keys = FILTER_STEPS[[s[0] for s in FILTER_STEPS].index(x3.verso)][1] if x3.verso in (0, 1, 2, 3) else FILTER_STEPS[0][1]
    key = next(k for k, a in keys.items() if a == 'nd+')
    before, _ = press_filters(x3, key, '+ND')
    if before.get('nd', 0) == x3.flt_cfg['nd'] - 1:
        key = next(k for k, a in keys.items() if a == 'nd-')
        press_filters(x3, key, '-ND')
    x3.flt_events.clear()
    t0 = time.time()
    wait_until(x3, lambda: 'riordino' in x3.flt_events or time.time() - t0 > 6, lambda: f'{DIM}{time.time() - t0:.1f} s…{X}')
    if 'riordino' in x3.flt_events:
        print(f'   {OK}✓ stadi rimessi a posto dopo {time.time() - t0:.1f} s fermi{X} (ND {x3.flt["nd"]}, stadi {x3.flt["stadi"]})')
        return []
    print(f'   {BAD}✗ nessun riordino dopo 6 s{X}')
    return ['riordino: gli stadi non si aggiornano']


# Canvas::Rotation -> posizione fisica, secondo kQuarterTurns in device/src/main.cpp.
VERSI = {0: 'tasti a destra', 1: 'capovolto, tasti in alto', 2: 'tasti a sinistra', 3: 'verticale, tasti in basso'}


def show_log(x3, path):
    log = open(path, 'a', buffering=1) if path else None
    print(f'\n{B}Registro dell\'X3{X} dal vivo (ctrl-C per finire){f": anche in {path}" if path else ""}.\n')

    def on_line(line):
        stamp = time.strftime('%H:%M:%S')
        if log:
            log.write(f'{stamp} {line}\n')
        if not line or line.startswith('[imu]'):
            return
        m = re.match(r'\[camtag\] tasto logico (\d)', line)
        if m:
            line = f'{B}tasto {KEY_NAMES.get(int(m.group(1)), "?")}{X}'
        m = re.match(r'\[camtag\] verso (\d)', line)
        if m:
            line = f'{B}verso {m.group(1)}{X}: {VERSI.get(int(m.group(1)), "?")}'
        print(f'{DIM}{stamp}{X} {line}')

    x3.on_line = on_line
    while True:
        x3.pump()
        time.sleep(0.02)


# --- Cavo collegato a riposo ---------------------------------------------------------
# A riposo la USB è spenta: il firmware sente il computer sul filo dei dati e si
# sveglia per farsi riconoscere. Aprire la seriale riavvia l'X3, quindi qui la porta si
# guarda soltanto; gli appunti sull'ultimo riposo l'X3 li stampa all'avvio dopo ([usb]).
USB_OK_S = 10     # entro quanto deve comparire la porta
USB_REST_S = 22   # dopo aver staccato: 15 s svegli dall'avvio, più il riposo


def port_now(x3):
    found = [x3.fixed_port] if x3.fixed_port else glob.glob('/dev/cu.usbmodem*')
    return found[0] if found and Path(found[0]).exists() else None


def read_usb_note(x3, secs=10):
    """Apre la seriale (l'X3 si riavvia) e legge gli appunti sull'ultimo riposo e la batteria."""
    note, gauge = {}, {}
    prev = x3.on_line

    def on_line(line):
        if prev:
            prev(line)
        m = re.match(r'\[usb\] ultimo riposo: (\d+) s, fine (.+?), cavo (\d+) letture \((\d+) con computer\), '
                     r'computer dopo (-?\d+) ms', line)
        if m:
            note.update(secs=int(m.group(1)), why=m.group(2), probes=int(m.group(3)), host=int(m.group(4)),
                        ms=int(m.group(5)))
        m = re.match(r'\[batt\] ok=1 mV=(\d+) mA=(-?\d+) .*pct=(\d+)', line)
        if m:
            gauge.update(mv=int(m.group(1)), ma=int(m.group(2)), pct=int(m.group(3)))

    x3.on_line = on_line
    try:
        x3.connect()
        t0 = time.time()
        while not gauge and time.time() - t0 < secs:
            x3.pump()
            time.sleep(0.02)
    finally:
        x3.on_line = prev
        x3.close()
    return note, gauge


def test_usb(x3, trials):
    print(f"""
{B}Cavo collegato a riposo{X}: {trials} prove. Lascia l'X3 acceso, appoggiato sul tavolo, e
non toccare i tasti. Stacca e attacca il cavo {B}dal lato del computer{X}, così l'X3
resta fermo (se si muove si sveglia per il verso e la prova non vale).
""")
    results = []
    i = 0
    while i < trials:
        if port_now(x3):
            print(f'{B}{i + 1}/{trials}{X} stacca il cavo.')
            while port_now(x3):
                time.sleep(0.1)
        for left in range(USB_REST_S, 0, -1):
            sys.stdout.write(f'\r\033[K   staccato: aspetto che vada a riposo, {left} s (non toccarlo)')
            sys.stdout.flush()
            time.sleep(1)
        print(f'\r\033[K{B}{i + 1}/{trials} ricollega il cavo adesso.{X}')
        t0 = time.time()
        while not port_now(x3) and time.time() - t0 < 30:
            sys.stdout.write(f'\r\033[K   {time.time() - t0:4.1f} s')
            sys.stdout.flush()
            time.sleep(0.05)
        dt = time.time() - t0 if port_now(x3) else None
        sys.stdout.write('\r\033[K')
        if dt is None:
            print(f'   {BAD}porta non comparsa in 30 s{X}: premi un tasto dell\'X3 per continuare.')
        note, gauge = read_usb_note(x3)
        batt = f'batteria {gauge["pct"]}% {gauge["ma"]:+d} mA' if gauge else 'batteria ?'
        if not note:
            print(f'   {WARN}nessun riposo prima del cavo{X} (l\'X3 era sveglio, o il firmware è vecchio): rifaccio.')
            continue
        woke = (f'riposo di {note["secs"]} s finito per: {note["why"]}; cavo sentito {note["host"]} volte su '
                f'{note["probes"]}; riconosciuto dopo {note["ms"]} ms' if note['ms'] >= 0 else
                f'riposo di {note["secs"]} s finito per: {note["why"]}; cavo sentito {note["host"]} volte su '
                f'{note["probes"]}; computer mai arrivato in quel risveglio')
        # "carica": l'ha svegliato prima il gauge, ma era a riposo lo stesso.
        if dt is not None and note['why'] not in ('usb', 'carica'):
            print(f'   {WARN}non vale{X}: {woke}. Rifaccio.')
            continue
        i += 1
        good = dt is not None and dt <= USB_OK_S
        results.append((dt, note, gauge))
        mark = f'{OK}{dt:.1f} s{X}' if good else f'{BAD}{"mai" if dt is None else f"{dt:.1f} s"}{X}'
        print(f'   {mark}  {DIM}{woke}; {batt}{X}')

    good = [r for r in results if r[0] is not None and r[0] <= USB_OK_S]
    times = sorted(r[0] for r in results if r[0] is not None)
    host = sorted(r[1]['ms'] for r in results if r[1]['ms'] >= 0)
    print(f'\n{B}Risultato{X}: {OK if len(good) >= 0.9 * trials else BAD}{len(good)} su {trials}{X} entro {USB_OK_S} s '
          f'(il tempo comprende la mano che attacca il cavo).')
    if times:
        print(f'   porta: da {times[0]:.1f} a {times[-1]:.1f} s, mediana {statistics.median(times):.1f} s')
    if host:
        print(f'   dal risveglio al computer: da {host[0]} a {host[-1]} ms, mediana {statistics.median(host):.0f} ms')
    full = [r for r in results if r[2] and r[2]['ma'] <= 5]
    if full:
        print(f'   {len(full)} prove senza corrente di carica (batteria piena): vale anche per quel caso.')
    false_host = [r for r in results if r[1]['host'] > 1]
    if false_host:
        print(f'   {WARN}in {len(false_host)} prove il cavo è stato sentito più di una volta{X} prima del risveglio: '
              f'da guardare (letture sbagliate a cavo staccato, o un computer che non rispondeva).')


# --- Reattività dei tasti -----------------------------------------------------------
# Il firmware di debug ha un compito che legge la scala dei tasti ogni 5 ms anche mentre
# lo schermo si aggiorna ([adc] giu/su), e segna ogni aggiornamento ([epd] ... da t a t).
# Una pressione vista dall'hardware ma non dal loop ("tasto logico") è persa: se cadeva
# dentro un aggiornamento è colpa nostra, se no va guardato l'hardware.
def test_reactivity(x3):
    print(f"""
{B}Reattività dei tasti{X}: usa l'X3 come al solito. Sulla schermata filtri cambia ND e
diottria, anche in fretta e subito dopo un cambio; sulle altre card X e Y.
Ogni pressione che l'hardware sente compare qui: {OK}✓{X} letta dal firmware, {BAD}✗{X} persa.
Se premi un tasto e qui {B}non compare nulla{X}, non l'ha sentita nemmeno l'hardware.
ctrl-C per finire e vedere il riepilogo.
""")
    busy = collections.deque(maxlen=60)     # (tipo, da, a) in ms dell'X3
    logical = collections.deque(maxlen=60)  # (tasto, t)
    down = {}                               # tasto -> (t, raw)
    pending = []                            # (tasto, da, a, raw, ora del Mac)
    stats = collections.Counter()
    refreshing = []                         # ora del Mac dell'aggiornamento in corso
    prev = x3.on_line

    def on_line(line):
        if prev:
            prev(line)
        if line.startswith('[camtag] device:'):
            busy.clear(); logical.clear(); down.clear(); pending.clear(); refreshing.clear()
            print(f'   {DIM}l\'X3 si è riavviato{X}')
            return
        m = re.match(r'\[adc\] giu (\d) raw=(\d+) t=(\d+)', line)
        if m:
            down[int(m.group(1))] = (int(m.group(3)), int(m.group(2)))
            return
        m = re.match(r'\[adc\] su (\d) t=(\d+)', line)
        if m and int(m.group(1)) in down:
            b = int(m.group(1))
            t0, raw = down.pop(b)
            pending.append((b, t0, int(m.group(2)), raw, time.time()))
            return
        m = re.match(r'\[camtag\] tasto logico (\d) t=(\d+)(?: ritardo=(\d+))?', line)
        if m:
            logical.append((int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)))
            return
        if line.startswith('[epd] inizio'):
            refreshing[:] = [time.time()]
            return
        m = re.match(r'\[epd\] (\w+) da (\d+) a (\d+)', line)
        if m:
            refreshing.clear()
            busy.append((m.group(1), int(m.group(2)), int(m.group(3))))

    def judge(b, t0, t1, raw):
        name, ms = KEY_NAMES.get(b, '?'), t1 - t0
        got = [late for k, t, late in logical if k == b and t0 - 30 <= t <= t1 + 30]
        if got:
            stats['lette'] += 1
            late = f', eseguita dopo {got[0]} ms (aspettava lo schermo)' if got[0] > 100 else ''
            print(f'   {OK}✓ {name}{X}  {DIM}{ms} ms, valore {raw}{late}{X}')
            return
        hit = [(kind, a, z) for kind, a, z in busy if a <= t1 and t0 <= z]
        if hit:
            kind, a, z = hit[0]
            stats[f'perse durante un aggiornamento {kind}'] += 1
            print(f'   {BAD}✗ {name} persa{X}: premuta e lasciata mentre lo schermo faceva un aggiornamento '
                  f'{kind} ({z - a} ms){DIM}, pressione di {ms} ms{X}')
        else:
            stats['perse a schermo fermo'] += 1
            print(f'   {BAD}✗ {name} persa a schermo fermo{X}{DIM}: pressione di {ms} ms, valore {raw}{X}')

    x3.on_line = on_line
    try:
        while True:
            x3.pump()
            # Il loop scrive il "tasto logico" entro pochi ms, o appena finito
            # l'aggiornamento: mezzo secondo dopo il rilascio, a schermo fermo, si giudica.
            if refreshing and time.time() - refreshing[0] < 10:
                time.sleep(0.02)
                continue
            for p in [p for p in pending if time.time() - p[4] > 0.5]:
                pending.remove(p)
                judge(*p[:4])
            time.sleep(0.02)
    except KeyboardInterrupt:
        pass
    finally:
        x3.on_line = prev
    total = sum(stats.values())
    print(f'\n{B}Riepilogo{X}: {total} pressioni sentite dall\'hardware')
    for k, n in stats.most_common():
        print(f'   {n:3d}  {k}')


# --- Batteria -----------------------------------------------------------------------
# Il firmware scrive sulla microSD una riga ogni 5 minuti, una all'avvio (A) e una allo
# spegnimento (S), con i contatori di tempo sveglio e a riposo. Qui si scarica il file
# (comando "batt?" sulla seriale) e si ricava la corrente media di ogni stato dal calo
# dei mAh contati dal fuel gauge. Formato: docs/batteria.md.
BATT_COLS = ('t', 'ev', 'info', 'mv', 'ma', 'mah', 'full', 'pct', 'awake', 'rest', 'screen', 'keys', 'touch',
             'offma', 'touchma')
# Colonne arrivate dopo: tocchi dalla 0.5.3; dalla 0.5.5 la corrente che il gauge aveva in
# memoria all'accensione dopo uno spento e al primo tocco breve (vuote se non c'è lettura).
BATT_LENS = (12, 13, len(BATT_COLS))
BATT_BLANK = ('offma', 'touchma')
# Diagnosi da spento della 0.5.6 (righe D dopo la riga A dell'accensione): cosa è stato
# fatto prima di ogni tratto da un minuto. L'ultimo tratto, con tutto rimesso a dormire,
# è la lettura normale dell'accensione. Dalla 0.5.7 le righe D dicono solo cosa si è
# fatto alla microSD (info da 100000 in su), dalla 0.5.8 a MISO (da 200000 in su), dalla 0.5.9 ai pin liberi (da 300000), dalla 0.5.10 se c'era la microSD (da 400000), dalla 0.5.11 il CS della flash (da 500000, diag_sd_notes).
DIAG_STEPS = ("com'era", 'dopo un risveglio a vuoto', 'tutto riavviato e rimesso a dormire', 'IMU lasciato sveglio',
              'pannello fuori dal sonno profondo', 'microSD lasciata accesa')
RESETS = {1: 'alimentazione tornata', 3: 'riavvio del firmware', 4: 'errore del firmware', 8: 'sonno profondo',
          9: 'tensione troppo bassa', 11: 'USB'}
WAKES = {0: '', 4: 'timer', 7: 'power'}
OFF_WHY = {0: 'power', 1: 'dimenticato acceso', 2: 'batteria scarica'}
CELL_MAH = 650


def parse_battery(text):
    rows = []
    for line in text.splitlines():
        f = line.strip().split(',')
        if len(f) not in BATT_LENS or not f[0].isdigit():
            continue
        row = dict(zip(BATT_COLS, f + ['0'] * (13 - len(f)) + [''] * 2))
        try:
            row = {k: v if k == 'ev' else (int(v) if v else None) if k in BATT_BLANK else int(v) for k, v in row.items()}
        except ValueError:
            continue
        if row['ev'] == 'D':  # non è un tratto di consumo: va con l'accensione che la precede
            if rows:
                rows[-1].setdefault('diag', []).append(row)
            continue
        rows.append(row)
    return rows


def diag_sd_notes(diag):
    """Righe D della 0.5.10 (da 400000) e della 0.5.11 (da 500000): base + tratto × 1000
    + 100 se salvata in NVS da un'accensione senza microSD + quello che dice il tratto:
    0.5.10 la microSD (0 dentro, 1 fuori, 2 non si sa), 0.5.11 il CS della flash
    (1 con il pull-up acceso prima di dormire, + 10 se ce l'aveva già)."""
    notes, groups = [], {}

    def close():
        if groups:
            notes.append('   ' + ' · '.join(f'{k}: {B}{", ".join(v)} mA{X}{DIM}' for k, v in groups.items()))
            groups.clear()

    for d in diag:
        flash = d['info'] >= 500000
        step, rest = divmod(d['info'] - (500000 if flash else 400000), 1000)
        saved, sd = divmod(rest, 100)
        if step == 0:  # comincia uno spento
            close()
            notes.append('spento senza microSD (letture salvate, scritte a questa accensione):' if saved
                         else 'spento con diagnosi:')
        if flash:
            what = 'CS con pull-up' if sd % 10 else "CS com'era"
            if sd >= 10:
                what += ' (pull-up già acceso)'
        else:
            what = {0: 'microSD dentro', 1: 'microSD fuori'}.get(sd, 'microSD ?')
        ma = '?' if d['offma'] is None else f'{-d["offma"]} mA'
        notes.append(f'   diagnosi {step + 1}, {what}: {B}{ma}{X}{DIM}')
        groups.setdefault(what, []).append(ma.replace(' mA', ''))
    close()
    return notes


def fetch_battery(x3, erase, blank=None):
    """Chiede il registro all'X3. Restituisce (testo del CSV, ultima lettura del gauge)."""
    lines, state, gauge = [], {'on': False, 'done': False, 'booted': False}, {}
    prev = x3.on_line

    def on_line(line):
        if prev:
            prev(line)
        m = re.match(r'\[batt\] ok=(\d) mV=(\d+) mA=(-?\d+) mAh=(\d+) pieno=(\d+) progetto=(\d+) pct=(\d+)', line)
        if m:
            gauge.update(zip(('ok', 'mv', 'ma', 'mah', 'full', 'design', 'pct'), map(int, m.groups())))
            state['booted'] = True
        elif line == '[b] inizio':
            state['on'] = True
        elif line == '[b] fine':
            state['done'] = True
        elif state['on'] and line.startswith('[b] '):
            lines.append(line[4:])

    x3.on_line = on_line
    try:
        # Aprire la seriale riavvia l'X3: il comando va mandato quando è ripartito.
        t0 = time.time()
        while not state['booted'] and time.time() - t0 < 20:
            x3.pump()
            time.sleep(0.02)
        # Nei primi secondi il firmware controlla la capacità scritta nel gauge.
        t0 = time.time()
        while time.time() - t0 < 9:
            x3.pump()
            time.sleep(0.02)
        for attempt in range(3):
            x3.ser.write(b'batt?\n')
            t0 = time.time()
            while not state['done'] and time.time() - t0 < 30:
                x3.pump()
                time.sleep(0.02)
            if state['done']:
                break
        if not state['done']:
            sys.exit(f'{BAD}L\'X3 non ha mandato il registro: firmware senza il registro dei consumi?{X}')
        for cmd in ([b'batt!\n'] if erase else []) + ([b'blank=%d\n' % blank] if blank is not None else []):
            x3.ser.write(cmd)
            for _ in range(50):
                x3.pump()
                time.sleep(0.02)
    finally:
        x3.on_line = prev
    return '\n'.join(lines) + '\n', gauge


def fmt_hours(h):
    return f'{h * 60:.0f} min' if h < 1.5 else f'{h:.1f} h' if h < 72 else f'{h / 24:.1f} giorni'


def battery_report(rows, gauge=None):
    if gauge:
        if not gauge.get('ok'):
            print(f'{WARN}Il fuel gauge non risponde: niente misura.{X}')
        else:
            print(f'{B}Adesso{X}: {gauge["mv"]} mV, {gauge["ma"]:+d} mA, {gauge["mah"]} di {gauge["full"]} mAh ({gauge["pct"]}%)')
            if gauge['design'] > CELL_MAH * 1.25:
                print(f'   {WARN}Nel gauge la capacità di progetto è {gauge["design"]} mAh (la batteria è da {CELL_MAH}): percentuale '
                      f'e mAh non sono affidabili. Il firmware la corregge da solo nei primi secondi: riprova tra poco.{X}')
            elif gauge['full'] > CELL_MAH * 1.25:
                print(f'   {WARN}Il gauge crede che la batteria piena valga {gauge["full"]} mAh: i mAh vanno presi con cautela '
                      f'finché non ha fatto un ciclo completo di scarica e carica.{X}')
    if len(rows) < 2:
        print(f'\n{WARN}Il registro ha {len(rows)} righe: serve più tempo a batteria (una riga ogni 5 minuti).{X}')
        return
    # Tratti tra due righe di fila: tempo, mAh scesi, e in che stato è passato quel tempo.
    kinds = ('sveglio', 'riposo', 'misto', 'spento')
    tot = {k: {'s': 0, 'mah': 0, 'mv': 0, 'screen': 0, 'n': 0, 'ma': []} for k in kinds}
    notes, charged = [], 0
    # Dalla 0.6.0 lo spento è un sonno leggero che scrive una riga P ogni 5 minuti con
    # info 1: lo spento va dalla riga S alla A, passando per quelle.
    off_from = None
    for a, b in zip(rows, rows[1:]):
        dt = b['t'] - a['t']
        if a['ev'] == 'S':
            off_from = a
        off_p = a['ev'] == 'P' and a['info'] == 1
        if b['ev'] == 'P' and b['info'] == 1:
            if dt <= 0 or off_from is None:
                continue
            kind = 'spento'
        elif b['ev'] == 'A':
            reset, wake = divmod(b['info'], 100)
            # Dalla 0.5.10 le righe D (da 400000) dicono se durante il tratto c'era la microSD;
            # quelle di uno spento senza microSD arrivano a un'accensione dopo, quindi qui
            # non vanno legate allo spento appena prima: si scrivono per conto loro.
            new = [d for d in b.get('diag', []) if d['info'] >= 400000]
            b['diag'] = [d for d in b.get('diag', []) if d['info'] < 400000]
            if a['ev'] != 'S' and not (off_p and off_from):
                notes.append(f'riavvio da acceso ({RESETS.get(reset, reset)})')
                notes.extend(diag_sd_notes(new))
                continue
            how = f'{RESETS.get(reset, reset)}{" · " + WAKES.get(wake, str(wake)) if WAKES.get(wake, str(wake)) else ""}'
            if reset == 3 and wake == 7:
                how = 'power (spento leggero)'
            s0 = a if a['ev'] == 'S' else off_from
            span = b['t'] - s0['t']
            if dt <= 0:
                notes.append(f'spento ({OFF_WHY.get(a["info"], "?")}) e riacceso ({how}): l\'orologio è ripartito, '
                             'quindi da spento il chip è rimasto senza alimentazione')
                continue
            # Corrente di quel solo spento: se varia da uno all'altro si vede qui, non nella media.
            one = ''
            if s0['ma'] <= 0 and b['ma'] <= 0 and b['mah'] <= s0['mah'] and span >= 1800:
                one = f' · {s0["mah"] - b["mah"]} mAh, {(s0["mah"] - b["mah"]) / (span / 3600):.1f} mA'
            touch = f' · {WARN}{b["touch"]} tocchi brevi del power{X}{DIM}' if b['touch'] else ''
            # Dalla 0.5.5: la corrente che il gauge aveva in memoria appena sveglio, cioè il
            # consumo da spento al mA, senza aspettare che scendano i mAh.
            now = ''
            if b['offma'] is not None:
                now = f' · {B}al risveglio {-b["offma"]} mA{X}{DIM}'
                if b['touchma'] is not None:
                    now += f' (al primo tocco {-b["touchma"]} mA)'
                if span < 60:
                    now += ' (meno di un minuto: il gauge può non aver fatto in tempo a leggere)'
            notes.append(f'spento {fmt_hours(span / 3600)} ({OFF_WHY.get(s0["info"], "?")}), riacceso: {how}{one}{touch}{now}')
            notes.extend(diag_sd_notes(new))
            diag, groups = b.get('diag', []), {}
            for d in diag:
                ma = '?' if d['offma'] is None else f'{-d["offma"]} mA'
                if d['info'] >= 300000:
                    # Dalla 0.5.9: prima di ogni tratto i pin liberi sono lasciati com'erano o fermati tutti.
                    step, held = divmod(d['info'] - 300000, 1000)
                    what = 'pin fermi' if held == 1 else "pin com'erano"
                    groups.setdefault(what, []).append(ma.replace(' mA', ''))
                    notes.append(f'   diagnosi {step + 1}, {what}: {B}{ma}{X}{DIM}')
                    continue
                if d['info'] >= 200000:
                    # Dalla 0.5.8: prima di ogni tratto MISO della microSD è lasciato in aria o tenuto alto.
                    step, up = divmod(d['info'] - 200000, 1000)
                    what = 'MISO alto' if up == 1 else 'MISO in aria'
                    groups.setdefault(what, []).append(ma.replace(' mA', ''))
                    notes.append(f'   diagnosi {step + 1}, {what}: {B}{ma}{X}{DIM}')
                    continue
                if d['info'] >= 100000:
                    # Dalla 0.5.7: prima di ogni tratto la microSD è lasciata com'è o mandata a riposo (CMD0).
                    step, sd = divmod(d['info'] - 100000, 1000)
                    what = ("microSD com'è" if sd == 254 else 'microSD a riposo' if sd == 1 else
                            'microSD a riposo, ma non ha risposto' if sd == 255 else f'microSD a riposo, risposta 0x{sd:02X}')
                    groups.setdefault("microSD com'è" if sd == 254 else 'microSD a riposo', []).append(ma.replace(' mA', ''))
                    notes.append(f'   diagnosi {step + 1}, {what}: {B}{ma}{X}{DIM}')
                    continue
                # 0.5.6: una periferica lasciata sveglia per tratto, e lo stato dell'IMU al risveglio
                # (0x80 spento del tutto, bit 0 accelerometro, bit 1 giroscopio).
                step, imu = divmod(d['info'], 1000)
                what = DIAG_STEPS[step] if step < len(DIAG_STEPS) else f'tratto {step}'
                state = ('non risponde' if imu == 255 else 'spento' if imu & 0x80 else
                         ' + '.join(n for bit, n in ((1, 'accelerometro'), (2, 'giroscopio')) if imu & bit) or 'fermo ma non spento')
                notes.append(f'   diagnosi {step + 1}, {what}: {B}{ma}{X}{DIM} · IMU {state}')
            if diag and diag[0]['info'] < 100000 and b['offma'] is not None:
                notes.append(f'   diagnosi {len(diag) + 1}, alla fine con tutto a dormire: {B}{-b["offma"]} mA{X}{DIM}')
            if groups:
                notes.append('   ' + ' · '.join(f'{k}: {B}{", ".join(v)} mA{X}{DIM}' for k, v in groups.items()))
            kind = 'spento'
        else:
            awake, rest = b['awake'] - a['awake'], b['rest'] - a['rest']
            if dt <= 0 or awake < 0 or rest < 0:
                continue
            share = rest / max(1, awake + rest)
            kind = 'riposo' if share >= 0.95 else 'sveglio' if share <= 0.05 else 'misto'
        if a['ma'] > 0 or b['ma'] > 0 or b['mah'] > a['mah']:
            charged += dt
            continue
        t = tot[kind]
        t['s'] += dt; t['mah'] += a['mah'] - b['mah']; t['mv'] += a['mv'] - b['mv']; t['n'] += 1
        t['screen'] += max(0, b['screen'] - a['screen']) if b['ev'] != 'A' else 0
        t['ma'].append(-b['ma'])
    full = next((r['full'] for r in reversed(rows) if 0 < r['full'] <= CELL_MAH * 1.25), CELL_MAH)
    print(f'\n{B}Consumi a batteria{X} ({len(rows)} righe; {fmt_hours(charged / 3600)} in carica non contate)')
    print(f'   {DIM}stato      tempo        mAh    media mA   istantanea mA   aggiornamenti   durata con {full} mAh{X}')
    for k in kinds:
        t = tot[k]
        if t['s'] < 120:  # pochi secondi non dicono nulla
            continue
        h = t['s'] / 3600
        inst = f'{statistics.median(t["ma"]):.0f}' if k != 'spento' and t['ma'] else '—'
        if t['mah'] > 0:
            ma = t['mah'] / h
            mean, life = f'{ma:8.2f}', fmt_hours(full / ma)
        else:
            # Meno di 1 mAh in tutto il tratto: si sa solo che la media sta sotto 1 mAh / ore.
            mean, life = f'  < {1 / h:.2f}', f'più di {fmt_hours(full * h)}'
        print(f'   {k:9s} {fmt_hours(h):>9s} {t["mah"]:8d} {mean}   {inst:>12s} {t["screen"]:14d}   {life}')
    low = [k for k in ('riposo', 'spento') if tot[k]['s'] >= 120 and tot[k]['mah'] <= 1]
    if low:
        print(f'\n   {WARN}{" e ".join(low)}: il gauge ha contato 1 mAh o meno. Conta a passi di 1 mAh e sotto 1 mA può '
              f'sottostimare: per un numero buono servono più ore.{X}')
        for k in low:
            t = tot[k]
            print(f'   {DIM}{k}: tensione scesa di {t["mv"]} mV in {fmt_hours(t["s"] / 3600)}{X}')
    for n in notes:
        print(f'   {DIM}· {n}{X}')


def test_battery(x3, erase, read_path, blank=None):
    if read_path:
        battery_report(parse_battery(Path(read_path).read_text()))
        return
    print(f'\n{B}Registro dei consumi{X}: lo chiedo all\'X3…')
    text, gauge = fetch_battery(x3, erase, blank)
    out = REPO / 'misure'
    out.mkdir(exist_ok=True)
    path = out / f'batteria-{time.strftime("%Y%m%d-%H%M")}.csv'
    path.write_text(text)
    print(f'{DIM}salvato in {path.relative_to(REPO)}{", cancellato sull\'X3" if erase else ""}{X}\n')
    battery_report(parse_battery(text), gauge)
    if blank is not None:
        print(f'\n{B}Bianco da spento{X}: ' + (f'dopo {blank} s. Stacca la USB, spegni e aspetta; poi rimetti i tre giorni con --bianco 0.'
                                               if blank else 'di nuovo dopo tre giorni.'))
    print(f"""
{B}Per misurare{X}: stacca la USB e lascia l'X3 a batteria nello stato che ti interessa
(acceso e fermo = riposo; spento con il power tenuto), almeno qualche ora per riposo
e spento. Poi ricollega la USB e rilancia questo comando.""")


def main():
    ap = argparse.ArgumentParser(description='Prova guidata dell\'X3 con riscontro dal vivo.')
    ap.add_argument('prova', choices=['tasti', 'versi', 'card', 'filtri', 'blocco', 'reattivita', 'log', 'batteria',
                                      'usb'])
    ap.add_argument('--carica', action='store_true', help='compila e carica il firmware prima di cominciare')
    ap.add_argument('--out', help='salva il risultato della prova dei versi in questo file JSON')
    ap.add_argument('--da', type=int, default=1, help='prove card e filtri: riparti da questa posizione (1-4)')
    ap.add_argument('--file', help='salva anche il registro della seriale in questo file')
    ap.add_argument('--porta', help='porta seriale (di solito si trova da sola)')
    ap.add_argument('--azzera', action='store_true', help='batteria: cancella il registro sull\'X3 dopo averlo salvato')
    ap.add_argument('--leggi', help='batteria: rifà i conti su un registro già salvato (anche via Bluetooth), senza X3')
    ap.add_argument('--bianco', type=int, help='batteria: secondi da spento prima del bianco, per provarlo (0 = tre giorni)')
    ap.add_argument('--prove', type=int, default=10, help='usb: quante volte staccare e riattaccare il cavo')
    args = ap.parse_args()
    if args.prova == 'batteria' and args.leggi:
        return test_battery(None, False, args.leggi)

    x3 = X3(args.porta)
    if args.file and args.prova != 'log':
        log = open(args.file, 'a', buffering=1)
        x3.on_line = lambda line: log.write(f'{time.strftime("%H:%M:%S")} {line}\n')
    try:
        if args.carica:
            upload(x3)
        if args.prova == 'usb':
            return test_usb(x3, args.prove)  # la seriale si apre solo dopo ogni prova
        x3.connect()
        if args.prova == 'tasti':
            test_keys(x3)
        elif args.prova == 'card':
            test_cards(x3, args.da)
        elif args.prova == 'filtri':
            test_filters(x3, args.da)
        elif args.prova == 'reattivita':
            test_reactivity(x3)
        elif args.prova == 'blocco':
            test_filters(x3, only='blocco')
        elif args.prova == 'log':
            show_log(x3, args.file)
        elif args.prova == 'batteria':
            test_battery(x3, args.azzera, None, args.bianco)
        else:
            test_positions(x3, args.out)
    except KeyboardInterrupt:
        print(f'\n{WARN}interrotto{X}')
    finally:
        x3.close()


if __name__ == '__main__':
    main()
