#include "../include/Enlace.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include "../include/Json.h"

using namespace std;

// ======================= Tablas de 3GPP =======================

// TS 38.214 Tabla 5.1.3.2-1: TBS para N_info <= 3824
static const int TABLA_TBS[] = {
    24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120, 128, 136, 144, 152, 160, 168, 176, 184, 192, 208, 224,
    240, 256, 272, 288, 304, 320, 336, 352, 368, 384, 408, 432, 456, 480, 504, 528, 552, 576, 608, 640, 672, 704,
    736, 768, 808, 848, 888, 928, 984, 1032, 1064, 1128, 1160, 1192, 1224, 1256, 1288, 1320, 1352, 1416, 1480,
    1544, 1608, 1672, 1736, 1800, 1864, 1928, 2024, 2088, 2152, 2216, 2280, 2408, 2472, 2536, 2600, 2664, 2728,
    2792, 2856, 2976, 3104, 3240, 3368, 3496, 3624, 3752, 3824};

struct MCS {
    int Qm;
    double R1024;  // tasa de código objetivo x 1024
};

// TS 38.214 Tabla 5.1.3.1-2 (hasta 256QAM), índices 0-27
static const MCS MCS_256[] = {
    {2, 120}, {2, 193}, {2, 308}, {2, 449}, {2, 602}, {4, 378}, {4, 434}, {4, 490}, {4, 553}, {4, 616},
    {4, 658}, {6, 466}, {6, 517}, {6, 567}, {6, 616}, {6, 666}, {6, 719}, {6, 772}, {6, 822}, {6, 873},
    {8, 682.5}, {8, 711}, {8, 754}, {8, 797}, {8, 841}, {8, 885}, {8, 916.5}, {8, 948}};

// TS 38.214 Tabla 5.1.3.1-1 (hasta 64QAM), índices 0-28
static const MCS MCS_64[] = {
    {2, 120}, {2, 157}, {2, 193}, {2, 251}, {2, 308}, {2, 379}, {2, 449}, {2, 526}, {2, 602}, {2, 679},
    {4, 340}, {4, 378}, {4, 434}, {4, 490}, {4, 553}, {4, 616}, {4, 658}, {6, 438}, {6, 466}, {6, 517},
    {6, 567}, {6, 616}, {6, 666}, {6, 719}, {6, 772}, {6, 822}, {6, 873}, {6, 910}, {6, 948}};

// TS 38.212 Tabla 5.3.2-1: conjuntos de tamaños de elevación Zc = a * 2^j
static vector<int> tamanosElevacion() {
    vector<int> z;
    for (int a : {2, 3, 5, 7, 9, 11, 13, 15})
        for (int v = a; v <= 384; v *= 2) z.push_back(v);
    sort(z.begin(), z.end());
    return z;
}

// ======================= TBS (TS 38.214 §5.1.3.2) =======================

int64_t tbsNR(int nPRB, int simbolos, int dmrsPorPRB, int overheadPorPRB, double R, int Qm, int capas) {
    int nRE1 = 12 * simbolos - dmrsPorPRB - overheadPorPRB;   // N'_RE
    int64_t nRE = (int64_t)min(156, nRE1) * nPRB;              // N_RE
    double nInfo = nRE * R * Qm * capas;
    if (nInfo <= 0) return 0;
    if (nInfo <= 3824) {
        int n = max(3, (int)floor(log2(nInfo)) - 6);
        double p = pow(2.0, n);
        double nInfo1 = max(24.0, p * floor(nInfo / p));
        for (int t : TABLA_TBS)
            if (t >= nInfo1) return t;
        return 3824;
    }
    int n = (int)floor(log2(nInfo - 24)) - 5;
    double p = pow(2.0, n);
    double nInfo1 = max(3840.0, p * floor((nInfo - 24) / p + 0.5));  // los empates se redondean hacia arriba
    if (R <= 0.25) {
        double C = ceil((nInfo1 + 24) / 3816);
        return (int64_t)(8 * C * ceil((nInfo1 + 24) / (8 * C)) - 24);
    }
    if (nInfo1 > 8424) {
        double C = ceil((nInfo1 + 24) / 8424);
        return (int64_t)(8 * C * ceil((nInfo1 + 24) / (8 * C)) - 24);
    }
    return (int64_t)(8 * ceil((nInfo1 + 24) / 8) - 24);
}

// ======================= Codificación de canal (TS 38.212) =======================

