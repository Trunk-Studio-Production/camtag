// Lettura della flash con lo stub di esptool (comando READ_FLASH, 0xd2), al posto
// di ESPLoader.readFlash di esptool-js 0.7.1, che su 16 MB inchioda il browser:
// decodifica lo SLIP aggiungendo un byte alla volta a un array che ricopia, e
// lascia nel buffer il pacchetto finale con l'MD5, che confonde il comando dopo.
// Qui i pacchetti si decodificano in un buffer fisso, ogni pacchetto riceve la
// sua conferma (byte ricevuti finora) e l'MD5 finale dello stub si controlla.
//
// loader e transport sono quelli di esptool-js (ESPLoader, Transport): si usano
// checkCommand, ESP_READ_FLASH, transport.buffer (riempito da readLoop) e
// transport.write (che fa lo SLIP).
// Pacchetti piccoli, uno alla volta. Chrome apre la porta con PARMRK
// (serial_io_handler_posix.cc): il sistema gli consegna ogni byte 0xFF due volte
// (lui poi ne toglie uno) e li fa passare dalla coda lenta del terminale, che sul
// Mac tiene 1024 byte e butta senza errori quelli in più. Un pacchetto da 512
// byte di flash cancellata (tutta 0xFF) in coda ne occupa 1026: ogni tanto la
// fine si perde e la lettura si ferma. Con 384 byte il caso peggiore è 770, e
// con un pacchetto alla volta la coda è vuota quando parte il successivo.
// Le zone cancellate la pagina non le legge nemmeno (vedi index.html).
// Fuori dal browser (esptool, Node) PARMRK non c'è e vanno bene 4 KB, 64 in volo.
export const opts = { block: 384, inflight: 1 };
// Senza dati per questo tempo (conferma rimandata compresa) si dà l'X3 per inceppato.
const SILENCE_MS = 2500;
// Se l'X3 tace, si rimanda l'ultima conferma: se era andata persa per strada lo
// stub riparte da solo, senza riavvii (una conferma doppia non fa danni).
const RESEND_MS = 400;
export const stats = { ackResent: 0 };

// Attesa dei dati senza timer: con la finestra coperta Chrome rallenta i timer
// fino a un secondo e più, mentre l'arrivo dei dati dalla porta no. Si aggancia
// appendArray del trasporto, che readLoop chiama a ogni blocco ricevuto.
function dataWaiter(transport) {
  if (!transport._camtagWake) {
    const append = transport.appendArray.bind(transport);
    transport._camtagWake = [];
    transport.appendArray = (a, b) => {
      const r = append(a, b);
      for (const f of transport._camtagWake.splice(0)) f();
      return r;
    };
  }
  return ms => new Promise(r => {
    const t = setTimeout(r, ms);
    transport._camtagWake.push(() => { clearTimeout(t); r(); });
  });
}
const u32 = v => new Uint8Array(new Uint32Array([v]).buffer);  // little endian
const hex = b => [...b].map(x => x.toString(16).padStart(2, '0')).join('');

// Legge size byte da addr in out (da 0). Se si inceppa, l'errore ha .got: i byte
// arrivati interi e già in out, da cui riprendere dopo aver riavviato lo stub.
export async function readFlash(loader, transport, addr, size, md5, onProgress, out = new Uint8Array(size)) {
  let got = 0;
  try { return await read(); }
  catch (e) { e.got = got; throw e; }

  async function read() {
  const pkt = new Uint8Array(16), dv = new DataView(pkt.buffer);
  dv.setUint32(0, addr, true); dv.setUint32(4, size, true);
  dv.setUint32(8, opts.block, true); dv.setUint32(12, opts.inflight, true);
  await loader.checkCommand('read flash', loader.ESP_READ_FLASH, pkt);

  const wait = dataWaiter(transport);
  const frame = new Uint8Array(opts.block + 64);
  let len = -1, esc = false, digest = null, last = Date.now(), resent = Date.now();
  while (!digest) {
    const buf = transport.buffer;
    if (!buf.length) {
      const now = Date.now();
      if (now - last > SILENCE_MS) throw new Error(`no data from the X3 after ${got} bytes`);
      if (got && len < 0 && now - last > RESEND_MS && now - resent > RESEND_MS) {
        resent = now; stats.ackResent++;
        await transport.write(u32(got));
      }
      await wait(RESEND_MS);
      continue;
    }
    transport.buffer = new Uint8Array(0);
    last = Date.now();
    for (let i = 0; i < buf.length; i++) {
      const b = buf[i];
      if (len < 0) {                       // fuori da un pacchetto: deve iniziarne uno
        if (b !== 0xc0) throw new Error(`unexpected byte 0x${b.toString(16)} after ${got} bytes`);
        len = 0;
      } else if (esc) {
        esc = false;
        if (b === 0xdc) frame[len++] = 0xc0;
        else if (b === 0xdd) frame[len++] = 0xdb;
        else throw new Error(`bad SLIP escape 0x${b.toString(16)}`);
      } else if (b === 0xdb) {
        esc = true;
      } else if (b === 0xc0) {
        if (len === 0) continue;           // due 0xc0 di fila: inizio vero
        if (got < size) {
          // Un pacchetto corto prima della fine = byte persi per strada (buffer della
          // porta pieno): meglio fermarsi subito che confermare un conteggio sbagliato.
          if (len !== Math.min(opts.block, size - got)) throw new Error(`lost data: packet of ${len} bytes after ${got}`);
          out.set(frame.subarray(0, len), got);
          got += len;
          len = -1;
          await transport.write(u32(got));
          onProgress?.(got, size);
        } else {                           // dopo i dati: l'MD5 dello stub
          digest = frame.slice(0, len);
          len = -1;
          const rest = buf.subarray(i + 1);
          if (rest.length) {               // byte già arrivati dopo: restano per il comando seguente
            const merged = new Uint8Array(rest.length + transport.buffer.length);
            merged.set(rest); merged.set(transport.buffer, rest.length);
            transport.buffer = merged;
          }
          break;
        }
      } else {
        if (len >= frame.length) throw new Error('packet too long');
        frame[len++] = b;
      }
    }
  }
  if (digest.length !== 16) throw new Error(`unexpected final packet (${digest.length} bytes)`);
  const mine = md5(out);
  if (mine !== hex(digest)) throw new Error(`read MD5 ${mine} differs from the X3's ${hex(digest)}`);
  return out;
}
}
