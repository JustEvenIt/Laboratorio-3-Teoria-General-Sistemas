#include "../include/Simulacion.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <thread>
#include <vector>
#include "../include/Cifrado.h"
#include "../include/Compresor.h"
#include "../include/ControlError.h"
#include "../include/Enlace.h"
#include "../include/Json.h"
#include "../include/NR.h"

using namespace std;
namespace fs = std::filesystem;
using nr::Complejo;

namespace {

// ======================= Escenarios =======================

struct Escenario {
    const char* clave;
    const char* nombre;
    const char* generacion;
    const char* banda;
    int mu;                 // numerología: SCS = 15·2^mu kHz (TS 38.211 §4.2)
    int anchoMHz, prb;      // ancho de banda y bloques de recursos
    int simbolos;           // símbolos OFDM del PDSCH por transmisión (13 en un slot, 7 en un mini-slot)
    int tablaMCS;           // tabla de modulación y tasa (MCS) de TS 38.214
    double brechaDb;        // margen respecto a Shannon para la adaptación de enlace (modelo)
    double latMin, latMax;  // requisito de latencia del plano de usuario (s)
    double fiabMin, fiabMax;// requisito de confiabilidad de la generación (32 bytes en 1 ms; IMT-2020 o IMT-2030)
    double tasaMin, tasaMax;// velocidad experimentada por el usuario (bit/s)
    int paquetes;           // paquetes de 32 bytes de la prueba de confiabilidad
    int rttUnidades;        // unidades de transmisión entre dos envíos del mismo bloque (supuesto)
    double fraccionBajada;  // TDD DDDSU: 3 slots y 10 de 14 símbolos del especial son de bajada
    double blerObjetivo;    // BLER de la primera transmisión que busca la adaptación de enlace
};

const Escenario ESCENARIOS[] = {
    {"embb", "5G eMBB · banda ancha (3.5 GHz, 100 MHz)", "5G (IMT-2020)", "n78", 1, 100, 273, 13, 2, 1.5,
     4e-3, 4e-3, 1 - 1e-5, 1 - 1e-5, 100e6, 100e6, 0, 4, (3 * 14 + 10) / 70.0, 0.1},
    {"urllc", "5G URLLC · baja latencia y alta confiabilidad (3.5 GHz)", "5G (IMT-2020)", "n78", 1, 100, 273, 7, 3, 4.0,
     1e-3, 1e-3, 1 - 1e-5, 1 - 1e-5, 0, 0, 0, 4, (3 * 14 + 10) / 70.0, 1e-3},
    {"6g", "6G · escenario candidato (400 MHz)", "6G (IMT-2030)", "candidata", 3, 400, 264, 7, 4, 1.5,
     0.1e-3, 1e-3, 1 - 1e-5, 1 - 1e-7, 300e6, 500e6, 0, 4, (3 * 14 + 10) / 70.0, 0.1},
};

const Escenario& escenarioDe(const string& clave) {
    for (const Escenario& e : ESCENARIOS)
        if (clave == e.clave) return e;
    return ESCENARIOS[0];
}

double duracionUnidad(const Escenario& e) {
    const double slot = 1e-3 / (1 << e.mu);
    return e.simbolos >= 13 ? slot : slot * e.simbolos / 14.0;
}

// Bits que caben en una transmisión por capa: elementos de recurso de datos x bits por símbolo
int64_t bitsCodificados(const Escenario& e, int prb, int Qm) {
    return (int64_t)min(156, 12 * e.simbolos - 12) * prb * Qm;
}

int elegirMCS(int tabla, double snr, double brechaDb) {
    const double ee = log2(1 + snr / pow(10.0, brechaDb / 10));
    const auto& t = nr::tablaMCS(tabla);
    int mcs = -1;
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].Qm * t[i].R1024 / 1024 <= ee) mcs = (int)i;
    return mcs;
}

// ======================= Trabajo en paralelo =======================

// Reparte [0, n) entre los núcleos cuando hay trabajo suficiente. Cada hilo toma el siguiente trozo libre (así un
// bloque difícil de decodificar no deja a los demás esperando). Cada trozo escribe en posiciones distintas, así que
// el resultado es el mismo con cualquier cantidad de hilos.
template <class F>
void enParalelo(size_t n, size_t minimo, const F& f) {
    static const size_t nucleos = max(1u, thread::hardware_concurrency());
    minimo = max<size_t>(1, minimo);
    const size_t hilos = min(nucleos, n / minimo);
    if (hilos <= 1) {
        f((size_t)0, n);
        return;
    }
    const size_t trozo = max(minimo, n / (hilos * 4));
    atomic<size_t> siguiente{0};
    auto trabajar = [&] {
        for (size_t a; (a = siguiente.fetch_add(trozo)) < n;) f(a, min(n, a + trozo));
    };
    vector<thread> otros;
    for (size_t h = 1; h < hilos; ++h) otros.emplace_back(trabajar);
    trabajar();
    for (thread& t : otros) t.join();
}

