// Figura «Visualizar»: un banco de pruebas donde cada bloque de transporte viaja por la red 5G/6G.
// Uso: node visual/build.mjs [simulacion.json de ejemplo]  →  escribe visual/viaje.html (tema claro)
// La aplicación reemplaza /*__DATOS__*/null por los datos reales de la última simulación.
import { readFileSync, writeFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import * as k from "./kit/iso-kit.mjs";

const HERE = dirname(fileURLToPath(import.meta.url));
const kitScript = () => readFileSync(join(HERE, "kit", "iso-kit.mjs"), "utf8").replace(/^export /gm, "");

const WIDTH = 1120;
const HEIGHT = 470;

// ---------------- Mundo (unidades ≈ px; x a lo largo de la línea, y hacia el frente, z arriba)
const PLATE = { x: -310, y: -72, w: 620, d: 144, r: 8 };
const PLATE_H = 12, FOOT_H = 5;
const RAIL_Y = 36, RAIL_D = 8, RAIL_H = 3;
const TX_RAIL = { x: -292, y: RAIL_Y, w: 240, d: RAIL_D, r: 1 };
const RX_RAIL = { x: 52, y: RAIL_Y, w: 250, d: RAIL_D, r: 1 };
const CART = { w: 20, d: 12, h: 8 };             // cartucho (bloque de transporte) en reposo, centrado en x = 0
const CART_Y = RAIL_Y + RAIL_D / 2;
const POST_BACK = 25, POST_FRONT = 49, POST_D = 5, BAR_Z = 26, BAR_H = 4;
const MAST_X = 35, MAST_TOP = 62, HEAD_H = 7;
const LANES = Array.from({ length: 8 }, (_, l) => -54 + 12 * l);
const AIR_Z = MAST_TOP + HEAD_H / 2;

// Estaciones (centro x) — el número grabado en su placa coincide con la leyenda
const ST = {
  tolva: -286, aes: -236, crc: -190, codigo: -140, modular: -94, repartir: -60,
  combinar: 60, decodificar: 106, revisar: 152, descifrar: 198, salida: 280,
};
const NUMEROS = [["tolva", 1], ["aes", 2], ["crc", 3], ["codigo", 4], ["modular", 5], ["repartir", 6],
                 ["combinar", 7], ["decodificar", 8], ["revisar", 9], ["descifrar", 10], ["salida", 11]];

const P = k.fitProjection(
  [...k.boxCorners(PLATE, -PLATE_H - FOOT_H, 0), [0, 0, MAST_TOP + HEAD_H + 4], [ST.aes, POST_BACK, BAR_Z + BAR_H + 10],
   [ST.revisar, POST_BACK, BAR_Z + BAR_H + 12]],
  WIDTH, HEIGHT, { pad: 26, azimuth: 60 },
);

const at = (p) => k.iso(p, P);
const S = (paths, style) => k.solidSvg(paths, style);
const L = (d, style) => k.lineSvg(d, style);
const D = (points, style) => k.dotsSvg(points, style);
const slab = (plan, z, h, steps = 3, bevel = 0.5) => k.slabOf(plan, z, h, P, steps, bevel);
const byDepth = (pts) => [...pts].sort((a, b) => k.depthOf([a[0], a[1], 0], P) - k.depthOf([b[0], b[1], 0], P));
const screws = (plan, inset, z) => D(k.corners(plan, inset).map(([x, y]) => at([x, y, z])), { size: 0.42 });

// ---------------- Base
function base() {
  const out = [`<path class="iso-halo" d="${k.haloOf(PLATE, -PLATE_H - FOOT_H, PLATE_H, P)}"/>`];
  for (const [x, y] of byDepth(k.corners(PLATE, 24))) {
    out.push(S(k.cylinder(x, y, 8, -PLATE_H - FOOT_H, 1.6, P, 28), { tone: "lo" }));
    out.push(S(k.cylinder(x, y, 6.2, -PLATE_H - FOOT_H + 1.6, FOOT_H - 1.6, P, 28), { tone: "mid" }));
  }
  out.push(S(slab(PLATE, -PLATE_H, PLATE_H, 8, 1.6), { tone: "mid" }));
  out.push(L(k.planOutline(k.insetPlan(PLATE, 5), 0, P), { tone: "faint" }));
  const esquinas = k.corners(PLATE, 11);
  out.push(L(esquinas.map(([x, y]) => k.ring(x, y, 2.2, 0, P, 16)).join(""), { tone: "lo" }));
  out.push(D(esquinas.map(([x, y]) => at([x, y, 0])), { size: 0.5 }));
  // Regla grabada a lo largo del frente
  const regla = { x: -292, y: 64, w: 594, d: 5, r: 1 };
  out.push(S(slab(regla, 0, 0.6, 3, 0), { tone: "lo", crease: "none" }));
  const t = k.topTicks(regla.x + 4, regla.x + regla.w - 4, 6, 5, regla.y + 0.6, 0.6, [1.8, 3.4], P);
  out.push(L(t.minor, { tone: "lo" }), L(t.major, { tone: "mid" }));
  out.push(D([at([regla.x + 2, regla.y + 3, 0.6]), at([regla.x + regla.w - 2, regla.y + 3, 0.6])], { size: 0.45 }));
  // Placa de identificación al frente, bajo el aire
  const placa = { x: -30, y: 52, w: 60, d: 9, r: 1.4 };
  out.push(S(slab(placa, 0, 1, 3, 0.3), { tone: "mid" }));
  out.push(screws(placa, 2.2, 1));
  out.push(k.faceTextSvg(k.topMatrix([0, 57.6], 1, P, "x"), "TS 38.211 · 38.212", { size: 3, tone: "lo", anchor: "middle" }));
  // Placas numeradas de cada estación
  for (const [clave, n] of NUMEROS) {
    const x = ST[clave];
    const p = { x: x - 7, y: 55, w: 14, d: 8, r: 1.2 };
    out.push(S(slab(p, 0, 0.8, 3, 0.2), { tone: "mid", crease: "none" }));
    out.push(D([at([p.x + 1.6, p.y + 4, 0.8]), at([p.x + p.w - 1.6, p.y + 4, 0.8])], { size: 0.35, tone: "lo" }));
    out.push(k.faceTextSvg(k.topMatrix([x, 60.6], 0.8, P, "x"), String(n), { size: 5.4, tone: "hi", anchor: "middle" }));
  }
  return out.join("");
}

// ---------------- Rieles
function rieles() {
  const out = [];
  for (const r of [TX_RAIL, RX_RAIL]) {
    out.push(S(slab(r, 0, RAIL_H, 3, 0.4), { tone: "mid" }));
    out.push(L(k.lineOnTop([r.x + 2, RAIL_Y + RAIL_D / 2], [r.x + r.w - 2, RAIL_Y + RAIL_D / 2], RAIL_H, P), { tone: "faint" }));
    for (const x of [r.x - 5, r.x + r.w]) {
      out.push(S(slab({ x, y: RAIL_Y - 2, w: 5, d: RAIL_D + 4, r: 1 }, 0, 7, 3, 0.4), { tone: "mid" }));
      out.push(D([at([x + 2.5, RAIL_Y + RAIL_D / 2, 7])], { size: 0.45 }));
    }
    const pernos = [];
    for (let x = r.x + 10; x < r.x + r.w - 6; x += 30) pernos.push(at([x, RAIL_Y + RAIL_D / 2, RAIL_H]));
    out.push(D(pernos, { size: 0.38, tone: "lo" }));
  }
  return out.join("");
}

// ---------------- Pórtico (dos postes y travesaño); el cabezal va aparte
function porticoAtras(x) {
  const p = { x: x - 4, y: POST_BACK, w: 8, d: POST_D, r: 1 };
  return S(slab({ x: x - 6, y: POST_BACK - 1, w: 12, d: POST_D + 2, r: 1.2 }, 0, 2, 3, 0.4), { tone: "mid" }) +
         S(slab(p, 2, BAR_Z - 2, 3, 0.4), { tone: "mid" }) + screws({ x: x - 6, y: POST_BACK - 1, w: 12, d: POST_D + 2 }, 1.6, 2);
}
function porticoFrente(x) {
  const p = { x: x - 4, y: POST_FRONT, w: 8, d: POST_D, r: 1 };
  const bar = { x: x - 6, y: POST_BACK, w: 12, d: POST_FRONT + POST_D - POST_BACK, r: 1.2 };
  return S(slab({ x: x - 6, y: POST_FRONT - 1, w: 12, d: POST_D + 2, r: 1.2 }, 0, 2, 3, 0.4), { tone: "mid" }) +
         S(slab(p, 2, BAR_Z - 2, 3, 0.4), { tone: "mid" }) + screws({ x: x - 6, y: POST_FRONT - 1, w: 12, d: POST_D + 2 }, 1.6, 2) +
         S(slab(bar, BAR_Z, BAR_H, 3, 0.6), { tone: "mid" }) +
         L(k.lineOnTop([x, POST_BACK + 2], [x, POST_FRONT + POST_D - 2], BAR_Z + BAR_H, P), { tone: "faint" }) +
         D([at([x, POST_BACK + 2.5, BAR_Z + BAR_H]), at([x, POST_FRONT + POST_D - 2.5, BAR_Z + BAR_H])], { size: 0.42 });
}

// Cabezal que baja a sellar (AES, CRC, descifrar): cuerpo cilíndrico con su marca; se mueve con translateAlong
function cabezalSello(x, clase, marca) {
  const out = [];
  out.push(S(k.cylinder(x, CART_Y, 2.2, 19, BAR_Z - 19, P, 20), { tone: "mid", crease: "none" }));
  out.push(S(k.cylinder(x, CART_Y, 5.6, 14, 5, P, 28, 0.4), { tone: "hi" }));
  out.push(L(k.sideArc(x, CART_Y, 5.6, 16.5, P, 20), { tone: "faint" }));
  if (marca === "llave") {
    out.push(L(k.lineOnTop([x - 1.8, CART_Y], [x + 1.8, CART_Y], 19, P), { tone: "lo" }));
    out.push(D([at([x, CART_Y - 0.4, 19])], { size: 0.7, tone: "lo" }));
  } else {
    out.push(L(k.ring(x, CART_Y, 3.4, 19, P, 20), { tone: "lo" }));
  }
  return `<g data-part="${clase}">${out.join("")}</g>`;
}

// Esfera de reloj sobre un cabezal o una caja: anillo, marcas y aguja
function esfera(x, y, z, r, marcas, mayores, clase) {
  const out = [];
  out.push(S(k.cylinder(x, y, r, z, 2.2, P, 48, 0.4), { tone: "hi", crease: "lo" }));
  out.push(L(k.ring(x, y, r - 1.2, z + 2.2, P, 48), { tone: "lo" }));
  const t = k.radialTicks(x, y, r - 1.6, marcas, mayores, z + 2.2, [0.9, 1.8], P, -Math.PI * 0.75, Math.PI * 1.5);
  out.push(L(t.minor, { tone: "lo" }), L(t.major, { tone: "mid" }));
  out.push(`<path data-part="${clase}" class="iso-line" data-tone="lit" data-free="start" d="" style="stroke-width:0.9"/>`);
  out.push(D([at([x, y, z + 2.25])], { size: 0.7, tone: "hi" }));
  return out.join("");
}

// ---------------- Estaciones del transmisor
function tolva(x, y0) {
  const out = [];
  const caja = { x: x - 14, y: y0, w: 28, d: 22, r: 1.5 };
  out.push(S(slab(caja, 0, 3, 3, 0.4), { tone: "mid" }));
  out.push(S(slab({ x: x - 14, y: y0, w: 28, d: 2, r: 0.6 }, 3, 12, 3, 0.3), { tone: "mid" }));
  out.push(S(slab({ x: x - 14, y: y0 + 2, w: 2, d: 18, r: 0.6 }, 3, 12, 3, 0.3), { tone: "mid" }));
  out.push(S(slab({ x: x + 12, y: y0 + 2, w: 2, d: 18, r: 0.6 }, 3, 12, 3, 0.3), { tone: "mid" }));
  out.push(screws(caja, 1.6, 3));
  // Pila de tejas: el archivo comprimido que falta por enviar (se ocultan según el avance)
  const tejas = [];
  for (let i = 0; i < 10; i++)
    tejas.push(`<g data-teja="${x < 0 ? "tx" : "rx"}" data-i="${i}">${S(slab({ x: x - 9, y: y0 + 5, w: 18, d: 12, r: 0.8 }, 3 + i * 1.3, 1.1, 3, 0.2), { tone: "hi", crease: "none" })}</g>`);
  out.push(tejas.join(""));
  out.push(S(slab({ x: x - 12, y: y0 + 20, w: 24, d: 2, r: 0.6 }, 3, 6, 3, 0.3), { tone: "mid" }));
  return out.join("");
}

function tunelAtras(x) {
  return S(slab({ x: x - 20, y: POST_BACK, w: 40, d: 3, r: 0.8 }, 0, 24, 3, 0.4), { tone: "mid" });
}
function tunelFrente(x) {
  const out = [];
  out.push(S(slab({ x: x - 20, y: POST_FRONT + 2, w: 40, d: 3, r: 0.8 }, 0, 24, 3, 0.4), { tone: "mid" }));
  out.push(S(slab({ x: x - 22, y: POST_BACK - 1, w: 44, d: POST_FRONT + 6 - POST_BACK, r: 1.6 }, 24, 3, 4, 0.6), { tone: "mid" }));
  out.push(L(k.planOutline({ x: x - 18, y: POST_BACK + 3, w: 36, d: POST_FRONT - POST_BACK - 2, r: 1 }, 27, P, 3), { tone: "faint" }));
  // Ventana de rejilla en la pared del frente: el buffer circular del código
  const v = [];
  for (let i = 0; i < 9; i++) {  // ranuras cerradas en la cara del frente
    const x0 = x - 14.6 + i * 3.5, cara = POST_FRONT + 5;
    v.push(k.pathOf([[x0, cara, 6], [x0 + 1.2, cara, 6], [x0 + 1.2, cara, 18], [x0, cara, 18]].map(at), true));
  }
  out.push(L(v.join(""), { tone: "lo" }));
  out.push(screws({ x: x - 20, y: POST_FRONT + 5, w: 40, d: 0 }, 2, 24));
  return out.join("");
}

function cajaConector(x, y) {
  const c = { x: x - 6, y, w: 12, d: 8, r: 1 };
  return S(slab(c, 0, 6, 3, 0.4), { tone: "mid" }) + D([at([x - 3, y + 4, 6]), at([x + 3, y + 4, 6])], { size: 0.42 });
}

function bloqueReparto(x, salidaAtras) {
  // Distribuidor: recibe el cartucho por el riel y tiene 8 puertos (uno por capa) en la cara trasera
  const out = [];
  const b = { x: x - 8, y: RAIL_Y - 6, w: 16, d: RAIL_D + 14, r: 1.6 };
  out.push(S(slab(b, 0, 18, 3, 0.6), { tone: "mid" }));
  out.push(L(k.planOutline(k.insetPlan(b, 2.4), 18, P, 3), { tone: "faint" }));
  out.push(screws(b, 2, 18));
  for (let l = 0; l < 8; l++) {
    const px = salidaAtras(l);
    out.push(S(k.cylinder(px, RAIL_Y - 6.6, 0.8, 1.2, 2.2, P, 10), { tone: "lo", crease: "none" }));
  }
  return out.join("");
}

// ---------------- Antenas (mástiles) y cables
const puertoTX = (l) => -66 + 1.75 * l;
const puertoRX = (l) => 66 - 1.75 * l;

function cables() {
  const out = [];
  for (let l = 0; l < 8; l++) {
    const y = LANES[l];
    out.push(`<g data-cable="tx${l}">${k.wireSvg(at([puertoTX(l), RAIL_Y - 6.6, 1.2]), at([-MAST_X - 4.2, y, 0.6]),
      { via: [at([puertoTX(l), y, 0.6])], tone: "lo", size: 0.9 })}</g>`);
    out.push(`<g data-cable="rx${l}">${k.wireSvg(at([MAST_X + 4.2, y, 0.6]), at([puertoRX(l), RAIL_Y - 6.6, 1.2]),
      { via: [at([puertoRX(l), y, 0.6])], tone: "lo", size: 0.9 })}</g>`);
  }
  return out.join("");
}

function mastil(x, y, clave) {
  const out = [];
  out.push(S(k.cylinder(x, y, 4.2, 0, 2, P, 28, 0.4), { tone: "mid" }));
  out.push(S(k.cylinder(x, y, 1.4, 2, MAST_TOP - 2, P, 14), { tone: "mid", crease: "none" }));
  out.push(L(k.sideArc(x, y, 1.4, 30, P, 10), { tone: "faint" }));
  out.push(`<g data-cabeza="${clave}">${S(k.cylinder(x, y, 3, MAST_TOP, HEAD_H, P, 24, 0.5), { tone: "mid" })}</g>`);
  out.push(L(k.ring(x, y, 1.6, MAST_TOP + HEAD_H, P, 14), { tone: "lo" }));
  return out.join("");
}

function carriles() {
  // Por capa: mástil TX, mástil RX, guía del aire y sus cuentas (símbolos), de atrás hacia adelante
  const out = [];
  for (let l = 0; l < 8; l++) {
    const y = LANES[l];
    out.push(mastil(-MAST_X, y, `tx${l}`), mastil(MAST_X, y, `rx${l}`));
    out.push(`<path data-guia="${l}" class="iso-line" data-tone="faint" d="${k.segment([-MAST_X + 3, y, AIR_Z], [MAST_X - 3, y, AIR_Z], P)}" style="stroke-dasharray:1.2 2.4"/>`);
    out.push(`<g data-carril="${l}"></g>`);
  }
  return out.join("");
}

function medidorSNR() {
  const c = { x: -16, y: 32, w: 32, d: 16, r: 1.6 };
  return S(slab(c, 0, 9, 3, 0.6), { tone: "mid" }) + screws(c, 2, 9) + esfera(0, 40, 9, 6.6, 25, 5, "aguja-snr");
}

function cableHARQ() {
  // Del revisor CRC (9) al codificador (4), por detrás de todo: lleva el ACK o el NACK
  const ruta = [[ST.revisar, 14, 3], [ST.revisar, 12, 0.6], [ST.revisar, -69, 0.6], [ST.codigo, -69, 0.6], [ST.codigo, 12, 0.6], [ST.codigo, 14, 3]];
  return { ruta, svg: k.wireSvg(at(ruta[0]), at(ruta[ruta.length - 1]), { via: ruta.slice(1, -1).map(at), tone: "lo", size: 0.9 }) };
}

// ---------------- Cartucho (lo único iluminado: el bloque que viaja)
function cartucho() {
  const info = { x: -CART.w / 2, y: CART_Y - CART.d / 2, w: CART.w, d: CART.d, r: 1 };
  const out = [];
  out.push(`<g data-part="cuerpo">${S(slab(info, RAIL_H, CART.h, 3, 0.5), { tone: "hi", lit: true })}</g>`);
  // Franja de cifrado (aparece después de la estación 2)
  const franja = [];
  for (let i = 0; i < 6; i++) franja.push(k.lineOnTop([info.x + 3 + i * 2.8, info.y], [info.x + 3 + i * 2.8, info.y + info.d], RAIL_H + CART.h, P));
  out.push(`<g data-part="franja" style="opacity:0">${L(franja.join(""), { tone: "lo" })}</g>`);
  // Etiqueta del CRC (estación 3)
  out.push(`<g data-part="etiqueta" style="opacity:0">${S(slab({ x: info.x + info.w - 4, y: info.y + 2, w: 3, d: info.d - 4, r: 0.5 }, RAIL_H + CART.h, 1.2, 3, 0.2), { tone: "mid" })}</g>`);
  return out.join("");
}
// ---------------- Estaciones del receptor
function lamparas(x) {
  const out = [];
  for (const [dx, clase] of [[-3.2, "ack"], [3.2, "nack"]]) {
    out.push(S(k.cylinder(x + dx, 40, 2.4, BAR_Z + BAR_H, 1.2, P, 20, 0.3), { tone: "mid" }));
    out.push(`<g data-lampara="${clase}">${S(k.cylinder(x + dx, 40, 1.8, BAR_Z + BAR_H + 1.2, 2.2, P, 20, 0.4), { tone: "hi" })}</g>`);
  }
  return out.join("");
}

function cabezalDial(x, clase, marcas, mayores) {
  const out = [];
  out.push(S(k.cylinder(x, CART_Y, 2.2, 18, BAR_Z - 18, P, 20), { tone: "mid", crease: "none" }));
  out.push(S(slab({ x: x - 7, y: CART_Y - 7, w: 14, d: 14, r: 2 }, 14, 4, 4, 0.5), { tone: "mid" }));
  out.push(esfera(x, CART_Y, BAR_Z + BAR_H, 6.2, marcas, mayores, clase));
  return out.join("");
}

// ---------------- Armado en orden de pintura (atrás → adelante, abajo → arriba)
const harq = cableHARQ();
const cuerpo = [
  base(),
  `<g data-part="harq">${harq.svg}</g>`,
  `<circle data-part="pulso" r="1.8" cx="0" cy="0" style="opacity:0"/>`,
  cajaConector(ST.codigo, 14), cajaConector(ST.revisar, 14),
  tolva(ST.tolva, 2), tolva(ST.salida, 2),
  cables(),
  carriles(),
  medidorSNR(),
  rieles(),
  porticoAtras(ST.aes), porticoAtras(ST.crc), porticoAtras(ST.modular), tunelAtras(ST.codigo),
  porticoAtras(ST.decodificar), porticoAtras(ST.revisar), porticoAtras(ST.descifrar),
  `<g data-part="paridad"></g>`,
  `<g data-part="cartucho">${cartucho()}</g>`,
  bloqueReparto(ST.repartir, puertoTX), bloqueReparto(ST.combinar, puertoRX),
  tunelFrente(ST.codigo),
  porticoFrente(ST.aes), cabezalSello(ST.aes, "sello-aes", "llave"),
  porticoFrente(ST.crc), cabezalSello(ST.crc, "sello-crc", "anillo"),
  porticoFrente(ST.modular), cabezalDial(ST.modular, "aguja-qam", 20, 4),
  porticoFrente(ST.decodificar), cabezalDial(ST.decodificar, "aguja-iter", 25, 5),
  porticoFrente(ST.revisar), lamparas(ST.revisar),
  porticoFrente(ST.descifrar), cabezalSello(ST.descifrar, "sello-des", "llave"),
];

const svg = k.figureSvg({
  width: WIDTH, height: HEIGHT,
  label: "Banco de pruebas de un enlace 5G y 6G. A la izquierda, el transmisor: el archivo comprimido sale de una tolva en bloques; cada bloque se cifra, recibe su CRC, se codifica, se modula y se reparte entre las capas MIMO. En el centro, ocho antenas de cada lado: los símbolos cruzan el aire con ruido. A la derecha, el receptor combina las capas, decodifica, revisa el CRC y responde ACK o NACK por el cable de atrás; si falla, el bloque se vuelve a enviar.",
  body: cuerpo,
});

// ---------------- Datos de ejemplo (para ver la figura sin la aplicación)
let ejemplo = null;
const rutaEjemplo = process.argv[2];
if (rutaEjemplo && existsSync(rutaEjemplo)) {
  const s = JSON.parse(readFileSync(rutaEjemplo, "utf8"));
  ejemplo = {
    escenario: s.escenario.nombre, generacion: s.escenario.generacion, snr_db: s.canal.snr_db, capas: s.mimo.capas,
    palabras: s.mimo.palabras_codigo, capas_palabra: s.mimo.capas_por_palabra, snr_capa_db: s.mimo.snr_capa_db,
    cifrado: s.cifrado.activo, rtt_ms: s.escenario.rtt_s * 1e3, unidad_ms: s.escenario.unidad_s * 1e3,
    caminos: s.caminos.map((c) => ({
      clave: c.clave, nombre: c.nombre, bloques: c.bloques_transporte, envios: c.transmisiones,
      fallidos: Math.round(c.bler_final * c.bloques_transporte), identico: c.identico, descifrado_ok: c.descifrado_ok,
      traza: c.traza.map((t) => ({ tb: t.tb, bits: t.bits, qm: t.qm, tasa: t.tasa, simbolos: t.simbolos, palabra: t.palabra,
        capa_inicial: t.capa_inicial, capas: t.capas, errores_capa: t.errores_capa, ok: t.ok, latencia_ms: t.latencia * 1e3,
        bloques_codigo: t.bloques_codigo || 0, crc: t.crc_hex,
        envios: t.envios.map((x) => ({ errores: x.errores, ok: x.ok, iteraciones: x.iteraciones, rv: x.rv })) })),
    })),
  };
}

const CONST = { P, ST, LANES, MAST_X, AIR_Z, CART_Y, RAIL_H, CART, harq: harq.ruta };
const live = readFileSync(join(HERE, "vivo.js"), "utf8")
  .replace("/*__CONST__*/null", JSON.stringify(CONST))
  .replace("/*__EJEMPLO__*/null", JSON.stringify(ejemplo));

const controles = `<div class="viaje-controles" role="group" aria-label="Controles">
  <div class="viaje-caminos" data-caminos></div>
  <div class="viaje-botones"><button data-accion="anterior" aria-label="Bloque anterior">◀</button><button data-accion="pausa" aria-label="Pausar">❚❚</button><button data-accion="siguiente" aria-label="Bloque siguiente">▶</button></div>
</div>`;

const body = k.plateHtml({
  fig: "Fig 1",
  title: "Un bloque por la red, capa por capa",
  hint: "Flechas: bloque · espacio: pausa",
  readout: "Bloque 1",
  keys: [
    { mark: "lit", label: "El bloque de transporte que viaja, del color de su código (azul LDPC, naranja código de bloque, gris sin código); en gris detrás, la paridad que agrega el código" },
    { mark: "raised", label: "1 archivo comprimido · 2 cifrado AES-128 · 3 CRC · 4 código (y buffer HARQ) · 5 modulación QAM · 6 reparto en capas" },
    { mark: "raised", label: "7 combinar capas · 8 decodificar (aguja: iteraciones) · 9 revisar CRC (luz ACK o NACK) · 10 descifrar · 11 archivo recuperado" },
    { mark: "line", label: "Cuentas del aire: símbolos de cada capa (rojas: llegaron más cerca de otro punto). Cable de atrás: respuesta ACK/NACK. Esfera del centro: SNR por capa" },
  ],
  caption: "__PIE__",
  body: `${controles}<div class="iso-stage" tabindex="0" aria-label="Viaje de los bloques: flechas para cambiar de bloque, espacio para pausar">${svg}</div>`,
});

const css = `
.viaje-controles{display:flex;justify-content:space-between;align-items:center;gap:12px;margin:0 0 6px;font:12px/1.3 system-ui,sans-serif}
.viaje-caminos{display:flex;gap:6px;flex-wrap:wrap}
.viaje-controles button{font:inherit;border:1px solid var(--anatomy-lo);background:var(--anatomy-face);color:var(--anatomy-hi);border-radius:6px;padding:4px 10px;cursor:pointer}
.viaje-controles button[aria-pressed="true"]{border-color:var(--anatomy-hi);font-weight:600}
.viaje-botones{display:flex;gap:4px}
[data-carril] circle{stroke-width:.5}
[data-carril] circle[data-bien]{fill:var(--anatomy-hi);stroke:var(--anatomy-face)}
[data-carril] circle[data-mal]{fill:#d1242f;stroke:var(--anatomy-face)}
[data-part="pulso"]{fill:var(--anatomy-hi)}
[data-part="pulso"][data-ack]{fill:#1a7f37}
[data-part="pulso"][data-nack]{fill:#d1242f}
[data-lampara] .iso-side,[data-lampara] .iso-top{transition:fill .25s}
[data-lampara][data-on="ack"] path:not(.iso-line){fill:#2da44e}
[data-lampara][data-on="nack"] path:not(.iso-line){fill:#e5534b}
[data-cabeza][data-activa] path:not(.iso-line){fill:var(--anatomy-hi)}
[data-cable][data-activo] .iso-line{stroke:var(--anatomy-hi)}
.iso-page{margin:8px auto 8px}
`;

const html = k.pageHtml({ title: "Viaje de los bloques", theme: "light", body, script: kitScript() + live, width: WIDTH })
  .replace("</style>", css + "</style>").replace("__PIE__", "<span data-pie></span>");
writeFileSync(join(HERE, "viaje.html"), html);
console.log("escrito visual/viaje.html", (html.length / 1024).toFixed(0), "KB");
