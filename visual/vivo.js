
// ---------------- Parte viva: mueve el banco con los bloques reales de la simulación
const C = /*__CONST__*/null;
const EJEMPLO = /*__EJEMPLO__*/null;
const DATOS = /*__DATOS__*/null || EJEMPLO;
const P = C.P;
const stage = document.querySelector(".iso-stage");
const svg = stage.querySelector("svg");
const q = (s) => svg.querySelector(s);
const NS = "http://www.w3.org/2000/svg";
const MOD = { 2: "QPSK", 4: "16QAM", 6: "64QAM", 8: "256QAM", 10: "1024QAM" };
const fmt = (x) => Math.round(x).toLocaleString("es");
const parte = (n) => q(`[data-part="${n}"]`);
const cartucho = parte("cartucho"), paridad = parte("paridad"), franja = parte("franja"), etiqueta = parte("etiqueta");
const sellos = { aes: parte("sello-aes"), crc: parte("sello-crc"), des: parte("sello-des") };
const agujas = { qam: parte("aguja-qam"), iter: parte("aguja-iter"), snr: parte("aguja-snr") };
const pulso = parte("pulso");
const lampara = { ack: q('[data-lampara="ack"]'), nack: q('[data-lampara="nack"]') };
const carriles = [...Array(8)].map((_, l) => q(`[data-carril="${l}"]`));
const cabezas = (lado) => [...Array(8)].map((_, l) => q(`[data-cabeza="${lado}${l}"]`));
const cab = { tx: cabezas("tx"), rx: cabezas("rx") };
const cablesDe = (lado) => [...Array(8)].map((_, l) => q(`[data-cable="${lado}${l}"]`));
const cab2 = { tx: cablesDe("tx"), rx: cablesDe("rx") };
const tejas = { tx: [...svg.querySelectorAll('[data-teja="tx"]')], rx: [...svg.querySelectorAll('[data-teja="rx"]')] };
const readout = document.querySelector("[data-readout]");
const pie = document.querySelector("[data-pie]");
const quieto = matchMedia("(prefers-reduced-motion: reduce)");
const ST = C.ST;

// Cuentas (símbolos) del aire: 7 por capa
const CUENTAS = 7;
const cuentas = carriles.map((g) => [...Array(CUENTAS)].map(() => {
  const c = document.createElementNS(NS, "circle");
  c.setAttribute("r", "1.6");
  c.style.opacity = "0";
  g.appendChild(c);
  return c;
}));
const azar = (a) => { const s = Math.sin(a * 12.9898 + 78.233) * 43758.5453; return s - Math.floor(s); };

// Agujas de las esferas: fracción 0..1 del arco de 270°
const aguja = (el, [x, y, z], r, f) => {
  const a = -Math.PI * 0.75 + Math.max(0, Math.min(1, f)) * Math.PI * 1.5;
  el.setAttribute("d", lineOnTop([x - Math.sin(a) * 1.6, y + Math.cos(a) * 1.6], [x + Math.sin(a) * (r - 1.4), y - Math.cos(a) * (r - 1.4)], z + 2.3, P));
};
const DIAL = { qam: [[ST.modular, C.CART_Y, 30], 6.2], iter: [[ST.decodificar, C.CART_Y, 30], 6.2], snr: [[0, 40, 9], 6.6] };

let camino = 0, tb = 0, fases = [], reloj = 0, pausa = quieto.matches, visible = false, cuadro = 0, ultimo = 0;

function datosCamino() { return DATOS.caminos[camino]; }
// El cartucho toma el color de su código, el mismo de las gráficas de la aplicación
const COLORES = { ldpc: ["#2a78d6", "#6aa3e8", "#1d5aa6"], bloque: ["#eb6834", "#f29a74", "#b94a1f"], sin: ["#8a8a8a", "#b0b0b0", "#666666"] };
function colorear() {
  const [cara, arriba, sombra] = COLORES[datosCamino().clave] || COLORES.sin;
  const g = parte("cuerpo");
  g.style.setProperty("--anatomy-face", cara);
  g.style.setProperty("--anatomy-lit-top", arriba);
  g.style.setProperty("--anatomy-lit-shade", sombra);
}