uint64_t mezclar(uint64_t x) {  // splitmix64: convierte un contador en una semilla bien repartida
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// ======================= MIMO =======================

// Con L capas la potencia se reparte por igual entre ellas, así que cada capa llega con SNR/L. Una palabra de código
// lleva hasta 4 capas (TS 38.211 §7.3.1.3); con 8 capas se envían 2 palabras de código (2 bloques) a la vez.
struct MIMO {
    int capas = 1, palabras = 1, capasPalabra = 1;
    double snrCapa = 1;
};

MIMO configurarMIMO(int capas, double snr) {
    MIMO m;
    m.capas = capas;
    m.palabras = capas > 4 ? 2 : 1;
    m.capasPalabra = capas / m.palabras;
    m.snrCapa = snr / capas;
    return m;
}

// ======================= Canal =======================

// Ruido gaussiano blanco aditivo (AWGN). Cada símbolo de una capa tiene potencia media 1 y el ruido N0 = 1/(SNR por capa).
// Cada envío tiene su número de secuencia y el ruido se genera por trozos con semillas propias (en paralelo).
struct CanalAWGN {
    double N0 = 1;
    uint64_t semilla = 0, envios = 0;
    CanalAWGN(uint64_t semilla, double snrCapa) : N0(1 / snrCapa), semilla(semilla) {}
    void transmitir(vector<Complejo>& x) {
        const uint64_t id = mezclar(semilla ^ mezclar(envios++));
        const double sigma = sqrt(N0 / 2);
        const size_t T = 1 << 14, trozos = (x.size() + T - 1) / T;
        enParalelo(trozos, 2, [&](size_t a, size_t b) {
            for (size_t k = a; k < b; ++k) {
                mt19937_64 gen(mezclar(id + k));
                normal_distribution<double> normal(0.0, sigma);
                for (size_t i = k * T; i < min(x.size(), (k + 1) * T); ++i) {
                    x[i].re += (float)normal(gen);
                    x[i].im += (float)normal(gen);
                }
            }
        });
    }
};

// ======================= Estadísticas y registro de bloques =======================

struct Estadisticas {
    int64_t tbs = 0, transmisiones = 0, tbsPrimeraMal = 0, tbsFinalMal = 0;
    int64_t bitsBrutos = 0, erroresBrutos = 0;  // bits codificados de la primera transmisión
    int64_t bitsInfo = 0, erroresInfo = 0;      // bits de información después de HARQ
    int64_t iteraciones = 0, bloquesDecodificados = 0;
    vector<int64_t> erroresCapa, bitsCapa;      // primera transmisión, por capa MIMO
    vector<double> latencias;
    double sumaInfoMutua = 0;
    int64_t bitsInfoMutua = 0;
    vector<Complejo> constelacion;
    explicit Estadisticas(int capas = 1) : erroresCapa(capas, 0), bitsCapa(capas, 0) {}
};

struct EnvioTB { int rv = -1; int64_t errores = 0; int iteraciones = 0; bool ok = false; };

// Lo que le pasa a un bloque de transporte, con números reales (para el paso a paso y la visualización)
struct TrazaTB {
    int64_t indice = 0, A = 0, G = 0, simbolos = 0, bitsErrados = 0;
    int mcs = -1, Qm = 0, palabra = 0, capaInicial = 0, capasPalabra = 1, L = 0;
    double R = 0, latencia = 0;
    string crcHex, infoHex;
    int C = 0, Kp = 0, K = 0, Zc = 0, grafo = 0, N = 0, E = 0, Lcb = 0;  // LDPC
    int n = 0, k = 0;                                                    // código de bloque
    int64_t palabrasBloque = 0;
    vector<int64_t> erroresCapa;
    vector<EnvioTB> envios;
    bool ok = false;
};

double log2UnoMasExp(double x) { return (x > 0 ? x + log1p(exp(-x)) : log1p(exp(x))) / log(2.0); }

string bitsHex(const uint8_t* b, size_t n) {
    static const char* H = "0123456789abcdef";
    string s;
    for (size_t i = 0; i < n; i += 4) {
        int v = 0;
        for (size_t j = 0; j < 4; ++j) v = v * 2 + (i + j < n ? b[i + j] : 0);
        s += H[v];
    }
    return s;
}

string bytesHex(const uint8_t* b, size_t n) {
    static const char* H = "0123456789abcdef";
    string s;
    for (size_t i = 0; i < n; ++i) {
        s += H[b[i] >> 4];
        s += H[b[i] & 15];
    }
    return s;
}

// Modula, reparte entre las capas, pasa por el canal y demodula. Mapeo a capas (TS 38.211 §7.3.1.3): la capa l lleva
// los símbolos l, l + L, l + 2L, ... Las capas llegan separadas, así que cada símbolo sólo recibe el ruido de su capa.
// En la primera transmisión mide errores brutos (por capa), la información mutua por bit del demodulador (BICM)
// y guarda una muestra de la constelación recibida.
void pasarPorCanal(const vector<uint8_t>& g, int Qm, int capasPalabra, int capaInicial, CanalAWGN& canal,
                   vector<float>& llr, Estadisticas& est, bool primera, int64_t& errores, vector<int64_t>* erroresCapa) {
    const size_t ns = g.size() / Qm;
    vector<Complejo> x(ns);
    enParalelo(ns, 1 << 15, [&](size_t a, size_t b) { nr::modular(&g[a * Qm], b - a, Qm, &x[a]); });
    canal.transmitir(x);
    const vector<float> var(ns, (float)canal.N0);
    llr.assign(ns * Qm, 0.0f);
    enParalelo(ns, 1 << 14, [&](size_t a, size_t b) { nr::demodular(&x[a], &var[a], b - a, Qm, &llr[a * Qm]); });
    vector<int64_t> porCapa(capasPalabra, 0);
    for (size_t i = 0; i < ns; ++i) {
        int e = 0;
        for (int q = 0; q < Qm; ++q) e += (llr[i * Qm + q] < 0) != (g[i * Qm + q] != 0);
        porCapa[i % capasPalabra] += e;
    }
    errores = accumulate(porCapa.begin(), porCapa.end(), (int64_t)0);
    if (erroresCapa) *erroresCapa = porCapa;
    if (!primera) return;
    est.erroresBrutos += errores;
    est.bitsBrutos += ns * Qm;
    for (int l = 0; l < capasPalabra; ++l) {
        est.erroresCapa[capaInicial + l] += porCapa[l];
        est.bitsCapa[capaInicial + l] += (int64_t)(ns / capasPalabra) * Qm;
    }
    // I(B; LLR) = 1 - E[log2(1 + e^(-(1-2b)·LLR))], estimada con los bits enviados
    const size_t muestra = min<size_t>(ns * Qm, 200000);
    for (size_t i = 0; i < muestra && est.bitsInfoMutua < 4000000; ++i) {
        const double s = g[i] ? -llr[i] : llr[i];
        est.sumaInfoMutua += 1 - log2UnoMasExp(-s);
        est.bitsInfoMutua++;
    }
    if (est.constelacion.empty()) est.constelacion.assign(x.begin(), x.begin() + min<size_t>(ns, 1500));
}

// ======================= Códigos de canal dentro de HARQ =======================

// Lo único que cambia entre los caminos: cómo se codifica el bloque, qué bits salen en cada envío
// y cómo se combinan los envíos y se decodifica. El lazo de retransmisión (enviarTB) es uno solo.
class CodigoTB {
public:
    virtual ~CodigoTB() = default;
    virtual void codificar(const vector<uint8_t>& b) = 0;            // b: bits del bloque con su CRC
    virtual int bitsEnvio(int t, vector<uint8_t>& g) = 0;           // devuelve la versión de redundancia (-1: no hay)
    virtual int decodificar(const vector<float>& llr, vector<uint8_t>& b, int& bloques) = 0;  // devuelve iteraciones
    virtual void describir(TrazaTB&) const {}
};

const int SECUENCIA_RV[] = {0, 2, 3, 1};

// LDPC de 5G NR (TS 38.212): segmentación con CRC24B, LDPC, buffer circular y redundancia incremental
class TB_LDPC : public CodigoTB {
public:
    TB_LDPC(int64_t A, double R, int64_t G, int Qm, int capas, int maxIter)
        : Qm(Qm), maxIter(maxIter), seg(nr::segmentar(A, R)), cod(nr::LDPC::obtener(seg.grafo, seg.Zc)),
          E(nr::bitsPorBloque(G, seg.C, Qm, capas)) {}

    void codificar(const vector<uint8_t>& b) override {
        const int C = seg.C, Kp = seg.Kprima, K = seg.K, N = seg.N, Zc = seg.Zc, Lcb = seg.crcCB, porBloque = Kp - Lcb;
        d.assign(C, vector<uint8_t>(N));
        // Segmentación (§5.2.2): Kp - Lcb bits del bloque de transporte + CRC24B si C > 1 + relleno hasta K
        enParalelo(C, 1, [&](size_t a, size_t z) {
            vector<uint8_t> c(K), w((size_t)cod.columnas * Zc);
            for (size_t r = a; r < z; ++r) {
                fill(c.begin(), c.end(), 0);
                copy(b.begin() + r * porBloque, b.begin() + (r + 1) * porBloque, c.begin());
                if (Lcb) {
                    auto q = nr::calcularCRC(c.data(), porBloque, nr::TipoCRC::CRC24B);
                    copy(q.begin(), q.end(), c.begin() + porBloque);
                }
                cod.codificar(c.data(), w.data());
                copy(w.begin() + 2 * Zc, w.begin() + 2 * Zc + N, d[r].begin());  // los 2Zc primeros no se envían
                for (int k = Kp; k < K; ++k) d[r][k - 2 * Zc] = nr::NULO;
            }
        });
        buffer.assign(C, vector<float>(N, 0.0f));
        duro.assign(C, vector<uint8_t>(Kp, 0));
        bloqueOk.assign(C, 0);
        inicio.assign(C + 1, 0);
        for (int r = 0; r < C; ++r) inicio[r + 1] = inicio[r] + E[r];
    }

    int bitsEnvio(int t, vector<uint8_t>& g) override {
        const int rv = SECUENCIA_RV[t % 4];
        k0 = nr::inicioVersion(rv, seg.grafo, seg.N, seg.Zc);
        for (int r = 0; r < seg.C; ++r) nr::adaptarTasa(d[r].data(), seg.N, E[r], k0, Qm, &g[inicio[r]]);
        return rv;
    }

    int decodificar(const vector<float>& llr, vector<uint8_t>& b, int& bloques) override {
        const int C = seg.C, Kp = seg.Kprima, N = seg.N, Zc = seg.Zc, Lcb = seg.crcCB, porBloque = Kp - Lcb;
        vector<int> iter(C, 0), usado(C, 0);
        enParalelo(C, 1, [&](size_t a, size_t z) {
            vector<float> Lv((size_t)cod.columnas * Zc);
            for (size_t r = a; r < z; ++r) {
                // Combinación HARQ: los LLR de este envío se suman en su lugar del buffer circular
                nr::recuperarTasa(&llr[inicio[r]], E[r], k0, Qm, d[r].data(), N, buffer[r].data());
                if (bloqueOk[r]) continue;  // ya pasó su CRC24B en un envío anterior
                fill(Lv.begin(), Lv.end(), 0.0f);
                int ultima = 0;
                for (int k = 0; k < N; ++k) {
                    if (d[r][k] == nr::NULO) Lv[2 * Zc + k] = 30.0f;  // relleno conocido: es 0
                    else {
                        Lv[2 * Zc + k] = buffer[r][k];
                        if (buffer[r][k] != 0) ultima = k;
                    }
                }
                const int filas = max(4, (ultima + 2 * Zc) / Zc - cod.kb + 1);
                bool paridad;
                iter[r] = cod.decodificar(Lv, filas, maxIter, paridad);
                usado[r] = 1;
                for (int k = 0; k < Kp; ++k) duro[r][k] = Lv[k] < 0;
                bloqueOk[r] = Lcb && nr::verificarCRC(duro[r].data(), Kp, nr::TipoCRC::CRC24B);
            }
        });
        b.resize((size_t)C * porBloque);
        for (int r = 0; r < C; ++r) copy(duro[r].begin(), duro[r].begin() + porBloque, b.begin() + (size_t)r * porBloque);
        bloques = accumulate(usado.begin(), usado.end(), 0);
        return accumulate(iter.begin(), iter.end(), 0);
    }

    void describir(TrazaTB& t) const override {
        t.C = seg.C; t.Kp = seg.Kprima; t.K = seg.K; t.Zc = seg.Zc; t.grafo = seg.grafo; t.N = seg.N;
        t.E = E.empty() ? 0 : E[0]; t.Lcb = seg.crcCB;
    }

private:
    int Qm, maxIter, k0 = 0;
    nr::Segmentacion seg;
    const nr::LDPC& cod;
    vector<int> E;
    vector<size_t> inicio;
    vector<vector<uint8_t>> d, duro;
    vector<vector<float>> buffer;
    vector<char> bloqueOk;
};

// Código de bloque (n, k) con decodificación por síndrome, o sin código (cod = nullptr). Los envíos repiten la misma
// palabra y se combinan sumando los LLR (combinación de Chase).
class TB_Bloque : public CodigoTB {
public:
    TB_Bloque(const CodigoBloque* cod, int64_t G) : cod(cod), n(cod ? cod->n : 1), k(cod ? cod->k : 1), G(G) {}

    void codificar(const vector<uint8_t>& b) override {
        B = (int64_t)b.size();
        bloques = (B + k - 1) / k;
        g.assign(G, 0);
        enParalelo(bloques, 1 << 14, [&](size_t a, size_t z) {
            for (size_t i = a; i < z; ++i) {
                uint32_t m = 0;
                for (int j = 0; j < k; ++j) m = (m << 1) | ((int64_t)(i * k + j) < B ? b[i * k + j] : 0);
                const uint32_t c = cod ? cod->codificar(m) : m;  // c = m G
                for (int j = 0; j < n; ++j) g[i * n + j] = (c >> (n - 1 - j)) & 1;
            }
        });
        suma.assign(G, 0.0f);
    }

    int bitsEnvio(int, vector<uint8_t>& gs) override {
        gs = g;
        return -1;
    }

    int decodificar(const vector<float>& llr, vector<uint8_t>& b, int& usados) override {
        b.assign((size_t)bloques * k, 0);
        enParalelo(bloques, 1 << 14, [&](size_t a, size_t z) {
            for (size_t i = a; i < z; ++i) {
                uint32_t r = 0;
                for (int j = 0; j < n; ++j) {
                    float& s = suma[i * n + j];
                    s += llr[i * n + j];
                    r = (r << 1) | (s < 0);
                }
                uint32_t m = r;
                if (cod) m = (r ^ cod->lider[cod->sindrome(r)]) & ((1u << k) - 1);  // c^ = r + e^, m^ = últimos k bits
                for (int j = 0; j < k; ++j) b[i * k + j] = (m >> (k - 1 - j)) & 1;
            }
        });
        usados = 0;
        return 0;
    }

    void describir(TrazaTB& t) const override {
        t.n = n;
        t.k = k;
        t.palabrasBloque = bloques;
    }

private:
    const CodigoBloque* cod;
    int n, k;
    int64_t G, B = 0, bloques = 0;
    vector<uint8_t> g;
    vector<float> suma;
};

struct ResultadoTB { bool ok = false; int envios = 0; };

// Un bloque de transporte con su CRC (CRC24A o CRC16) y HARQ: se envía, el receptor combina, decodifica y revisa el CRC;
// si falla responde NACK y se envía otra vez, hasta harqMax envíos. Este lazo es el mismo para los tres caminos.
ResultadoTB enviarTB(CodigoTB& cod, int64_t A, int64_t G, int Qm, int capasPalabra, int capaInicial, const uint8_t* info,
                     uint8_t* salida, CanalAWGN& canal, int harqMax, Estadisticas& est, TrazaTB* tr) {
    const nr::TipoCRC tcrc = A > 3824 ? nr::TipoCRC::CRC24A : nr::TipoCRC::CRC16;
    vector<uint8_t> b(info, info + A);
    const auto p = nr::calcularCRC(b.data(), A, tcrc);
    b.insert(b.end(), p.begin(), p.end());
    cod.codificar(b);
    if (tr) {
        tr->A = A; tr->G = G; tr->Qm = Qm; tr->simbolos = G / Qm; tr->L = (int)p.size();
        tr->crcHex = bitsHex(p.data(), p.size());
        tr->infoHex = bitsHex(info, (size_t)min<int64_t>(A, 64));
        tr->capasPalabra = capasPalabra; tr->capaInicial = capaInicial;
        cod.describir(*tr);
    }
    vector<uint8_t> g(G), estimado;
    vector<float> llr;
    ResultadoTB res;
    for (int t = 0; t < harqMax; ++t) {
        const int rv = cod.bitsEnvio(t, g);
        int64_t errores = 0;
        pasarPorCanal(g, Qm, capasPalabra, capaInicial, canal, llr, est, t == 0, errores,
                      tr && t == 0 ? &tr->erroresCapa : nullptr);
        est.transmisiones++;
        res.envios = t + 1;
        int bloques = 0;
        const int iter = cod.decodificar(llr, estimado, bloques);
        est.iteraciones += iter;
        est.bloquesDecodificados += bloques;
        res.ok = nr::verificarCRC(estimado.data(), b.size(), tcrc);
        if (tr) tr->envios.push_back({rv, errores, iter, res.ok});
        if (t == 0 && !res.ok) est.tbsPrimeraMal++;
        if (res.ok) break;  // ACK
    }
    copy(estimado.begin(), estimado.begin() + A, salida);
    if (tr) tr->ok = res.ok;
    return res;
}

// Información por bloque para el código de bloque y sin código: palabras de n bits que caben en G, menos el CRC
int64_t infoBloque(int64_t G, int n, int k) {
    const int64_t bloques = G / n;
    int64_t A = (bloques * k - 24) / 8 * 8;
    if (A <= 3824) A = min<int64_t>(3824, (bloques * k - 16) / 8 * 8);
    return max<int64_t>(A, 8);
}

// ======================= Caminos =======================

struct Parametros { int mcs = -1, Qm = 2; double R = 1; int64_t A = 0, G = 0; };

// Un camino es una forma de transmitir (código + capas MIMO) con su canal, su adaptación de enlace y sus medidas
struct Camino {
    string clave, nombre, codigo;
    int tipo = 0;                       // 0: LDPC 5G, 1: código de bloque, 2: sin código
    const CodigoBloque* bloque = nullptr;
    MIMO mimo;
    int QmFijo = 2;                     // modulación del código de bloque y de sin código
    double offset = 0;                  // ajuste de la adaptación de enlace de lazo externo (OLLA), en dB
    unique_ptr<CanalAWGN> canal;
    Estadisticas est;
    vector<int> mcsUsados;
    int64_t bitsBien = 0, bitsEnPlazo = 0;
    double tiempoAire = 0;              // s de transmisión de bajada usados
    nr::Segmentacion seg;
    double cpuMs = 0;
    vector<TrazaTB> traza;
};

Camino crearCamino(int tipo, const CodigoBloque* bloque, int capas, double snr, const Escenario& esc, uint64_t semilla) {
    Camino c;
    c.tipo = tipo;
    c.bloque = tipo == 1 ? bloque : nullptr;
    c.mimo = configurarMIMO(capas, snr);
    c.QmFijo = nr::tablaMCS(esc.tablaMCS)[max(0, elegirMCS(esc.tablaMCS, c.mimo.snrCapa, esc.brechaDb))].Qm;
    c.est = Estadisticas(capas);
    c.canal = make_unique<CanalAWGN>(semilla, c.mimo.snrCapa);
    if (tipo == 0) { c.clave = "ldpc"; c.nombre = "LDPC de 5G NR"; c.codigo = "LDPC (TS 38.212)"; }
    else if (tipo == 1) { c.clave = "bloque"; c.nombre = bloque->nombre; c.codigo = bloque->nombre + " · decodificación por síndrome"; }
    else { c.clave = "sin"; c.nombre = "Sin código"; c.codigo = "Sólo CRC"; }
    return c;
}

// Tamaño y modulación del siguiente bloque. LDPC: MCS según la SNR por capa menos el ajuste OLLA (TBS de TS 38.214
// con las capas de la palabra de código). Código de bloque y sin código: modulación fija, tasa del código.
Parametros parametrosDe(const Camino& c, const Escenario& esc) {
    Parametros p;
    const auto& tabla = nr::tablaMCS(esc.tablaMCS);
    const int v = c.mimo.capasPalabra;
    if (c.tipo == 0) {
        p.mcs = max(0, elegirMCS(esc.tablaMCS, c.mimo.snrCapa / pow(10.0, c.offset / 10), esc.brechaDb));
        p.Qm = tabla[p.mcs].Qm;
        p.R = tabla[p.mcs].R1024 / 1024;
        p.G = bitsCodificados(esc, esc.prb, p.Qm) * v;
        p.A = tbsNR(esc.prb, esc.simbolos, 12, 0, p.R, p.Qm, v);
    } else {
        p.Qm = c.QmFijo;
        p.G = bitsCodificados(esc, esc.prb, p.Qm) * v;
        p.A = infoBloque(p.G, c.bloque ? c.bloque->n : 1, c.bloque ? c.bloque->k : 1);
        p.R = (double)p.A / p.G;
    }
    return p;
}

// Envía un bloque por el camino y actualiza la adaptación de enlace (OLLA: NACK baja 1 dB, ACK sube 1 dB·b/(1-b))
ResultadoTB enviarPorCamino(Camino& c, const Escenario& esc, const Parametros& p, const vector<uint8_t>& info,
                            vector<uint8_t>& salida, int harqMax, int iteraciones, TrazaTB* tr) {
    const int palabra = (int)(c.est.tbs % c.mimo.palabras);
    unique_ptr<CodigoTB> cod;
    if (c.tipo == 0) cod = make_unique<TB_LDPC>(p.A, p.R, p.G, p.Qm, c.mimo.capasPalabra, iteraciones);
    else cod = make_unique<TB_Bloque>(c.bloque, p.G);
    if (tr) { tr->indice = c.est.tbs; tr->mcs = p.mcs; tr->R = p.R; tr->palabra = palabra; }
    salida.assign(p.A, 0);
    const ResultadoTB r = enviarTB(*cod, p.A, p.G, p.Qm, c.mimo.capasPalabra, palabra * c.mimo.capasPalabra, info.data(),
                                   salida.data(), *c.canal, harqMax, c.est, tr);
    if (c.tipo == 0) {
        const double bajada = 1.0, subida = bajada * esc.blerObjetivo / (1 - esc.blerObjetivo);
        c.offset += (r.ok && r.envios == 1) ? -subida : bajada;
        c.mcsUsados.push_back(p.mcs);
    }
    const double Tu = duracionUnidad(esc), lat = Tu + (r.envios - 1) * esc.rttUnidades * Tu;
    c.est.tbs++;
    if (!r.ok) c.est.tbsFinalMal++;
    c.est.latencias.push_back(lat);
    c.tiempoAire += r.envios * Tu / c.mimo.palabras / esc.fraccionBajada;
    if (r.ok && lat <= esc.latMax + 1e-12) c.bitsEnPlazo += p.A;
    if (tr) tr->latencia = lat;
    return r;
}

// Bits [pos, pos + n) de un arreglo de bytes (el primer bit es el más significativo); fuera del arreglo valen 0
void leerBits(const vector<uint8_t>& bytes, uint64_t pos, size_t n, uint8_t* bits) {
    const uint64_t total = (uint64_t)bytes.size() * 8;
    for (size_t i = 0; i < n; ++i) {
        const uint64_t q = pos + i;
        bits[i] = q < total ? (bytes[q >> 3] >> (7 - (q & 7))) & 1 : 0;
    }
}

void escribirBits(vector<uint8_t>& bytes, uint64_t pos, const uint8_t* bits, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const uint64_t q = pos + i;
        const uint8_t m = (uint8_t)(0x80 >> (q & 7));
        if (bits[i]) bytes[q >> 3] |= m;
        else bytes[q >> 3] &= (uint8_t)~m;
    }
}

