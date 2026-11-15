// CUMBIA en el navegador: el motor del FM-1 (cumbia.wasm) dentro del hilo de audio.
// El hilo principal manda las ranuras de sonido y los botones; aquí se manda la pantalla de vuelta (~30 por segundo).
class CumbiaProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.e = null;
    this.frames = 0;
    this.port.onmessage = (m) => {
      try { this.msg(m.data); } catch (err) { this.port.postMessage({ t: 'error', m: String(err && err.message || err) }); }
    };
  }
  msg(d) {
    if (d.t === 'init') {
      const wasi = { fd_close: () => 0, fd_fdstat_get: () => 0, fd_seek: () => 0, fd_write: () => 0 };
      const mod = new WebAssembly.Module(d.bytes);
      const e = new WebAssembly.Instance(mod, { wasi_snapshot_preview1: wasi }).exports;
      const base = e.p_slots(), sz = e.p_slot_size();
      d.slots.forEach((s, i) => {
        const m = new Uint8Array(e.memory.buffer);
        m.set(s.hdr, base + i * sz);
        m.set(s.data, base + i * sz + 512);
      });
      e.p_init();
      this.e = e;
      this.port.postMessage({ t: 'ready' });
    } else if (this.e) {
      if (d.t === 'btn') this.e.p_button(d.l, d.d ? 1 : 0);
      else if (d.t === 'knob') this.e.p_knob(d.e, d.d);
      else if (d.t === 'key') this.e.p_key(d.k, d.d ? 1 : 0);
    }
  }
  process(inputs, outputs) {
    const out = outputs[0];
    if (!this.e || !out || !out[0]) return true;
    const n = out[0].length;                    // 128
    this.e.p_render(n);
    const f = new Float32Array(this.e.memory.buffer, this.e.p_outbuf(), 2 * n);
    const L = out[0], R = out[1] || out[0];
    for (let i = 0; i < n; i++) { L[i] = f[2 * i]; R[i] = f[2 * i + 1]; }
    this.frames += n;
    if (this.frames >= 1470) {                  // 30 cuadros por segundo
      this.frames = 0;
      const s = new Uint16Array(this.e.memory.buffer, this.e.p_screen(), 240 * 240).slice();
      this.port.postMessage({ t: 'screen', s }, [s.buffer]);
    }
    return true;
  }
}
registerProcessor('cumbia', CumbiaProcessor);