// Fases de un bloque: todos sus envíos (los reenvíos salen otra vez desde el código, estación 4)
function armarFases() {
  const c = datosCamino(), t = c.traza[tb];
  fases = [];
  if (!t) return;
  const f = (tipo, dur, extra = {}) => fases.push({ tipo, dur, ...extra });
  t.envios.forEach((e, j) => {
    if (j === 0) {
      f("sale", 0.45); f("mover", 0.45, { de: ST.tolva + 6, a: ST.aes });
      f("aes", 0.55); f("mover", 0.4, { de: ST.aes, a: ST.crc }); f("crc", 0.55); f("mover", 0.45, { de: ST.crc, a: ST.codigo });
    } else {
      f("reaparece", 0.35, { j });
    }
    f("codigo", 0.65, { j }); f("mover", 0.4, { de: ST.codigo, a: ST.modular }); f("modular", 0.55, { j });
    f("mover", 0.35, { de: ST.modular, a: ST.repartir, entra: true }); f("aire", 1.7, { j });
    f("combinar", 0.35, { j }); f("mover", 0.4, { de: ST.combinar, a: ST.decodificar }); f("decodificar", 0.75, { j });
    f("mover", 0.4, { de: ST.decodificar, a: ST.revisar }); f("revisar", 0.55, { j }); f("respuesta", 0.9, { j });
    if (e.ok || j === t.envios.length - 1) {
      f("mover", 0.4, { de: ST.revisar, a: ST.descifrar }); f("descifrar", 0.5, { j });
      f("mover", 0.55, { de: ST.descifrar, a: ST.salida - 6, sale: true }); f("pausa", 0.35, { j });
    } else {
      f("espera", 0.35, { j });
    }
  });
}

const suave = (u) => u * u * (3 - 2 * u);
const ponerCartucho = (x, opacidad) => {
  cartucho.setAttribute("transform", translateAlong(P, [x, 0, 0]));
  cartucho.style.opacity = String(opacidad);
};
const largoParidad = (t) => {
  const c = datosCamino();
  if (c.clave === "sin") return 0;
  return Math.max(2, Math.min(30, C.CART.w * (1 / Math.max(0.05, t.tasa) - 1)));
};
// La paridad que agrega el código se dibuja detrás del cartucho (lado −x) con el largo que corresponde a la tasa
let ultimoLargo = -1;
const ponerParidad = (t, f, x) => {
  const largo = Math.round(largoParidad(t) * f * 2) / 2;
  if (largo !== ultimoLargo) {
    ultimoLargo = largo;
    paridad.innerHTML = largo >= 0.5
      ? solidSvg(slabOf({ x: -C.CART.w / 2 - largo, y: C.CART_Y - C.CART.d / 2 + 0.5, w: largo, d: C.CART.d - 1, r: 0.6 },
                        C.RAIL_H, C.CART.h - 1, P, 3, 0.3), { tone: "mid" })
      : "";
  }
  paridad.setAttribute("transform", translateAlong(P, [x, 0, 0]));
  paridad.style.opacity = cartucho.style.opacity;
};

function sello(el, u) {
  const baja = Math.sin(Math.PI * Math.min(1, u)) * 4;
  el.setAttribute("transform", translateAlong(P, [0, 0, -baja]));
}

function capasActivas(t) { return [...Array(t.capas)].map((_, i) => t.capa_inicial + i); }

function limpiarAire() {
  cuentas.forEach((fila) => fila.forEach((c) => { c.style.opacity = "0"; }));
  for (const lado of ["tx", "rx"]) {
    cab[lado].forEach((h) => h.removeAttribute("data-activa"));
    cab2[lado].forEach((h) => h.removeAttribute("data-activo"));
  }
}

function aire(t, e, j, u) {
  const activas = capasActivas(t);
  const bitsCapa = (t.simbolos / t.capas) * t.qm;
  for (let l = 0; l < 8; l++) {
    const on = activas.includes(l);
    for (const lado of ["tx", "rx"]) {
      cab[lado][l].toggleAttribute("data-activa", on);
      cab2[lado][l].toggleAttribute("data-activo", on);
    }
    if (!on) { cuentas[l].forEach((c) => { c.style.opacity = "0"; }); continue; }
    const i = l - t.capa_inicial;
    const p = j === 0 && t.errores_capa.length ? t.errores_capa[i] / bitsCapa : e.errores / (t.simbolos * t.qm);
    const malSimbolo = 1 - Math.pow(1 - Math.min(1, p), t.qm);  // al menos un bit errado en el símbolo
    const ruido = 1.2 + 7 * Math.min(1, p * 3);
    cuentas[l].forEach((c, n) => {
      const s = Math.max(0, Math.min(1, u * 1.55 - n * 0.09));
      const semilla = tb * 131 + j * 17 + l * 7 + n;
      const mal = azar(semilla) < malSimbolo;
      const dy = (azar(semilla + 0.3) * 2 - 1) * ruido * s, dz = (azar(semilla + 0.7) * 2 - 1) * ruido * 0.7 * s;
      const x = -C.MAST_X + 3 + s * (2 * C.MAST_X - 6);
      const [px, py] = iso([x, C.LANES[l] + dy, C.AIR_Z + dz], P);
      c.setAttribute("cx", px.toFixed(2));
      c.setAttribute("cy", py.toFixed(2));
      c.style.opacity = s > 0 && s < 1 ? "1" : "0";
      c.toggleAttribute("data-mal", mal && s > 0.45);
      c.toggleAttribute("data-bien", !(mal && s > 0.45));
    });
  }
}