// ======================= Agente de transmisión =======================

// El agente prueba opciones (código x capas) bloque por bloque y aprende cuál entrega más bits correctos por segundo
// dentro del plazo de latencia del escenario. Usa la regla ε-greedy: con probabilidad ε prueba una opción al azar y si
// no usa la mejor que conoce; ε baja a medida que aprende. Después se miden todas las opciones con los mismos bloques
// para comprobar la decisión.
struct Brazo {
    int tipo = 0, capas = 1;
    string clave, nombre;
    int pruebas = 0;
    double suma = 0;                    // recompensas del agente (Mbit/s)
    // Medida de comprobación (todas las opciones con los mismos bloques)
    int64_t tbs = 0, primeraMal = 0, finalMal = 0, envios = 0, bits = 0, bitsPlazo = 0;
    double aire = 0, latSuma = 0;
    double media() const { return pruebas ? suma / pruebas : 0; }
    double goodput() const { return aire > 0 ? bitsPlazo / aire : 0; }
};

struct PasoAgente { int brazo; double recompensa, epsilon; bool explora; };

struct ResultadoAgente {
    bool hecho = false;
    vector<Brazo> brazos;
    vector<PasoAgente> historia;
    int elegido = -1, mejor = -1, bloquesMedida = 0;
    double ms = 0;
};