SegmentacionNR segmentarNR(int64_t A, double R) {
    SegmentacionNR s;
    // §7.2.2: selección del grafo base del LDPC
    s.grafoBase = (A <= 292 || (A <= 3824 && R <= 0.67) || R <= 0.25) ? 2 : 1;
    // §7.2.1: CRC del bloque de transporte
    s.crcTB = A > 3824 ? 24 : 16;
    int64_t B = A + s.crcTB;
    // §5.2.2: segmentación en bloques de código
    int64_t Kcb = s.grafoBase == 1 ? 8448 : 3840;
    int64_t Bprima;
    if (B <= Kcb) {
        s.crcCB = 0;
        s.C = 1;
        Bprima = B;
    } else {
        s.crcCB = 24;
        s.C = (B + (Kcb - s.crcCB) - 1) / (Kcb - s.crcCB);
        Bprima = B + s.C * s.crcCB;
    }
    s.Kprima = Bprima / s.C;
    int Kb = s.grafoBase == 1 ? 22 : (B > 640 ? 10 : B > 560 ? 9 : B > 192 ? 8 : 6);
    s.Zc = 384;
    for (int z : tamanosElevacion())
        if ((int64_t)Kb * z >= s.Kprima) {
            s.Zc = z;
            break;
        }
    s.K = (s.grafoBase == 1 ? 22 : 10) * (int64_t)s.Zc;
    s.relleno = s.K - s.Kprima;
    s.N = (s.grafoBase == 1 ? 66 : 50) * (int64_t)s.Zc;
    return s;
}

// ======================= Configuraciones =======================

struct Banda {
    const char* nombre;
    const char* rango;      // FR1 / FR2
    const char* banda;      // banda de operación NR
    double frecuenciaGHz;
    int mu;                 // numerología: separación entre subportadoras 15 * 2^mu kHz (TS 38.211 §4.2)
    int anchoMHz;
    int nRB;                // TS 38.101-1/-2 Tabla 5.3.2-1
    bool tdd;
    double overhead;        // TS 38.306 §4.1.2: 0.14 FR1 DL, 0.18 FR2 DL
};

static const Banda BANDAS[] = {
    {"5G NR · n28 (700 MHz)", "FR1", "n28", 0.7, 0, 20, 106, false, 0.14},
    {"5G NR · n78 (3.5 GHz)", "FR1", "n78", 3.5, 1, 100, 273, true, 0.14},
    {"5G NR · n257 (28 GHz)", "FR2", "n257", 28, 3, 400, 264, true, 0.18},
};

struct Servicio {
    const char* nombre;
    int simbolos;           // símbolos OFDM por transmisión (slot de 14 con 1 de PDCCH, o mini-slot)
    double bler;            // BLER objetivo de la adaptación de enlace
    double brechaDb;        // margen respecto a Shannon para elegir el MCS (modelo, no definido por 3GPP)
    double latenciaS;       // requisito de latencia del plano de usuario, ITU-R M.2410
};

static Servicio servicioDe(const string& s) {
    if (s == "urllc") return {"URLLC", 7, 1e-5, 4.0, 1e-3};
    return {"eMBB", 13, 0.1, 1.5, 4e-3};
}

// Una palabra de código lleva hasta 4 capas; con más se usan dos (TS 38.211 §7.3.1.3)
static vector<int> capasPorPalabra(int capas) {
    if (capas <= 4) return {capas};
    return {capas / 2, capas - capas / 2};
}

// ======================= Cálculo =======================