const RUTA = C.harq;
const largoRuta = RUTA.slice(1).reduce((s, p, i) => s + Math.hypot(p[0] - RUTA[i][0], p[1] - RUTA[i][1], p[2] - RUTA[i][2]), 0);
function puntoRuta(u) {
  let falta = u * largoRuta;
  for (let i = 1; i < RUTA.length; i++) {
    const a = RUTA[i - 1], b = RUTA[i], l = Math.hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
    if (falta <= l) { const f = falta / l; return [a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f]; }
    falta -= l;
  }
  return RUTA[RUTA.length - 1];
}

function texto(fase, t, e) {
  const c = datosCamino(), R = t.tasa.toFixed(3);
  const capas = t.capas === 1 ? `la capa ${t.capa_inicial + 1}` : `las capas ${t.capa_inicial + 1} a ${t.capa_inicial + t.capas}`;
  const env = fase.j > 0 ? ` (envío ${fase.j + 1})` : "";
  switch (fase.tipo) {
    case "sale": return `1 · sale del archivo comprimido: ${fmt(t.bits)} bits`;
    case "aes": return DATOS.cifrado ? "2 · se cifra con AES-128 (modo contador)" : "2 · sin cifrar";
    case "crc": return `3 · se agrega el CRC ${t.crc}`;
    case "reaparece": return `NACK recibido: el transmisor saca el bloque de su buffer${env}`;
    case "codigo":
      if (fase.j > 0) return c.clave === "ldpc" ? `4 · LDPC: otra versión de redundancia, rv = ${e.rv}${env}` : `4 · se repite la misma palabra${env}`;
      return c.clave === "ldpc" ? `4 · LDPC de 5G: tasa ${R}` : c.clave === "sin" ? "4 · sin código: sólo el CRC" : `4 · ${c.nombre}: tasa ${R}`;
    case "modular": return `5 · ${MOD[t.qm]}: ${t.qm} bits por símbolo → ${fmt(t.simbolos)} símbolos${env}`;
    case "aire": return `aire · ${capas} · ${DATOS.snr_capa_db.toFixed(1)} dB por capa · ${fmt(e.errores)} bits errados`;
    case "combinar": return `7 · se juntan los símbolos de ${capas}`;
    case "decodificar":
      if (c.clave === "ldpc") return `8 · LDPC: ${fmt(e.iteraciones)} iteraciones (${(e.iteraciones / Math.max(1, t.bloques_codigo)).toFixed(1)} por parte)${env}`;
      return c.clave === "sin" ? `8 · se decide cada bit${env}` : `8 · síndrome y líder de clase${env}`;
    case "revisar": return e.ok ? "9 · CRC correcto → ACK" : "9 · CRC incorrecto → NACK";
    case "respuesta": return e.ok ? "ACK: el transmisor pasa al bloque siguiente" : (fase.j < t.envios.length - 1 ? "NACK: el transmisor lo envía otra vez" : "NACK y no quedan envíos: se entrega con errores");
    case "espera": return "el receptor guarda lo recibido para combinarlo con el próximo envío";
    case "descifrar": return DATOS.cifrado ? "10 · se descifra (el mismo AES en modo contador)" : "10 · sin cifrar";
    case "pausa": return `11 · ${t.ok ? "llegó bien" : "llegó con errores"} · ${t.latencia_ms.toFixed(3)} ms`;
    default: return null;
  }
}