ResultadoAgente decidirTransmision(const Escenario& esc, const OpcionesSimulacion& op, double snr, const CodigoBloque* bloque,
                                   const vector<uint8_t>& datos, const function<void(int, int)>& avance) {
    ResultadoAgente ag;
    auto t0 = chrono::steady_clock::now();
    ag.hecho = true;
    for (int tipo = 0; tipo < 3; ++tipo) {
        if (tipo == 1 && !bloque) continue;
        for (int capas : {1, 2, 4, 8}) {
            Brazo b;
            b.tipo = tipo;
            b.capas = capas;
            b.clave = string(tipo == 0 ? "ldpc" : tipo == 1 ? "bloque" : "sin") + "_" + to_string(capas);
            b.nombre = (tipo == 0 ? string("LDPC 5G") : tipo == 1 ? bloque->nombre : string("Sin código")) + " · " +
                       to_string(capas) + (capas == 1 ? " capa" : " capas");
            ag.brazos.push_back(b);
        }
    }
    const int nb = (int)ag.brazos.size();
    const int T = 5 * nb, M = 8;  // pruebas del agente y bloques por opción en la comprobación
    ag.bloquesMedida = M;
    const double Tu = duracionUnidad(esc);
    uint64_t posDatos = 0;
    auto probar = [&](Camino& c, Brazo* medida) {
        const Parametros p = parametrosDe(c, esc);
        vector<uint8_t> info(p.A), salida;
        if (datos.empty()) fill(info.begin(), info.end(), 0);
        else {
            posDatos %= (uint64_t)datos.size() * 8;
            leerBits(datos, posDatos, p.A, info.data());
            posDatos += p.A;
        }
        const int64_t primerasAntes = c.est.tbsPrimeraMal;
        const ResultadoTB r = enviarPorCamino(c, esc, p, info, salida, op.harqMax, op.iteraciones, nullptr);
        const double lat = c.est.latencias.back(), aire = r.envios * Tu / c.mimo.palabras / esc.fraccionBajada;
        const bool aTiempo = r.ok && lat <= esc.latMax + 1e-12;
        if (medida) {
            medida->tbs++;
            medida->primeraMal += c.est.tbsPrimeraMal - primerasAntes;
            medida->finalMal += !r.ok;
            medida->envios += r.envios;
            medida->bits += r.ok ? p.A : 0;
            medida->bitsPlazo += aTiempo ? p.A : 0;
            medida->aire += aire;
            medida->latSuma += lat;
        }
        return aTiempo ? p.A / aire / 1e6 : 0.0;  // recompensa: Mbit/s entregados a tiempo
    };

    // Aprendizaje
    vector<Camino> enlaces;
    for (int i = 0; i < nb; ++i)
        enlaces.push_back(crearCamino(ag.brazos[i].tipo, bloque, ag.brazos[i].capas, snr, esc, op.semilla + 101 + i));
    mt19937_64 azar(op.semilla + 5);
    uniform_real_distribution<double> u01(0, 1);
    auto mejorConocido = [&] {
        int m = 0;
        for (int i = 1; i < nb; ++i)
            if (ag.brazos[i].media() > ag.brazos[m].media()) m = i;
        return m;
    };
    for (int t = 0; t < T; ++t) {
        int a;
        bool explora;
        double eps = 1;
        if (t < nb) { a = t; explora = true; }  // primera vuelta: cada opción una vez
        else {
            eps = max(0.05, 0.5 * (1 - (double)(t - nb) / (T - nb)));
            explora = u01(azar) < eps;
            a = explora ? (int)(azar() % nb) : mejorConocido();
        }
        const double rec = probar(enlaces[a], nullptr);
        ag.brazos[a].pruebas++;
        ag.brazos[a].suma += rec;
        ag.historia.push_back({a, rec, eps, explora});
        avance(t + 1, T + nb * M);
    }
    ag.elegido = mejorConocido();

    // Comprobación: cada opción con los mismos datos, el mismo canal y M bloques
    for (int i = 0; i < nb; ++i) {
        Camino c = crearCamino(ag.brazos[i].tipo, bloque, ag.brazos[i].capas, snr, esc, op.semilla + 777);
        posDatos = 0;
        for (int m = 0; m < M; ++m) {
            probar(c, &ag.brazos[i]);
            avance(T + i * M + m + 1, T + nb * M);
        }
    }
    ag.mejor = 0;
    for (int i = 1; i < nb; ++i)
        if (ag.brazos[i].goodput() > ag.brazos[ag.mejor].goodput()) ag.mejor = i;
    ag.ms = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    cout << "[Agente] eligio " << ag.brazos[ag.elegido].nombre << " | la medida de todas las opciones da "
         << ag.brazos[ag.mejor].nombre << "\n";
    return ag;
}