int ejecutarEnlace(const string& ruta, uint64_t bytesOriginal, uint64_t bytesComprimido, const ParametrosEnlace& p,
                   const string& rutaJson) {
    const Servicio sv = servicioDe(p.servicio);
    const MCS* tabla = p.tablaMCS == 64 ? MCS_64 : MCS_256;
    const int numMCS = p.tablaMCS == 64 ? 29 : 28;
    const int dmrs = 12;    // 1 símbolo de DMRS tipo 1 con 2 grupos CDM sin datos: 12 RE por PRB
    const double snr = pow(10.0, p.snrDb / 10);
    const double eeMax = log2(1 + snr / pow(10.0, sv.brechaDb / 10));  // eficiencia espectral alcanzable por capa

    ofstream archivo(rutaJson);
    Json j(archivo);
    j.objeto().str("modo", "enlace");
    j.objeto("configuracion")
     .str("archivo", ruta).ent("bytes_original", bytesOriginal).ent("bytes_comprimido", bytesComprimido)
     .num("snr_db", p.snrDb).ent("capas", p.capas).str("servicio", sv.nombre).ent("tabla_mcs", p.tablaMCS)
     .ent("simbolos", sv.simbolos).num("bler", sv.bler).num("brecha_db", sv.brechaDb).num("latencia_s", sv.latenciaS)
     .ent("dmrs_por_prb", dmrs).fin();

    j.arreglo("enlaces");
    for (const Banda& b : BANDAS) {
        const double scsKHz = 15.0 * (1 << b.mu);
        const double duracionSlot = 1e-3 / (1 << b.mu);
        const double duracionSimbolo = duracionSlot / 14;
        const double fraccionBajada = b.tdd ? (3 * 14 + 10) / 70.0 : 1.0;  // símbolos de bajada en DDDSU (S = 10D:2G:2U)
        const double anchoOcupadoHz = b.nRB * 12 * scsKHz * 1e3;

        // Tasa pico, TS 38.306 §4.1.2
        const double Ts = 1e-3 / (14 * (1 << b.mu));
        const int QmMax = p.tablaMCS == 64 ? 6 : 8;
        const double tasaPico = p.capas * QmMax * (948.0 / 1024) * (b.nRB * 12) / Ts * (1 - b.overhead);
        const double capacidad = p.capas * anchoOcupadoHz * log2(1 + snr);  // Shannon con capas paralelas ideales

        // Adaptación de enlace: el MCS más alto cuya eficiencia espectral cabe en la del canal (modelo)
        int mcs = -1;
        for (int i = 0; i < numMCS; ++i)
            if (tabla[i].Qm * tabla[i].R1024 / 1024 <= eeMax) mcs = i;

        j.objeto().str("nombre", b.nombre).str("rango", b.rango).str("banda", b.banda).num("frecuencia_ghz", b.frecuenciaGHz)
         .ent("mu", b.mu).num("scs_khz", scsKHz).ent("ancho_mhz", b.anchoMHz).ent("n_rb", b.nRB)
         .str("duplex", b.tdd ? "TDD (DDDSU)" : "FDD").num("fraccion_bajada", fraccionBajada)
         .num("duracion_slot_ms", duracionSlot * 1e3).num("tasa_pico", tasaPico).num("capacidad_shannon", capacidad)
         .num("ee_canal", eeMax).booleano("viable", mcs >= 0);
        if (mcs >= 0) {
            const double R = tabla[mcs].R1024 / 1024;
            const int Qm = tabla[mcs].Qm;
            const vector<int> cw = capasPorPalabra(p.capas);
            auto tbsTotal = [&](int simbolos) {
                int64_t t = 0;
                for (int v : cw) t += tbsNR(b.nRB, simbolos, dmrs, 0, R, Qm, v);
                return t;
            };

            // Bits codificados (salida del LDPC tras la adaptación de tasa) de una transmisión con todas las capas
            auto bitsAire = [&](int simbolos) { return (int64_t)min(156, 12 * simbolos - dmrs) * b.nRB * Qm * p.capas; };

            // Transmisiones de un periodo de la trama: (instante en que termina, bits del bloque de transporte, bits en el aire).
            // FDD: periodo de 1 slot. TDD DDDSU: periodo de 5 slots; el slot especial lleva un PDSCH más corto
            // (9 símbolos: 10 de bajada menos 1 de PDCCH) o los mini-slots que quepan en sus 10 símbolos.
            struct Unidad { double fin; int64_t tbs, aire; };
            vector<Unidad> periodo;
            const int porSlot = 14 / sv.simbolos;
            const int slotsBajada = b.tdd ? 3 : 1;
            for (int sl = 0; sl < slotsBajada; ++sl)
                for (int u = 0; u < porSlot; ++u)
                    periodo.push_back({sl * duracionSlot + ((sv.simbolos == 13 ? 1 : 0) + (u + 1) * sv.simbolos) * duracionSimbolo,
                                       tbsTotal(sv.simbolos), bitsAire(sv.simbolos)});
            if (b.tdd) {
                if (sv.simbolos >= 10) periodo.push_back({3 * duracionSlot + 10 * duracionSimbolo, tbsTotal(9), bitsAire(9)});
                else
                    for (int u = 0; u < 10 / sv.simbolos; ++u)
                        periodo.push_back({3 * duracionSlot + (u + 1) * sv.simbolos * duracionSimbolo, tbsTotal(sv.simbolos),
                                           bitsAire(sv.simbolos)});
            }
            const double duracionPeriodo = (b.tdd ? 5 : 1) * duracionSlot;
            int64_t bitsPeriodo = 0;
            for (auto& u : periodo) bitsPeriodo += u.tbs;
            const double tasa = bitsPeriodo * (1 - sv.bler) / duracionPeriodo;

            const int64_t tbs0 = tbsNR(b.nRB, sv.simbolos, dmrs, 0, R, Qm, cw[0]);
            const SegmentacionNR s = segmentarNR(tbs0, R);
            const int64_t nRE = (int64_t)min(156, 12 * sv.simbolos - dmrs) * b.nRB;
            const int64_t G = nRE * Qm * cw[0];  // bits codificados disponibles para la palabra de código

            j.ent("mcs", mcs).ent("qm", Qm).num("tasa_codigo", R).num("ee_mcs", Qm * R)
             .ent("palabras_codigo", cw.size()).ent("tbs", tbsTotal(sv.simbolos)).ent("tbs_palabra", tbs0)
             .ent("transmisiones_por_periodo", periodo.size()).num("duracion_periodo_ms", duracionPeriodo * 1e3)
             .ent("re_por_transmision", nRE).ent("bits_codificados", G)
             .num("tasa_efectiva_codigo", (double)(tbs0 + s.crcTB + s.C * s.crcCB) / G)
             .num("transmisiones_esperadas", 1 / (1 - sv.bler)).num("tasa", tasa)
             .num("uso_capacidad", tasa / (capacidad * fraccionBajada));
            j.objeto("ldpc").ent("grafo_base", s.grafoBase).ent("crc_tb", s.crcTB).ent("bloques", s.C).ent("crc_bloque", s.crcCB)
             .ent("k_prima", s.Kprima).ent("zc", s.Zc).ent("k", s.K).ent("relleno", s.relleno).ent("n", s.N).fin();

            // Envío: se recorren las transmisiones de la trama hasta entregar todos los bits; cada bloque
            // entrega en promedio (1 - BLER) de sus bits (las retransmisiones HARQ ocupan transmisiones)
            auto envio = [&](const char* clave, uint64_t bytes) {
                const double bits = bytes * 8.0;
                double entregado = 0, fin = 0;
                int64_t usadas = 0, aire = 0;
                for (int64_t k = 0; entregado < bits; ++k) {
                    const Unidad& u = periodo[k % periodo.size()];
                    entregado += u.tbs * (1 - sv.bler);
                    aire += u.aire;
                    fin = (double)(k / periodo.size()) * duracionPeriodo + u.fin;
                    usadas++;
                }
                j.objeto(clave).num("bits", bits).ent("transmisiones", usadas).ent("bits_aire", aire)
                 .num("tiempo_aire_s", fin).num("tiempo_total_s", fin + sv.latenciaS).fin();
            };
            envio("original", bytesOriginal);
            envio("comprimido", bytesComprimido);
        }
        j.fin();
    }
    j.fin();

    // Requisitos de la UIT: IMT-2020 (ITU-R M.2410) y valores de IMT-2030 para investigación (ITU-R M.2160)
    struct Objetivo {
        const char* nombre;
        const char* generacion;
        double tasaMin, tasaMax, latMin, latMax;
    };
    const double lat5G = sv.latenciaS;
    const Objetivo objetivos[] = {
        {"Velocidad pico de bajada", "5G (IMT-2020)", 20e9, 20e9, lat5G, lat5G},
        {"Velocidad experimentada por el usuario", "5G (IMT-2020)", 100e6, 100e6, lat5G, lat5G},
        {"Velocidad pico", "6G (IMT-2030)", 50e9, 200e9, 0.1e-3, 1e-3},
        {"Velocidad experimentada por el usuario", "6G (IMT-2030)", 300e6, 500e6, 0.1e-3, 1e-3},
    };
    j.arreglo("objetivos");
    for (const Objetivo& o : objetivos) {
        j.objeto().str("nombre", o.nombre).str("generacion", o.generacion).num("tasa_min", o.tasaMin).num("tasa_max", o.tasaMax)
         .num("latencia_min", o.latMin).num("latencia_max", o.latMax);
        for (auto [clave, bytes] : {pair<const char*, uint64_t>{"original", bytesOriginal}, {"comprimido", bytesComprimido}}) {
            double bits = bytes * 8.0;
            j.objeto(clave).num("tiempo_min_s", bits / o.tasaMax + o.latMin).num("tiempo_max_s", bits / o.tasaMin + o.latMax).fin();
        }
        j.fin();
    }
    j.fin();
    j.fin();
    archivo << "\n";
    cout << "[Enlace] " << sv.nombre << " | SNR " << p.snrDb << " dB | " << p.capas << " capas | " << bytesOriginal << " -> "
         << bytesComprimido << " bytes\n";
    return 0;
}