function dibujar() {
  const c = datosCamino(), t = c.traza[tb];
  if (!t) return;
  // Fase actual
  let acumulado = 0, fase = fases[fases.length - 1], u = 1, idx = fases.length - 1;
  for (let i = 0; i < fases.length; i++) {
    if (reloj < acumulado + fases[i].dur) { fase = fases[i]; u = (reloj - acumulado) / fases[i].dur; idx = i; break; }
    acumulado += fases[i].dur;
  }
  const j = fase.j ?? 0, e = t.envios[Math.min(j, t.envios.length - 1)];
  // Estado de fondo según lo que ya pasó en este bloque
  const paso = (tipo, desde = 0) => fases.slice(0, idx).some((f) => f.tipo === tipo && (f.j ?? 0) >= desde);
  const cifrado = DATOS.cifrado && (paso("aes") || fase.tipo === "aes" && u > 0.5) && !(paso("descifrar") || fase.tipo === "descifrar" && u > 0.5);
  franja.style.opacity = cifrado ? "1" : "0";
  etiqueta.style.opacity = (paso("crc") || fase.tipo === "crc" && u > 0.5) && !(fase.tipo === "descifrar" || paso("descifrar") || fase.tipo === "pausa") ? "1" : "0";
  for (const s of Object.values(sellos)) s.setAttribute("transform", "");
  limpiarAire();
  lampara.ack.removeAttribute("data-on");
  lampara.nack.removeAttribute("data-on");
  pulso.style.opacity = "0";

  let x = ST.tolva + 6, op = 1;
  if (fase.tipo === "mover") {
    x = fase.de + (fase.a - fase.de) * suave(u);
    if (fase.entra) op = 1 - Math.max(0, (u - 0.7) / 0.3);
    if (fase.sale) op = 1 - Math.max(0, (u - 0.75) / 0.25);
  } else {
    x = { sale: ST.tolva + 6, aes: ST.aes, crc: ST.crc, reaparece: ST.codigo, codigo: ST.codigo, modular: ST.modular, aire: ST.repartir,
          combinar: ST.combinar, decodificar: ST.decodificar, revisar: ST.revisar, respuesta: ST.revisar, espera: ST.revisar,
          descifrar: ST.descifrar, pausa: ST.salida - 6 }[fase.tipo];
    if (fase.tipo === "sale") op = suave(u);
    if (fase.tipo === "reaparece") op = suave(u);
    if (fase.tipo === "aire") op = 0;
    if (fase.tipo === "combinar") op = suave(u);
    if (fase.tipo === "espera") op = 1 - suave(u);
    if (fase.tipo === "pausa") op = 0;
  }
  // Paridad: crece en el código (4) y se quita al decodificar (8), en cada envío
  let par;
  if (fase.tipo === "codigo") par = suave(u);
  else if (fase.tipo === "decodificar") par = 1 - suave(u);
  else par = paso("codigo", j) && !paso("decodificar", j) ? 1 : 0;
  ponerCartucho(x, op);
  ponerParidad(t, par, x);

  if (fase.tipo === "aes" && DATOS.cifrado) sello(sellos.aes, u);
  if (fase.tipo === "crc") sello(sellos.crc, u);
  if (fase.tipo === "descifrar" && DATOS.cifrado) sello(sellos.des, u);
  aguja(agujas.qam, ...DIAL.qam, (t.qm - 2) / 8 * (fase.tipo === "modular" ? suave(u) : 1));
  const iterFrac = c.clave === "ldpc" ? e.iteraciones / Math.max(1, t.bloques_codigo) / 25 : 0;
  aguja(agujas.iter, ...DIAL.iter, fase.tipo === "decodificar" ? iterFrac * suave(u) : (paso("decodificar", j) ? iterFrac : 0));
  aguja(agujas.snr, ...DIAL.snr, (DATOS.snr_capa_db + 10) / 50);
  if (fase.tipo === "aire") aire(t, e, j, u);
  if (fase.tipo === "revisar" || fase.tipo === "respuesta") lampara[e.ok ? "ack" : "nack"].setAttribute("data-on", e.ok ? "ack" : "nack");
  if (fase.tipo === "respuesta") {
    const [px, py] = iso(puntoRuta(suave(u)), P);
    pulso.setAttribute("cx", px.toFixed(2));
    pulso.setAttribute("cy", py.toFixed(2));
    pulso.style.opacity = "1";
    pulso.toggleAttribute("data-ack", e.ok);
    pulso.toggleAttribute("data-nack", !e.ok);
  }
  // Tejas: lo que falta por enviar (izquierda) y lo recuperado (derecha), entre los bloques que se muestran
  const n = c.traza.length, entregados = tb + (fase.tipo === "pausa" ? 1 : 0);
  tejas.tx.forEach((g, i) => { g.style.opacity = i < Math.ceil(10 * (n - tb) / n) ? "1" : "0"; });
  tejas.rx.forEach((g, i) => { g.style.opacity = i < Math.floor(10 * entregados / n) ? "1" : "0"; });

  const linea = texto(fase.tipo === "mover" ? (fases[idx + 1] || fase) : fase, t, e);
  const lectura = `Bloque ${tb + 1} de ${n}` + (t.envios.length > 1 ? ` · ${t.envios.length} envíos` : "") + (linea ? ` · ${linea}` : "");
  if (readout.textContent !== lectura) readout.textContent = lectura;
}