// ======================= Medidas =======================

// Compara el archivo original (leído por partes desde el disco) con el recuperado: I(X;Y) por byte, posición por
// posición (un byte que falta es un símbolo más), y si son idénticos
struct ComparacionBytes { double HX = 0, IXY = 0; bool identico = false; uint64_t bytes = 0; };

ComparacionBytes compararConOriginal(const string& rutaOriginal, const vector<unsigned char>& y) {
    ComparacionBytes r;
    ifstream in(rutaOriginal, ios::binary);
    vector<uint64_t> conj(256 * 257, 0), px(256, 0), py(257, 0);
    vector<char> buf(1 << 24);
    uint64_t i = 0;
    bool iguales = true;
    while (in) {
        in.read(buf.data(), buf.size());
        const size_t leidos = (size_t)in.gcount();
        for (size_t j = 0; j < leidos; ++j, ++i) {
            const int a = (uint8_t)buf[j], c = i < y.size() ? y[i] : 256;
            iguales &= a == c;
            conj[a * 257 + c]++;
            px[a]++;
            py[c]++;
        }
    }
    r.bytes = i;
    if (i == 0) return r;
    auto H = [&](const vector<uint64_t>& v) {
        double h = 0;
        for (uint64_t c : v)
            if (c) h -= (double)c / i * log2((double)c / i);
        return h;
    };
    r.HX = H(px);
    r.IXY = max(0.0, r.HX + H(py) - H(conj));
    r.identico = iguales && y.size() == i;
    return r;
}

// Cota superior al 95 % de la probabilidad de falla (Clopper-Pearson) con f fallas en N intentos
double cotaSuperior(int64_t f, int64_t N) {
    if (N <= 0) return 1;
    if (f >= N) return 1;
    auto cdf = [&](double p) {
        double s = 0;
        for (int64_t i = 0; i <= f; ++i)
            s += exp(lgamma(N + 1.0) - lgamma(i + 1.0) - lgamma(N - i + 1.0) + i * log(p) + (N - i) * log1p(-p));
        return s;
    };
    double lo = 0, hi = 1;
    for (int it = 0; it < 80; ++it) {
        double mid = (lo + hi) / 2;
        (cdf(mid) > 0.05 ? lo : hi) = mid;
    }
    return hi;
}

double percentil(vector<double> v, double q) {
    if (v.empty()) return 0;
    sort(v.begin(), v.end());
    return v[min(v.size() - 1, (size_t)floor(q * (v.size() - 1) + 0.5))];
}

// ======================= Prueba de confiabilidad =======================

// Mensajes de 32 bytes con el plazo del requisito: IMT-2020 pide 99.999 % en 1 ms; IMT-2030, 99.999 % a 99.99999 %
// en 0.1 a 1 ms. Cada mensaje lleva su CRC y el LDPC, con la modulación y tasa más robustas que permite la SNR, y sólo
// se cuentan los envíos que caben en 1 ms.
struct Confiabilidad {
    bool hecha = false;
    int64_t paquetes = 0, fallas = 0, enPlazoMin = 0;
    int mcs = 0, Qm = 0, prb = 0, maxEnvios = 0;
    double R = 0, cota = 1, plazo = 1e-3, plazoMin = 1e-3, cpuMs = 0;
    int64_t tbs = 0;
};

Confiabilidad probarConfiabilidad(const Escenario& esc, double snrDb, int harqMax, int iteraciones, uint64_t semilla,
                                  int64_t paquetes) {
    Confiabilidad conf;
    auto t0 = chrono::steady_clock::now();
    const double snr = pow(10.0, snrDb / 10);
    const double Tu = duracionUnidad(esc), rtt = esc.rttUnidades * Tu;
    conf.hecha = true;
    conf.paquetes = paquetes;
    conf.plazo = 1e-3;
    conf.plazoMin = min(esc.latMin, 1e-3);
    conf.mcs = max(0, elegirMCS(3, snr, 4.0));
    const auto& t3 = nr::tablaMCS(3);
    conf.Qm = t3[conf.mcs].Qm;
    conf.R = t3[conf.mcs].R1024 / 1024;
    for (conf.prb = 1; conf.prb < esc.prb; ++conf.prb)
        if (tbsNR(conf.prb, esc.simbolos, 12, 0, conf.R, conf.Qm, 1) >= 256) break;
    conf.tbs = tbsNR(conf.prb, esc.simbolos, 12, 0, conf.R, conf.Qm, 1);
    const int64_t Gp = bitsCodificados(esc, conf.prb, conf.Qm);
    conf.maxEnvios = Tu > conf.plazo ? 0 : min(harqMax, 1 + (int)floor((conf.plazo - Tu) / rtt + 1e-9));
    CanalAWGN canal(semilla + 7, snr);
    mt19937_64 gen(semilla + 11);
    vector<uint8_t> info(conf.tbs, 0), salida(conf.tbs);
    Estadisticas est;
    for (int64_t i = 0; i < paquetes && conf.maxEnvios > 0; ++i) {
        for (int j = 0; j < 256; ++j) info[j] = gen() & 1;
        TB_LDPC cod(conf.tbs, conf.R, Gp, conf.Qm, 1, iteraciones);
        const ResultadoTB r = enviarTB(cod, conf.tbs, Gp, conf.Qm, 1, 0, info.data(), salida.data(), canal, conf.maxEnvios,
                                       est, nullptr);
        if (!r.ok) conf.fallas++;
        else if (Tu + (r.envios - 1) * rtt <= conf.plazoMin + 1e-12) conf.enPlazoMin++;
    }
    if (conf.maxEnvios == 0) conf.fallas = paquetes;
    conf.cota = cotaSuperior(conf.fallas, paquetes);
    conf.cpuMs = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
    cout << "[Confiabilidad] " << paquetes << " mensajes de 32 bytes, " << conf.fallas << " fallas\n";
    return conf;
}

void escribirConfiabilidad(Json& j, const Confiabilidad& conf, const Escenario& esc) {
    j.objeto("confiabilidad").booleano("hecha", conf.hecha);
    if (conf.hecha) {
        j.ent("paquetes", conf.paquetes).ent("bytes", 32).ent("fallas", conf.fallas)
         .num("exito", 1.0 - (double)conf.fallas / conf.paquetes).num("cota_falla_95", conf.cota)
         .num("confiabilidad_garantizada", 1 - conf.cota).ent("en_plazo_min", conf.enPlazoMin)
         .ent("mcs", conf.mcs).ent("qm", conf.Qm).num("tasa", conf.R).ent("prb", conf.prb).ent("tbs", conf.tbs)
         .ent("max_envios", conf.maxEnvios).num("plazo_s", conf.plazo).num("plazo_min_s", conf.plazoMin)
         .num("requisito_min", esc.fiabMin).num("requisito_max", esc.fiabMax)
         .num("paquetes_necesarios", 3.0 / (1 - esc.fiabMin)).num("cpu_ms", conf.cpuMs);
    }
    j.fin();
}

void escribirTraza(Json& j, const TrazaTB& t) {
    j.objeto().ent("tb", t.indice).ent("bits", t.A).ent("bits_codificados", t.G).ent("simbolos", t.simbolos)
     .ent("mcs", t.mcs).ent("qm", t.Qm).num("tasa", t.R).ent("palabra", t.palabra).ent("capa_inicial", t.capaInicial)
     .ent("capas", t.capasPalabra).ent("crc_bits", t.L).str("crc_hex", t.crcHex).str("info_hex", t.infoHex)
     .num("latencia", t.latencia).booleano("ok", t.ok).ent("bits_errados", t.bitsErrados);
    if (t.C) j.ent("bloques_codigo", t.C).ent("k_prima", t.Kp).ent("k", t.K).ent("zc", t.Zc).ent("grafo", t.grafo)
              .ent("n_buffer", t.N).ent("e", t.E).ent("crc_bloque", t.Lcb);
    if (t.n) j.ent("n", t.n).ent("k_bloque", t.k).ent("palabras_bloque", t.palabrasBloque);
    j.arreglo("errores_capa");
    for (int64_t e : t.erroresCapa) j.ent(nullptr, e);
    j.fin().arreglo("envios");
    for (const EnvioTB& e : t.envios)
        j.objeto().ent("rv", e.rv).ent("errores", e.errores).ent("iteraciones", e.iteraciones).booleano("ok", e.ok).fin();
    j.fin().fin();
}

