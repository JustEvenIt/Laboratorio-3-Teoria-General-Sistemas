#include "../include/Simulacion.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <vector>
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
    double fiabMin, fiabMax;// requisito de confiabilidad (probabilidad de éxito); 0 si no aplica
    double tasaMin, tasaMax;// velocidad experimentada por el usuario (bit/s)
    int paquetes;           // paquetes de 32 bytes de la prueba de confiabilidad
    int rttUnidades;        // unidades de transmisión entre dos envíos del mismo bloque (supuesto)
    double fraccionBajada;  // TDD DDDSU: 3 slots y 10 de 14 símbolos del especial son de bajada
    double blerObjetivo;    // BLER de la primera transmisión que busca la adaptación de enlace
};

const Escenario ESCENARIOS[] = {
    {"embb", "5G eMBB · banda ancha (3.5 GHz, 100 MHz)", "5G (IMT-2020)", "n78", 1, 100, 273, 13, 2, 1.5,
     4e-3, 4e-3, 0, 0, 100e6, 100e6, 0, 4, (3 * 14 + 10) / 70.0, 0.1},
    {"urllc", "5G URLLC · baja latencia y alta confiabilidad (3.5 GHz)", "5G (IMT-2020)", "n78", 1, 100, 273, 7, 3, 4.0,
     1e-3, 1e-3, 1 - 1e-5, 1 - 1e-5, 0, 0, 100000, 4, (3 * 14 + 10) / 70.0, 1e-3},
    {"6g", "6G · escenario candidato (400 MHz)", "6G (IMT-2030)", "candidata", 3, 400, 264, 7, 4, 1.5,
     0.1e-3, 1e-3, 1 - 1e-5, 1 - 1e-7, 300e6, 500e6, 100000, 4, (3 * 14 + 10) / 70.0, 0.1},
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

// Bits que caben en una transmisión: elementos de recurso de datos x bits por símbolo (1 símbolo OFDM de referencia)
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

// ======================= Estadísticas =======================

struct Estadisticas {
    int64_t tbs = 0, transmisiones = 0, tbsPrimeraMal = 0, tbsFinalMal = 0;
    int64_t bitsBrutos = 0, erroresBrutos = 0;  // bits codificados de la primera transmisión
    int64_t bitsInfo = 0, erroresInfo = 0;      // bits de información después de HARQ
    int64_t iteraciones = 0, bloquesDecodificados = 0;
    vector<double> latencias;
    double sumaInfoMutua = 0;
    int64_t bitsInfoMutua = 0;
    vector<Complejo> constelacion;
};

double log2UnoMasExp(double x) { return (x > 0 ? x + log1p(exp(-x)) : log1p(exp(x))) / log(2.0); }

// Modula, pasa por el canal y demodula. En la primera transmisión mide errores brutos,
// la información mutua por bit del demodulador (BICM) y guarda una muestra de la constelación
void pasarPorCanal(const vector<uint8_t>& g, int Qm, nr::Canal& canal, vector<float>& llr, Estadisticas& est, bool primera) {
    const size_t ns = g.size() / Qm;
    vector<Complejo> x(ns);
    nr::modular(g.data(), ns, Qm, x.data());
    vector<float> var;
    canal.transmitir(x, var);
    llr.assign(ns * Qm, 0.0f);
    nr::demodular(x.data(), var.data(), ns, Qm, llr.data());
    if (!primera) return;
    for (size_t i = 0; i < ns * Qm; ++i) est.erroresBrutos += (llr[i] < 0) != (g[i] != 0);
    est.bitsBrutos += ns * Qm;
    // I(B; LLR) = 1 - E[log2(1 + e^(-(1-2b)·LLR))], estimada con los bits enviados
    const size_t muestra = min<size_t>(ns * Qm, 200000);
    for (size_t i = 0; i < muestra && est.bitsInfoMutua < 4000000; ++i) {
        double s = g[i] ? -llr[i] : llr[i];
        est.sumaInfoMutua += 1 - log2UnoMasExp(-s);
        est.bitsInfoMutua++;
    }
    if (est.constelacion.empty()) est.constelacion.assign(x.begin(), x.begin() + min<size_t>(ns, 1500));
}

// ======================= Camino LDPC (TS 38.212) =======================

struct ResultadoTB { bool ok = false; int envios = 0; };

ResultadoTB enviarLDPC(int64_t A, int64_t G, int Qm, double R, const uint8_t* info, uint8_t* salida,
                       nr::Canal& canal, int harqMax, int maxIter, Estadisticas& est) {
    const nr::Segmentacion seg = nr::segmentar(A, R);
    const nr::TipoCRC tcrc = seg.crcTB == 24 ? nr::TipoCRC::CRC24A : nr::TipoCRC::CRC16;
    vector<uint8_t> b(info, info + A);
    auto p = nr::calcularCRC(b.data(), A, tcrc);
    b.insert(b.end(), p.begin(), p.end());
    const nr::LDPC& cod = nr::LDPC::obtener(seg.grafo, seg.Zc);
    const int C = seg.C, Kp = seg.Kprima, K = seg.K, N = seg.N, Zc = seg.Zc, Lcb = seg.crcCB;
    const int porBloque = Kp - Lcb;

    // Segmentación (§5.2.2): Kp - Lcb bits del bloque de transporte + CRC24B si C > 1 + relleno hasta K
    vector<vector<uint8_t>> d(C, vector<uint8_t>(N));
    {
        vector<uint8_t> c(K), w((size_t)cod.columnas * Zc);
        for (int r = 0; r < C; ++r) {
            fill(c.begin(), c.end(), 0);
            copy(b.begin() + (size_t)r * porBloque, b.begin() + (size_t)(r + 1) * porBloque, c.begin());
            if (Lcb) {
                auto q = nr::calcularCRC(c.data(), porBloque, nr::TipoCRC::CRC24B);
                copy(q.begin(), q.end(), c.begin() + porBloque);
            }
            cod.codificar(c.data(), w.data());
            copy(w.begin() + 2 * Zc, w.begin() + 2 * Zc + N, d[r].begin());
            for (int k = Kp; k < K; ++k) d[r][k - 2 * Zc] = nr::NULO;
        }
    }
    const vector<int> E = nr::bitsPorBloque(G, C, Qm, 1);
    vector<vector<float>> buffer(C, vector<float>(N, 0.0f));
    vector<vector<uint8_t>> duro(C, vector<uint8_t>(Kp, 0));
    vector<bool> bloqueOk(C, false);
    vector<uint8_t> g(G), bb((size_t)C * porBloque);
    vector<float> llr, Lv((size_t)cod.columnas * Zc);
    static const int secuenciaRV[] = {0, 2, 3, 1};
    ResultadoTB res;
    for (int t = 0; t < harqMax; ++t) {
        const int rv = secuenciaRV[t % 4];
        const int k0 = nr::inicioVersion(rv, seg.grafo, N, Zc);
        size_t off = 0;
        for (int r = 0; r < C; ++r) {
            nr::adaptarTasa(d[r].data(), N, E[r], k0, Qm, &g[off]);
            off += E[r];
        }
        pasarPorCanal(g, Qm, canal, llr, est, t == 0);
        est.transmisiones++;
        res.envios = t + 1;
        off = 0;
        for (int r = 0; r < C; ++r) {
            nr::recuperarTasa(&llr[off], E[r], k0, Qm, d[r].data(), N, buffer[r].data());
            off += E[r];
        }
        for (int r = 0; r < C; ++r) {
            if (bloqueOk[r]) continue;
            fill(Lv.begin(), Lv.end(), 0.0f);  // los 2Zc primeros bits sistemáticos no se transmiten
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
            est.iteraciones += cod.decodificar(Lv, filas, maxIter, paridad);
            est.bloquesDecodificados++;
            for (int k = 0; k < Kp; ++k) duro[r][k] = Lv[k] < 0;
            bloqueOk[r] = Lcb ? nr::verificarCRC(duro[r].data(), Kp, nr::TipoCRC::CRC24B) : paridad;
        }
        for (int r = 0; r < C; ++r) copy(duro[r].begin(), duro[r].begin() + porBloque, bb.begin() + (size_t)r * porBloque);
        res.ok = nr::verificarCRC(bb.data(), A + seg.crcTB, tcrc);
        if (C == 1) bloqueOk[0] = res.ok;
        if (t == 0 && !res.ok) est.tbsPrimeraMal++;
        if (res.ok) break;
    }
    copy(bb.begin(), bb.begin() + A, salida);
    return res;
}

// ======================= Camino con código de bloque (o sin código) =======================

int64_t infoBloque(int64_t G, int n, int k) {
    const int64_t bloques = G / n;
    int64_t A = (bloques * k - 24) / 8 * 8;
    if (A <= 3824) A = min<int64_t>(3824, (bloques * k - 16) / 8 * 8);
    return max<int64_t>(A, 8);
}

ResultadoTB enviarBloque(const CodigoBloque* cod, int64_t A, int64_t G, int Qm, const uint8_t* info, uint8_t* salida,
                         nr::Canal& canal, int harqMax, Estadisticas& est) {
    const nr::TipoCRC tcrc = A > 3824 ? nr::TipoCRC::CRC24A : nr::TipoCRC::CRC16;
    vector<uint8_t> b(info, info + A);
    auto p = nr::calcularCRC(b.data(), A, tcrc);
    b.insert(b.end(), p.begin(), p.end());
    const int n = cod ? cod->n : 1, k = cod ? cod->k : 1;
    const int64_t B = (int64_t)b.size(), bloques = (B + k - 1) / k;
    vector<uint8_t> g(G, 0);
    for (int64_t i = 0; i < bloques; ++i) {
        uint32_t m = 0;
        for (int j = 0; j < k; ++j) m = (m << 1) | (i * k + j < B ? b[i * k + j] : 0);
        const uint32_t c = cod ? cod->codificar(m) : m;
        for (int j = 0; j < n; ++j) g[i * n + j] = (c >> (n - 1 - j)) & 1;
    }
    vector<float> llr, suma(G, 0.0f);
    vector<uint8_t> mensaje((size_t)bloques * k);
    ResultadoTB res;
    for (int t = 0; t < harqMax; ++t) {
        pasarPorCanal(g, Qm, canal, llr, est, t == 0);
        est.transmisiones++;
        res.envios = t + 1;
        for (int64_t i = 0; i < G; ++i) suma[i] += llr[i];  // combinación de Chase: se suman los LLR de cada envío
        for (int64_t i = 0; i < bloques; ++i) {
            uint32_t r = 0;
            for (int j = 0; j < n; ++j) r = (r << 1) | (suma[i * n + j] < 0);
            uint32_t m = r;
            if (cod) m = (r ^ cod->lider[cod->sindrome(r)]) & ((1u << k) - 1);  // c^ = r + e^, m^ = últimos k bits
            for (int j = 0; j < k; ++j) mensaje[i * k + j] = (m >> (k - 1 - j)) & 1;
        }
        res.ok = nr::verificarCRC(mensaje.data(), B, tcrc);
        if (t == 0 && !res.ok) est.tbsPrimeraMal++;
        if (res.ok) break;
    }
    copy(mensaje.begin(), mensaje.begin() + A, salida);
    return res;
}

// ======================= Medidas =======================

// I(X;Y) por byte entre el archivo original y el recuperado (posición por posición; un byte que falta es un símbolo más)
double infoMutuaBytes(const vector<uint8_t>& x, const vector<uint8_t>& y, double& HX) {
    const size_t n = x.size();
    HX = 0;
    if (n == 0) return 0;
    vector<uint64_t> conj(256 * 257, 0), px(256, 0), py(257, 0);
    for (size_t i = 0; i < n; ++i) {
        const int a = x[i], c = i < y.size() ? y[i] : 256;
        conj[a * 257 + c]++;
        px[a]++;
        py[c]++;
    }
    auto H = [&](const vector<uint64_t>& v) {
        double h = 0;
        for (uint64_t c : v)
            if (c) h -= (double)c / n * log2((double)c / n);
        return h;
    };
    HX = H(px);
    return max(0.0, HX + H(py) - H(conj));
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

struct Camino {
    string clave, nombre, codigo;
    double tasa = 0;
    int64_t A = 0, G = 0;
    nr::Segmentacion seg;
    bool ldpc = false;
    unique_ptr<CodigoBloque> bloque;
    Estadisticas est;
    vector<uint8_t> recuperado;
    vector<int> mcsUsados;      // MCS de cada bloque de transporte (LDPC con adaptación de enlace)
    int64_t bitsBien = 0;       // bits de los bloques que llegaron con CRC correcto
    double offsetFinal = 0;
    double cpuMs = 0;
};

}  // namespace

// ======================= Simulación del archivo =======================

int ejecutarSimulacion(const string& rutaOriginal, const string& rutaComprimido, const OpcionesSimulacion& op,
                       const string& rutaJson) {
    const Escenario& esc = escenarioDe(op.escenario);
    const double snr = pow(10.0, op.snrDb / 10);
    const double Tu = duracionUnidad(esc), rtt = esc.rttUnidades * Tu;
    const double scs = 15e3 * (1 << esc.mu), anchoOcupado = esc.prb * 12 * scs;

    ifstream fo(rutaOriginal, ios::binary), fc(rutaComprimido, ios::binary);
    vector<uint8_t> original((istreambuf_iterator<char>(fo)), {}), comprimido((istreambuf_iterator<char>(fc)), {});
    if (comprimido.empty()) {
        cerr << "No se pudo leer el archivo comprimido " << rutaComprimido << "\n";
        return 1;
    }
    const bool completo = op.maxBytes == 0 || op.maxBytes >= comprimido.size();
    const size_t bytesSim = completo ? comprimido.size() : op.maxBytes;
    const vector<uint8_t> bits = nr::bytesABits(comprimido.data(), bytesSim);
    const int64_t totalBits = (int64_t)bits.size();

    // Modulación y tasa del código según la SNR (tabla MCS del escenario)
    const auto& tabla = nr::tablaMCS(esc.tablaMCS);
    const int mcsAuto = elegirMCS(esc.tablaMCS, snr, esc.brechaDb);
    const int mcs = max(0, mcsAuto);
    const int Qm = tabla[mcs].Qm;
    const double R = tabla[mcs].R1024 / 1024;
    const int64_t G = bitsCodificados(esc, esc.prb, Qm);

    // Caminos que se comparan
    vector<Camino> caminos;
    {
        Camino c;
        c.clave = "ldpc";
        c.nombre = "LDPC de 5G NR";
        c.codigo = "LDPC (TS 38.212)";
        c.ldpc = true;
        c.A = tbsNR(esc.prb, esc.simbolos, 12, 0, R, Qm, 1);
        c.G = G;
        c.seg = nr::segmentar(c.A, R);
        c.tasa = (double)c.A / G;
        caminos.push_back(move(c));
    }
    if (op.fec != "ninguno") {
        auto cod = make_unique<CodigoBloque>();
        string err;
        if (!CodigoBloque::crear(op.fec, *cod, err)) {
            cerr << err << "\n";
            return 2;
        }
        Camino c;
        c.clave = "bloque";
        c.nombre = cod->nombre;
        c.codigo = cod->nombre + " · decodificación por síndrome";
        c.A = infoBloque(G, cod->n, cod->k);
        c.G = G;
        c.tasa = (double)c.A / G;
        c.bloque = move(cod);
        caminos.push_back(move(c));
    }
    {
        Camino c;
        c.clave = "sin";
        c.nombre = "Sin código";
        c.codigo = "Sólo CRC";
        c.A = infoBloque(G, 1, 1);
        c.G = G;
        c.tasa = (double)c.A / G;
        caminos.push_back(move(c));
    }

    fs::create_directories(op.salida);
    string ext = fs::path(rutaOriginal).extension().string();
    for (Camino& c : caminos) {
        auto t0 = chrono::steady_clock::now();
        nr::Canal canal(op.semilla);
        canal.snr = snr;
        c.recuperado.assign(totalBits, 0);
        vector<uint8_t> info, salida;
        // Adaptación de enlace de lazo externo (OLLA) para el LDPC con MCS automático: cada NACK de la primera
        // transmisión baja la SNR estimada 1 dB y cada ACK la sube 1 dB·b/(1-b), así la BLER tiende al objetivo b
        double offset = 0;
        const double bajada = 1.0, subida = bajada * esc.blerObjetivo / (1 - esc.blerObjetivo);
        int64_t A = c.A;
        for (int64_t pos = 0; pos < totalBits; pos += A) {
            int Qt = Qm;
            double Rt = R;
            int64_t Gt = G;
            if (c.ldpc) {
                const int m = max(0, elegirMCS(esc.tablaMCS, pow(10.0, (op.snrDb - offset) / 10), esc.brechaDb));
                Qt = tabla[m].Qm;
                Rt = tabla[m].R1024 / 1024;
                Gt = bitsCodificados(esc, esc.prb, Qt);
                A = tbsNR(esc.prb, esc.simbolos, 12, 0, Rt, Qt, 1);
                c.mcsUsados.push_back(m);
            }
            const int64_t trozo = min<int64_t>(A, totalBits - pos);
            info.assign(A, 0);
            salida.assign(A, 0);
            copy(bits.begin() + pos, bits.begin() + pos + trozo, info.begin());
            ResultadoTB r = c.ldpc ? enviarLDPC(A, Gt, Qt, Rt, info.data(), salida.data(), canal, op.harqMax, op.iteraciones, c.est)
                                   : enviarBloque(c.bloque.get(), A, G, Qm, info.data(), salida.data(), canal, op.harqMax, c.est);
            if (c.ldpc) offset += (r.ok && r.envios == 1) ? -subida : bajada;
            c.est.tbs++;
            if (r.ok) c.bitsBien += trozo;
            if (!r.ok) c.est.tbsFinalMal++;
            c.est.latencias.push_back(Tu + (r.envios - 1) * rtt);
            for (int64_t i = 0; i < trozo; ++i) c.est.erroresInfo += salida[i] != info[i];
            c.est.bitsInfo += trozo;
            copy(salida.begin(), salida.begin() + trozo, c.recuperado.begin() + pos);
        }
        c.offsetFinal = offset;
        c.cpuMs = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
        cout << "[Simulacion] " << c.nombre << ": " << c.est.tbs << " bloques de transporte, " << c.est.transmisiones
             << " transmisiones, " << c.est.tbsFinalMal << " con error al final, " << c.est.erroresInfo << " bits errados\n";
    }

    // ---------- Prueba de confiabilidad: paquetes de 32 bytes con el plazo del requisito ----------
    struct Confiabilidad {
        bool hecha = false;
        int64_t paquetes = 0, fallas = 0, enPlazoMin = 0;
        int mcs = 0, Qm = 0, prb = 0, maxEnvios = 0;
        double R = 0, cota = 1;
        int64_t tbs = 0;
    } conf;
    const int paquetes = op.paquetes >= 0 ? op.paquetes : esc.paquetes;
    if (paquetes > 0 && esc.fiabMin > 0) {
        conf.hecha = true;
        conf.paquetes = paquetes;
        conf.mcs = max(0, elegirMCS(3, snr, 4.0));
        const auto& t3 = nr::tablaMCS(3);
        conf.Qm = t3[conf.mcs].Qm;
        conf.R = t3[conf.mcs].R1024 / 1024;
        for (conf.prb = 1; conf.prb < esc.prb; ++conf.prb)
            if (tbsNR(conf.prb, esc.simbolos, 12, 0, conf.R, conf.Qm, 1) >= 256) break;
        conf.tbs = tbsNR(conf.prb, esc.simbolos, 12, 0, conf.R, conf.Qm, 1);
        const int64_t Gp = bitsCodificados(esc, conf.prb, conf.Qm);
        conf.maxEnvios = Tu > esc.latMax ? 0 : min(op.harqMax, 1 + (int)floor((esc.latMax - Tu) / rtt + 1e-9));
        nr::Canal canal(op.semilla + 7);
        canal.snr = snr;
        mt19937_64 gen(op.semilla + 11);
        vector<uint8_t> info(conf.tbs), salida(conf.tbs);
        Estadisticas est;
        for (int64_t i = 0; i < paquetes && conf.maxEnvios > 0; ++i) {
            for (int j = 0; j < 256; ++j) info[j] = gen() & 1;
            ResultadoTB r = enviarLDPC(conf.tbs, Gp, conf.Qm, conf.R, info.data(), salida.data(), canal, conf.maxEnvios,
                                       op.iteraciones, est);
            if (!r.ok) conf.fallas++;
            else if (Tu + (r.envios - 1) * rtt <= esc.latMin + 1e-12) conf.enPlazoMin++;
        }
        if (conf.maxEnvios == 0) conf.fallas = paquetes;
        conf.cota = cotaSuperior(conf.fallas, paquetes);
        cout << "[Simulacion] Confiabilidad: " << paquetes << " paquetes de 32 bytes, " << conf.fallas << " fallas\n";
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
    j.objeto("canal").num("snr_db", op.snrDb).ent("harq_max", op.harqMax).ent("semilla", op.semilla)
     .num("capacidad_shannon", log2(1 + snr)).fin();
    j.objeto("mcs").ent("indice", mcs).booleano("viable", mcsAuto >= 0)
     .num("bler_objetivo", esc.blerObjetivo)
     .ent("qm", Qm).num("tasa", R).num("eficiencia", Qm * R).ent("bits_codificados", G).fin();
    j.objeto("archivo").ent("bytes_original", original.size()).ent("bytes_comprimido", comprimido.size())
     .ent("bytes_simulados", bytesSim).booleano("completo", completo).fin();

    const Camino& base = caminos[0];
    const double infoBit = base.est.bitsInfoMutua ? base.est.sumaInfoMutua / base.est.bitsInfoMutua : 0;
    j.objeto("bicm").num("info_mutua_bit", infoBit).num("info_mutua_simbolo", infoBit * Qm)
     .num("tasa_codigo_simbolo", R * Qm).fin();
    j.arreglo("constelacion");
    for (const Complejo& s : base.est.constelacion) j.arreglo().num(nullptr, s.re).num(nullptr, s.im).fin();
    j.fin();

    j.arreglo("caminos");
    for (Camino& c : caminos) {
        const Estadisticas& e = c.est;
        const int64_t unidades = e.transmisiones;
        const double ultima = e.latencias.empty() ? Tu : e.latencias.back();
        const double tiempo = unidades * Tu / esc.fraccionBajada + (ultima - Tu);
        const double goodput = tiempo > 0 ? c.bitsBien / tiempo : 0;  // bits entregados con CRC correcto por segundo
        int64_t enPlazo = 0;
        for (double l : e.latencias) enPlazo += l <= esc.latMax + 1e-12;

        // Flujo comprimido recuperado -> archivo
        vector<uint8_t> bytesRec = nr::bitsABytes(c.recuperado.data(), c.recuperado.size());
        vector<uint8_t> bytesEnv(comprimido.begin(), comprimido.begin() + bytesSim);
        uint64_t diferentes = 0;
        const double ixyBit = ControlError::informacionMutuaBits(bytesEnv, bytesRec, totalBits, diferentes);
        bool identico = false, descomprimido = false;
        double ixyByte = 0, HXbyte = 0;
        string archivoRec;
        if (completo) {
            vector<unsigned char> rec;
            string err;
            Compresor compresor;
            try {
                descomprimido = compresor.deserializar(bytesRec, rec, err);
            } catch (...) {
                descomprimido = false;
            }
            if (!descomprimido) rec.clear();
            identico = descomprimido && rec.size() == original.size() && equal(rec.begin(), rec.end(), original.begin());
            ixyByte = infoMutuaBytes(original, vector<uint8_t>(rec.begin(), rec.end()), HXbyte);
            if (descomprimido) {
                archivoRec = (fs::path(op.salida) / ("sim_" + c.clave + ext)).string();
                ofstream o(archivoRec, ios::binary);
                o.write((const char*)rec.data(), rec.size());
            }
        } else {
            vector<uint8_t> vacio;
            infoMutuaBytes(original, vacio, HXbyte);
        }

        j.objeto()
         .str("clave", c.clave).str("nombre", c.nombre).str("codigo", c.codigo).num("tasa", c.tasa)
         .ent("tb_bits", c.A).ent("bits_codificados", c.G)
         .ent("bloques_transporte", e.tbs).ent("transmisiones", e.transmisiones).ent("unidades", unidades)
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
         .num("IXY_bit", ixyBit).ent("bits_diferentes", diferentes).num("IXY_byte", ixyByte).num("H_byte", HXbyte)
         .booleano("descomprimido", descomprimido).booleano("identico", identico).str("archivo", archivoRec)
         .num("cpu_ms", c.cpuMs);
        if (!c.mcsUsados.empty()) {
            vector<int> cuenta(tabla.size(), 0);
            for (int m : c.mcsUsados) cuenta[m]++;
            j.num("offset_olla_db", c.offsetFinal).arreglo("mcs_usados");
            for (size_t m = 0; m < cuenta.size(); ++m)
                if (cuenta[m]) j.objeto().ent("mcs", m).ent("qm", tabla[m].Qm).num("tasa", tabla[m].R1024 / 1024).ent("bloques", cuenta[m]).fin();
            j.fin();
        }
        if (c.ldpc)
            j.objeto("ldpc").ent("grafo_base", c.seg.grafo).ent("zc", c.seg.Zc).ent("bloques_codigo", c.seg.C)
             .ent("k", c.seg.K).ent("k_prima", c.seg.Kprima).ent("n", c.seg.N).ent("crc_tb", c.seg.crcTB)
             .ent("crc_bloque", c.seg.crcCB).fin();
        if (c.bloque) j.objeto("bloque").ent("n", c.bloque->n).ent("k", c.bloque->k).ent("d_min", c.bloque->dMin)
                       .ent("corrige", c.bloque->t).fin();
        j.fin();
    }
    j.fin();

    j.objeto("confiabilidad").booleano("hecha", conf.hecha);
    if (conf.hecha) {
        j.ent("paquetes", conf.paquetes).ent("bytes", 32).ent("fallas", conf.fallas)
         .num("exito", 1.0 - (double)conf.fallas / conf.paquetes).num("cota_falla_95", conf.cota)
         .num("confiabilidad_garantizada", 1 - conf.cota).ent("en_plazo_min", conf.enPlazoMin)
         .ent("mcs", conf.mcs).ent("qm", conf.Qm).num("tasa", conf.R).ent("prb", conf.prb).ent("tbs", conf.tbs)
         .ent("max_envios", conf.maxEnvios).num("plazo_s", esc.latMax).num("plazo_min_s", esc.latMin)
         .num("requisito_min", esc.fiabMin).num("requisito_max", esc.fiabMax)
         .num("paquetes_necesarios", esc.fiabMin < 1 ? 3.0 / (1 - esc.fiabMin) : 0);
    }
    j.fin();
    j.fin();
    archivoJson << "\n";
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
                nr::Canal canal(op.semilla + (uint64_t)(s * 1000 + 1e6));
                canal.snr = pow(10.0, s / 10);
                vector<uint8_t> info(A), salida(A);
                Estadisticas est;
                while (bloques < op.bloquesCurva && errores < 40) {
                    for (auto& b : info) b = gen() & 1;
                    ResultadoTB r = c.tipo == 0 ? enviarLDPC(A, c.G, Qm, c.tasa, info.data(), salida.data(), canal, 1,
                                                             op.iteraciones, est)
                                                : enviarBloque(c.tipo == 1 ? cod.get() : nullptr, A, c.G, Qm, info.data(),
                                                               salida.data(), canal, 1, est);
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