function resumen() {
  const c = datosCamino();
  const palabras = DATOS.palabras > 1 ? `, ${DATOS.palabras} bloques a la vez (uno por cada ${DATOS.capas_palabra} capas)` : "";
  pie.textContent = `${DATOS.escenario} · ${c.nombre}: ${fmt(c.bloques)} bloques, ${fmt(c.envios)} envíos, ${fmt(c.fallidos)} con errores al final. ` +
    `${DATOS.capas} capa${DATOS.capas > 1 ? "s" : ""} MIMO${palabras}; con SNR ${DATOS.snr_db.toFixed(1)} dB cada capa llega con ` +
    `${DATOS.snr_capa_db.toFixed(1)} dB porque la potencia se reparte entre ellas. Se muestran los primeros ${c.traza.length} bloques, con sus números reales.`;
}

function irA(n) {
  const total = datosCamino().traza.length;
  tb = (n + total) % total;
  reloj = 0;
  armarFases();
  dibujar();
  correr();
}

function tick(ahora) {
  const dt = ultimo ? Math.min((ahora - ultimo) / 1000, 1 / 20) : 0;
  ultimo = ahora;
  if (!pausa) {
    reloj += dt;
    const total = fases.reduce((s, f) => s + f.dur, 0);
    if (reloj >= total) { irA(tb + 1); return; }
  }
  dibujar();
  cuadro = visible && !pausa ? requestAnimationFrame(tick) : 0;
}
function correr() { if (!cuadro && visible && !pausa) { ultimo = 0; cuadro = requestAnimationFrame(tick); } }

// Controles
const caja = document.querySelector("[data-caminos]");
DATOS.caminos.forEach((c, i) => {
  const b = document.createElement("button");
  b.textContent = c.nombre;
  b.setAttribute("aria-pressed", String(i === 0));
  b.addEventListener("click", () => {
    camino = i;
    caja.querySelectorAll("button").forEach((x, n) => x.setAttribute("aria-pressed", String(n === i)));
    resumen();
    colorear();
    irA(0);
  });
  caja.appendChild(b);
});
const botonPausa = document.querySelector('[data-accion="pausa"]');
const ponerPausa = (v) => { pausa = v; botonPausa.textContent = pausa ? "▶︎ seguir" : "❚❚"; if (!pausa) correr(); };
botonPausa.addEventListener("click", () => ponerPausa(!pausa));
document.querySelector('[data-accion="anterior"]').addEventListener("click", () => irA(tb - 1));
document.querySelector('[data-accion="siguiente"]').addEventListener("click", () => irA(tb + 1));
stage.addEventListener("keydown", (ev) => {
  if (ev.key === "ArrowRight") { ev.preventDefault(); irA(tb + 1); }
  else if (ev.key === "ArrowLeft") { ev.preventDefault(); irA(tb - 1); }
  else if (ev.key === " ") { ev.preventDefault(); ponerPausa(!pausa); }
});
new IntersectionObserver((ent) => { visible = ent[ent.length - 1].isIntersecting; if (visible) correr(); }).observe(stage);

if (!DATOS || !DATOS.caminos.length || !DATOS.caminos[0].traza.length) {
  readout.textContent = "Sin datos: simula primero";
} else {
  resumen();
  colorear();
  armarFases();
  if (quieto.matches) {  // sin movimiento: se muestra el bloque cruzando el aire
    reloj = fases.slice(0, fases.findIndex((f) => f.tipo === "aire")).reduce((s, f) => s + f.dur, 0) + 0.9;
    ponerPausa(true);
  }
  dibujar();
  ponerPausa(pausa);
}