bool hexAClave(const string& h, uint8_t clave[16]) {
    if (h.size() != 32) return false;
    for (int i = 0; i < 16; ++i) {
        unsigned v;
        if (sscanf(h.c_str() + 2 * i, "%2x", &v) != 1 || !isxdigit((unsigned char)h[2 * i]) ||
            !isxdigit((unsigned char)h[2 * i + 1]))
            return false;
        clave[i] = (uint8_t)v;
    }
    return true;
}

// Vector de prueba de FIPS-197 (apéndice C.1): confirma que el AES implementado es el estándar
bool aesCumpleFIPS197() {
    uint8_t k[16], p[16], c[16];
    for (int i = 0; i < 16; ++i) { k[i] = (uint8_t)i; p[i] = (uint8_t)(0x11 * i); }
    const uint8_t esperado[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                  0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    AES128(k).cifrarBloque(p, c);
    return memcmp(c, esperado, 16) == 0;
}

// Resultados de un camino que se escriben al final
struct ResultadoCamino {
    double IXYbit = 0, IXYbyte = 0, HXbyte = 0;
    uint64_t diferentes = 0;
    bool descifradoOk = false, descomprimido = false, identico = false;
    string archivo;
};

}  // namespace

// ======================= Simulación del archivo =======================

int ejecutarSimulacion(const string& rutaOriginal, const string& rutaComprimido, const OpcionesSimulacion& op,
                       const string& rutaJson) {
    const auto inicio = chrono::steady_clock::now();
    const Escenario& esc = escenarioDe(op.escenario);
    const double snr = pow(10.0, op.snrDb / 10);
    const double Tu = duracionUnidad(esc), rtt = esc.rttUnidades * Tu;
    const double scs = 15e3 * (1 << esc.mu), anchoOcupado = esc.prb * 12 * scs;

    // Avance para la aplicación: fase, trabajo hecho y total
    auto ultimoAvance = chrono::steady_clock::now() - chrono::seconds(10);
    auto avance = [&](const string& fase, double hecho, double total, bool forzar) {
        if (op.progreso.empty()) return;
        const auto ahora = chrono::steady_clock::now();
        if (!forzar && ahora - ultimoAvance < chrono::milliseconds(400)) return;
        ultimoAvance = ahora;
        ofstream o(op.progreso + ".tmp");
        Json j(o);
        j.objeto().str("fase", fase).num("hecho", hecho).num("total", total)
         .num("segundos", chrono::duration<double>(ahora - inicio).count()).fin();
        o << "\n";
        o.close();
        error_code ec;
        fs::rename(op.progreso + ".tmp", op.progreso, ec);
    };

    vector<uint8_t> comprimido;
    {
        ifstream fc(rutaComprimido, ios::binary | ios::ate);
        if (!fc.is_open()) {
            cerr << "No se pudo leer el archivo comprimido " << rutaComprimido << "\n";
            return 1;
        }
        comprimido.resize((size_t)fc.tellg());
        fc.seekg(0);
        fc.read((char*)comprimido.data(), comprimido.size());
    }
    if (comprimido.empty()) {
        cerr << "El archivo comprimido está vacío\n";
        return 1;
    }
    error_code ec;
    const uint64_t bytesOriginal = fs::file_size(rutaOriginal, ec);
    if (ec) {
        cerr << "No se pudo leer el archivo original " << rutaOriginal << "\n";
        return 1;
    }
    const bool completo = op.maxBytes == 0 || op.maxBytes >= comprimido.size();
    const size_t bytesSim = completo ? comprimido.size() : op.maxBytes;
    const uint64_t totalBits = (uint64_t)bytesSim * 8;

    // ---------- Cifrado AES-128-CTR (como 128-NEA2 de 5G) ----------
    uint8_t clave[16], contador[16];
    if (!hexAClave(op.clave, clave)) {
        cerr << "La clave AES debe tener 32 cifras hexadecimales\n";
        return 2;
    }
    contadorNEA2(0, 0, 1, contador);  // COUNT = 0, BEARER = 0, DIRECTION = 1 (bajada)
    avance("Cifrando con AES-128", 0, 1, true);
    vector<uint8_t> datos(comprimido.begin(), comprimido.begin() + bytesSim);
    if (op.cifrar) cifrarCTR(clave, contador, datos.data(), datos.size());
    const size_t nMuestra = min<size_t>(32, bytesSim);
    vector<uint8_t> flujo(nMuestra);
    for (size_t i = 0; i < nMuestra; ++i) flujo[i] = comprimido[i] ^ datos[i];

    unique_ptr<CodigoBloque> bloque;
    if (op.fec != "ninguno") {
        bloque = make_unique<CodigoBloque>();
        string err;
        if (!CodigoBloque::crear(op.fec, *bloque, err)) {
            cerr << err << "\n";
            return 2;
        }
    }

    // ---------- Agente: elige código y capas ----------
    ResultadoAgente ag;
    int capas = op.capas;
    if (op.capas == 0) {
        ag = decidirTransmision(esc, op, snr, bloque.get(), datos,
                                [&](int h, int t) { avance("El agente prueba las opciones", h, t, false); });
        capas = ag.brazos[ag.elegido].capas;
    }
    const MIMO mimo = configurarMIMO(capas, snr);

    // Modulación y tasa iniciales según la SNR por capa (tabla MCS del escenario)
    const auto& tabla = nr::tablaMCS(esc.tablaMCS);
    const int mcsAuto = elegirMCS(esc.tablaMCS, mimo.snrCapa, esc.brechaDb);
    const int mcs = max(0, mcsAuto);
    const int Qm = tabla[mcs].Qm;
    const double R = tabla[mcs].R1024 / 1024;
    const int64_t G = bitsCodificados(esc, esc.prb, Qm) * mimo.capasPalabra;

    vector<Camino> caminos;
    caminos.push_back(crearCamino(0, nullptr, capas, snr, esc, op.semilla));
    if (bloque) caminos.push_back(crearCamino(1, bloque.get(), capas, snr, esc, op.semilla));
    caminos.push_back(crearCamino(2, nullptr, capas, snr, esc, op.semilla));

    fs::create_directories(op.salida);
    const string ext = fs::path(rutaOriginal).extension().string();
    vector<ResultadoCamino> resultados(caminos.size());
    ComparacionBytes sinArchivo;
    if (!completo) sinArchivo = compararConOriginal(rutaOriginal, {});  // sólo H(X) del original
    const vector<uint8_t> prefijo = completo ? vector<uint8_t>() : vector<uint8_t>(comprimido.begin(), comprimido.begin() + bytesSim);
    const vector<uint8_t>& enviado = completo ? comprimido : prefijo;

    for (size_t ci = 0; ci < caminos.size(); ++ci) {
        Camino& c = caminos[ci];
        const string fase = "Transmitiendo con " + c.nombre + " (" + to_string(ci + 1) + " de " + to_string(caminos.size()) + ")";
        auto t0 = chrono::steady_clock::now();
        vector<uint8_t> recibido(bytesSim, 0), info, salida;
        for (uint64_t pos = 0; pos < totalBits;) {
            const Parametros p = parametrosDe(c, esc);
            const int64_t trozo = (int64_t)min<uint64_t>(p.A, totalBits - pos);
            info.assign(p.A, 0);
            leerBits(datos, pos, trozo, info.data());
            TrazaTB* tr = (int)c.traza.size() < op.bloquesTraza ? &c.traza.emplace_back() : nullptr;
            const ResultadoTB r = enviarPorCamino(c, esc, p, info, salida, op.harqMax, op.iteraciones, tr);
            if (c.est.tbs == 1 && c.tipo == 0) c.seg = nr::segmentar(p.A, p.R);
            int64_t errados = 0;
            for (int64_t i = 0; i < trozo; ++i) errados += salida[i] != info[i];
            c.est.erroresInfo += errados;
            c.est.bitsInfo += trozo;
            if (r.ok) c.bitsBien += trozo;
            if (tr) tr->bitsErrados = errados;
            escribirBits(recibido, pos, salida.data(), trozo);
            pos += trozo;
            avance(fase, (double)ci * totalBits + pos, (double)caminos.size() * totalBits, false);
        }
        c.cpuMs = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        cout << "[Simulacion] " << c.nombre << " con " << capas << " capas: " << c.est.tbs << " bloques de transporte, "
             << c.est.transmisiones << " envios, " << c.est.tbsFinalMal << " con error al final, " << c.est.erroresInfo
             << " bits errados\n";

        // ---------- Receptor: descifrar, comparar y descomprimir ----------
        ResultadoCamino& res = resultados[ci];
        avance("Descifrando y descomprimiendo lo recibido por " + c.nombre, (double)(ci + 1) * totalBits,
               (double)caminos.size() * totalBits, true);
        if (op.cifrar) cifrarCTR(clave, contador, recibido.data(), recibido.size());  // CTR: descifrar = cifrar
        res.IXYbit = ControlError::informacionMutuaBits(enviado, recibido, totalBits, res.diferentes);
        res.descifradoOk = res.diferentes == 0;
        if (completo) {
            vector<unsigned char> rec;
            string err;
            try {
                Compresor compresor;
                res.descomprimido = compresor.deserializar(recibido, rec, err, bytesOriginal);
            } catch (...) {
                res.descomprimido = false;
            }
            if (!res.descomprimido) rec.clear();
            vector<uint8_t>().swap(recibido);
            const ComparacionBytes cmp = compararConOriginal(rutaOriginal, rec);
            res.IXYbyte = cmp.IXY;
            res.HXbyte = cmp.HX;
            res.identico = res.descomprimido && cmp.identico;
            if (res.descomprimido) {
                res.archivo = (fs::path(op.salida) / ("sim_" + c.clave + ext)).string();
                ofstream o(res.archivo, ios::binary);
                o.write((const char*)rec.data(), rec.size());
            }
        } else {
            res.HXbyte = sinArchivo.HX;
        }
    }

    Confiabilidad conf;
    const int paquetes = op.paquetes >= 0 ? op.paquetes : esc.paquetes;
    if (paquetes > 0) {
        avance("Prueba de confiabilidad", 0, 1, true);
        conf = probarConfiabilidad(esc, op.snrDb, op.harqMax, op.iteraciones, op.semilla, paquetes);
    }

    // ---------- Resultados ----------
    ofstream archivoJson(rutaJson);
    Json j(archivoJson);
    j.objeto().str("modo", "simulacion");
    j.objeto("escenario")
     .str("clave", esc.clave).str("nombre", esc.nombre).str("generacion", esc.generacion).str("banda", esc.banda)
     .ent("mu", esc.mu).num("scs_khz", scs / 1e3).ent("ancho_mhz", esc.anchoMHz).ent("prb", esc.prb)
     .num("ancho_ocupado_hz", anchoOcupado).ent("simbolos", esc.simbolos).num("unidad_s", Tu).num("rtt_s", rtt)
     .ent("rtt_unidades", esc.rttUnidades).num("fraccion_bajada", esc.fraccionBajada)
     .ent("tabla_mcs", esc.tablaMCS).num("brecha_db", esc.brechaDb)
     .num("lat_min", esc.latMin).num("lat_max", esc.latMax).num("fiab_min", esc.fiabMin).num("fiab_max", esc.fiabMax)
     .num("tasa_min", esc.tasaMin).num("tasa_max", esc.tasaMax).fin();
    j.objeto("canal").num("snr_db", op.snrDb).ent("harq_max", op.harqMax).ent("semilla", (int64_t)op.semilla)
     .num("capacidad_shannon", log2(1 + snr)).fin();
    j.objeto("mimo").ent("capas", capas).ent("palabras_codigo", mimo.palabras).ent("capas_por_palabra", mimo.capasPalabra)
     .num("snr_capa_db", 10 * log10(mimo.snrCapa)).booleano("eligio_agente", op.capas == 0)
     .num("capacidad", capas * log2(1 + mimo.snrCapa)).arreglo("capacidad_por_capas");
    for (int l : {1, 2, 4, 8}) j.objeto().ent("capas", l).num("bits_por_uso", l * log2(1 + snr / l)).fin();
    j.fin().fin();
    j.objeto("mcs").ent("indice", mcs).booleano("viable", mcsAuto >= 0)
     .num("bler_objetivo", esc.blerObjetivo)
     .ent("qm", Qm).num("tasa", R).num("eficiencia", Qm * R).ent("bits_codificados", G).fin();
    j.objeto("archivo").ent("bytes_original", (int64_t)bytesOriginal).ent("bytes_comprimido", (int64_t)comprimido.size())
     .ent("bytes_simulados", (int64_t)bytesSim).booleano("completo", completo).fin();
    j.objeto("cifrado").booleano("activo", op.cifrar).str("algoritmo", "AES-128 en modo contador (CTR), como 128-NEA2 de 5G")
     .str("clave_hex", op.clave).str("contador_hex", bytesHex(contador, 16)).booleano("vector_fips197_ok", aesCumpleFIPS197())
     .str("claro_hex", bytesHex(comprimido.data(), nMuestra)).str("flujo_hex", bytesHex(flujo.data(), nMuestra))
     .str("cifrado_hex", bytesHex(datos.data(), nMuestra)).fin();

    const Camino& base = caminos[0];
    const double infoBit = base.est.bitsInfoMutua ? base.est.sumaInfoMutua / base.est.bitsInfoMutua : 0;
    j.objeto("bicm").num("info_mutua_bit", infoBit).num("info_mutua_simbolo", infoBit * Qm)
     .num("tasa_codigo_simbolo", R * Qm).fin();
    j.arreglo("constelacion");
    for (const Complejo& s : base.est.constelacion) j.arreglo().num(nullptr, s.re).num(nullptr, s.im).fin();
    j.fin();

    j.arreglo("caminos");
    for (size_t ci = 0; ci < caminos.size(); ++ci) {
        const Camino& c = caminos[ci];
        const ResultadoCamino& res = resultados[ci];
        const Estadisticas& e = c.est;
        const double ultima = e.latencias.empty() ? Tu : e.latencias.back();
        const double tiempo = c.tiempoAire + (ultima - Tu);
        const double goodput = tiempo > 0 ? c.bitsBien / tiempo : 0;  // bits entregados con CRC correcto por segundo
        int64_t enPlazo = 0;
        for (double l : e.latencias) enPlazo += l <= esc.latMax + 1e-12;
        const Parametros p0 = parametrosDe(crearCamino(c.tipo, c.bloque, capas, snr, esc, 0), esc);

        j.objeto()
         .str("clave", c.clave).str("nombre", c.nombre).str("codigo", c.codigo).num("tasa", p0.R)
         .ent("tb_bits", p0.A).ent("bits_codificados", p0.G).ent("qm", c.tipo == 0 ? Qm : c.QmFijo)
         .ent("bloques_transporte", e.tbs).ent("transmisiones", e.transmisiones)
         .num("unidades", (double)e.transmisiones / mimo.palabras)
         .num("bler_primera", e.tbs ? (double)e.tbsPrimeraMal / e.tbs : 0)
         .num("bler_final", e.tbs ? (double)e.tbsFinalMal / e.tbs : 0)
         .num("envios_medios", e.tbs ? (double)e.transmisiones / e.tbs : 0)
         .num("ber_bruta", e.bitsBrutos ? (double)e.erroresBrutos / e.bitsBrutos : 0)
         .num("ber_final", e.bitsInfo ? (double)e.erroresInfo / e.bitsInfo : 0).ent("bits_errados", e.erroresInfo)
         .num("iteraciones_medias", e.bloquesDecodificados ? (double)e.iteraciones / e.bloquesDecodificados : 0)
         .num("tiempo_s", tiempo).num("goodput", goodput).num("eficiencia_espectral", goodput / anchoOcupado)
         .num("latencia_media", e.latencias.empty() ? 0 : accumulate(e.latencias.begin(), e.latencias.end(), 0.0) / e.latencias.size())
         .num("latencia_p99", percentil(e.latencias, 0.99)).num("latencia_max", percentil(e.latencias, 1.0))
         .num("en_plazo", e.tbs ? (double)enPlazo / e.tbs : 0)
         .num("IXY_bit", res.IXYbit).ent("bits_diferentes", (int64_t)res.diferentes).num("IXY_byte", res.IXYbyte)
         .num("H_byte", res.HXbyte).booleano("descifrado_ok", res.descifradoOk)
         .booleano("descomprimido", res.descomprimido).booleano("identico", res.identico).str("archivo", res.archivo)
         .num("cpu_ms", c.cpuMs);
        j.arreglo("por_capa");
        for (int l = 0; l < capas; ++l)
            j.objeto().ent("capa", l + 1).ent("palabra", l / mimo.capasPalabra + 1).ent("bits", e.bitsCapa[l])
             .ent("errores", e.erroresCapa[l]).num("ber", e.bitsCapa[l] ? (double)e.erroresCapa[l] / e.bitsCapa[l] : 0).fin();
        j.fin();
        if (!c.mcsUsados.empty()) {
            vector<int> cuenta(tabla.size(), 0);
            for (int m : c.mcsUsados) cuenta[m]++;
            j.num("offset_olla_db", c.offset).arreglo("mcs_usados");
            for (size_t m = 0; m < cuenta.size(); ++m)
                if (cuenta[m]) j.objeto().ent("mcs", (int64_t)m).ent("qm", tabla[m].Qm).num("tasa", tabla[m].R1024 / 1024).ent("bloques", cuenta[m]).fin();
            j.fin();
        }
        if (c.tipo == 0)
            j.objeto("ldpc").ent("grafo_base", c.seg.grafo).ent("zc", c.seg.Zc).ent("bloques_codigo", c.seg.C)
             .ent("k", c.seg.K).ent("k_prima", c.seg.Kprima).ent("n", c.seg.N).ent("crc_tb", c.seg.crcTB)
             .ent("crc_bloque", c.seg.crcCB).fin();
        if (c.bloque) j.objeto("bloque").ent("n", c.bloque->n).ent("k", c.bloque->k).ent("d_min", c.bloque->dMin)
                       .ent("corrige", c.bloque->t).fin();
        j.arreglo("traza");
        for (const TrazaTB& t : c.traza) escribirTraza(j, t);
        j.fin();
        j.fin();
    }
    j.fin();

    j.objeto("agente").booleano("hecho", ag.hecho);
    if (ag.hecho) {
        j.ent("elegido", ag.elegido).ent("mejor_medido", ag.mejor).booleano("coincide", ag.elegido == ag.mejor)
         .ent("bloques_medida", ag.bloquesMedida).num("ms", ag.ms).arreglo("opciones");
        for (const Brazo& b : ag.brazos)
            j.objeto().str("clave", b.clave).str("nombre", b.nombre).ent("tipo", b.tipo).ent("capas", b.capas)
             .ent("pruebas", b.pruebas).num("recompensa_media", b.media())
             .num("goodput", b.goodput()).num("bler_primera", b.tbs ? (double)b.primeraMal / b.tbs : 0)
             .num("bler_final", b.tbs ? (double)b.finalMal / b.tbs : 0)
             .num("envios_medios", b.tbs ? (double)b.envios / b.tbs : 0)
             .num("latencia_media", b.tbs ? b.latSuma / b.tbs : 0)
             .num("a_tiempo", b.bits ? (double)b.bitsPlazo / b.bits : 0).fin();
        j.fin().arreglo("historia");
        for (size_t t = 0; t < ag.historia.size(); ++t)
            j.objeto().ent("paso", (int64_t)t + 1).ent("opcion", ag.historia[t].brazo).num("recompensa", ag.historia[t].recompensa)
             .num("epsilon", ag.historia[t].epsilon).booleano("explora", ag.historia[t].explora).fin();
        j.fin();
    }
    j.fin();

    escribirConfiabilidad(j, conf, esc);
    j.num("segundos", chrono::duration<double>(chrono::steady_clock::now() - inicio).count());
    j.fin();
    archivoJson << "\n";
    avance("Listo", 1, 1, true);
    return 0;
}

// ======================= Curvas BLER frente a SNR =======================

int ejecutarCurvas(const OpcionesSimulacion& op, const string& rutaJson) {
    const Escenario& esc = escenarioDe(op.escenario);
    const auto& tabla = nr::tablaMCS(esc.tablaMCS);
    const int mcs = max(0, elegirMCS(esc.tablaMCS, pow(10.0, op.snrDb / 10), esc.brechaDb));
    const int Qm = tabla[mcs].Qm;
    const double Rmcs = tabla[mcs].R1024 / 1024;
    const int64_t A = 4096;  // bloque de transporte de prueba (un bloque de código)

    unique_ptr<CodigoBloque> cod;
    if (op.fec != "ninguno") {
        cod = make_unique<CodigoBloque>();
        string err;
        if (!CodigoBloque::crear(op.fec, *cod, err)) {
            cerr << err << "\n";
            return 2;
        }
    }
    struct Curva { string clave, nombre; double tasa; int tipo; int64_t G; };  // tipo: 0 LDPC, 1 bloque, 2 sin código
    auto redondear = [&](double bitsCod) { return (int64_t)ceil(bitsCod / Qm) * Qm; };
    vector<Curva> curvas;
    curvas.push_back({"ldpc", "LDPC 5G, tasa del MCS", Rmcs, 0, redondear((A + 24) / Rmcs)});
    if (cod) {
        const double Rb = (double)cod->k / cod->n;
        curvas.push_back({"ldpc_igual", "LDPC 5G, tasa " + to_string(cod->k) + "/" + to_string(cod->n), Rb, 0, redondear((A + 24) / Rb)});
        const int64_t bloques = (A + 24 + cod->k - 1) / cod->k;
        curvas.push_back({"bloque", cod->nombre, Rb, 1, redondear((double)bloques * cod->n)});
    }
    curvas.push_back({"sin", "Sin código", 1.0, 2, redondear(A + 24.0)});

    double desde = op.snrDesde, hasta = op.snrHasta;
    if (hasta <= desde) {
        double minimo = 1e9;
        for (auto& c : curvas) minimo = min(minimo, 10 * log10(pow(2.0, c.tasa * Qm) - 1));
        desde = floor(minimo) - 2;
        hasta = ceil(10 * log10(pow(2.0, Qm) - 1)) + 12;
    }
    const double paso = op.snrPaso > 0 ? op.snrPaso : 1;

    ofstream archivoJson(rutaJson);
    Json j(archivoJson);
    j.objeto().str("modo", "curvas").str("escenario", esc.nombre).ent("mcs", mcs).ent("qm", Qm)
     .num("tasa_mcs", Rmcs).ent("tb_bits", A).ent("bloques_max", op.bloquesCurva);
    j.arreglo("curvas");
    for (auto& c : curvas)
        j.objeto().str("clave", c.clave).str("nombre", c.nombre).num("tasa", c.tasa).ent("bits_codificados", c.G)
         .num("limite_shannon_db", 10 * log10(pow(2.0, c.tasa * Qm) - 1)).fin();
    j.fin().arreglo("puntos");
    mt19937_64 gen(op.semilla);
    for (auto& c : curvas) {
        int ceros = 0;
        for (double s = desde; s <= hasta + 1e-9; s += paso) {
            int64_t errores = 0, bloques = 0, bitsErr = 0;
            if (ceros < 2) {
                CanalAWGN canal(op.semilla + (uint64_t)(s * 1000 + 1e6), pow(10.0, s / 10));
                vector<uint8_t> info(A), salida(A);
                Estadisticas est;
                while (bloques < op.bloquesCurva && errores < 40) {
                    for (auto& b : info) b = gen() & 1;
                    unique_ptr<CodigoTB> codigo;
                    if (c.tipo == 0) codigo = make_unique<TB_LDPC>(A, c.tasa, c.G, Qm, 1, op.iteraciones);
                    else codigo = make_unique<TB_Bloque>(c.tipo == 1 ? cod.get() : nullptr, c.G);
                    const ResultadoTB r = enviarTB(*codigo, A, c.G, Qm, 1, 0, info.data(), salida.data(), canal, 1, est, nullptr);
                    bloques++;
                    errores += !r.ok;
                    for (int64_t i = 0; i < A; ++i) bitsErr += salida[i] != info[i];
                }
                ceros = errores == 0 ? ceros + 1 : 0;
            } else {
                bloques = 0;  // ya llegó a cero: no hace falta seguir simulando esta curva
            }
            j.objeto().str("curva", c.clave).num("snr_db", s).ent("bloques", bloques)
             .num("bler", bloques ? (double)errores / bloques : 0).num("ber", bloques ? (double)bitsErr / (bloques * A) : 0)
             .booleano("simulado", bloques > 0).fin();
        }
        cout << "[Curvas] " << c.nombre << " lista\n";
    }
    j.fin().fin();
    archivoJson << "\n";
    return 0;
}

// ======================= Sólo la prueba de confiabilidad =======================

int ejecutarConfiabilidad(const OpcionesSimulacion& op, const string& rutaJson) {
    const Escenario& esc = escenarioDe(op.escenario);
    const int64_t paquetes = op.paquetes > 0 ? op.paquetes : 300000;
    Confiabilidad conf = probarConfiabilidad(esc, op.snrDb, op.harqMax, op.iteraciones, op.semilla, paquetes);
    ofstream archivoJson(rutaJson);
    Json j(archivoJson);
    j.objeto().str("modo", "confiabilidad");
    j.objeto("escenario").str("clave", esc.clave).str("nombre", esc.nombre).str("generacion", esc.generacion)
     .num("unidad_s", duracionUnidad(esc)).num("rtt_s", esc.rttUnidades * duracionUnidad(esc)).fin();
    j.objeto("canal").num("snr_db", op.snrDb).ent("harq_max", op.harqMax).ent("semilla", (int64_t)op.semilla).fin();
    escribirConfiabilidad(j, conf, esc);
    j.fin();
    archivoJson << "\n";
    return 0;
}
