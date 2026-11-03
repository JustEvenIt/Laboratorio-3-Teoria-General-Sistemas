#include "../include/AgenteRL.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "../include/Compresor.h"
#include "../include/Json.h"
#include "../include/ProcesadorFuente.h"
#include "../include/Sistema.h"

using namespace std;
namespace fs = std::filesystem;

double Aleatorio::normal() {
    double u = uniforme(), v = uniforme();
    return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

// ======================= Page-Hinkley =======================

bool PageHinkley::actualizar(double x) {
    n++;
    media += (x - media) / n;
    sumaArriba += x - media - delta;   // crece si la media sube
    minArriba = min(minArriba, sumaArriba);
    sumaAbajo += x - media + delta;    // decrece si la media baja
    maxAbajo = max(maxAbajo, sumaAbajo);
    return (int)n >= minimo && (sumaArriba - minArriba > lambda || (dosLados && maxAbajo - sumaAbajo > lambda));
}

void PageHinkley::reiniciar() {
    n = 0;
    media = sumaArriba = minArriba = sumaAbajo = maxAbajo = 0;
}

// ======================= Red Q =======================

RedQ::RedQ(int entradas, int ocultas, int salidas, double tasa, uint64_t semilla)
    : E(entradas), O(ocultas), S(salidas), tasa(tasa),
      W1(E * O), b1(O, 0), W2(O * S), b2(S, 0),
      m1(E * O, 0), v1(E * O, 0), mb1(O, 0), vb1(O, 0), m2(O * S, 0), v2(O * S, 0), mb2(S, 0), vb2(S, 0) {
    Aleatorio rng(semilla);
    double e1 = sqrt(2.0 / E), e2 = sqrt(1.0 / O);  // inicialización de He (ReLU) y de Xavier (salida lineal)
    for (double& w : W1) w = e1 * rng.normal();
    for (double& w : W2) w = e2 * rng.normal() * 0.1;
}

vector<double> RedQ::evaluar(const vector<double>& s) const {
    vector<double> h(O), q(b2);
    for (int j = 0; j < O; ++j) {
        double z = b1[j];
        for (int i = 0; i < E; ++i) z += W1[i * O + j] * s[i];
        h[j] = z > 0 ? z : 0;
    }
    for (int j = 0; j < O; ++j)
        if (h[j] > 0)
            for (int k = 0; k < S; ++k) q[k] += W2[j * S + k] * h[j];
    return q;
}

void RedQ::entrenar(const vector<const vector<double>*>& estados, const vector<int>& acciones, const vector<double>& objetivos) {
    vector<double> gW1(E * O, 0), gb1(O, 0), gW2(O * S, 0), gb2(S, 0), h(O);
    const double escala = 1.0 / estados.size();
    for (size_t m = 0; m < estados.size(); ++m) {
        const vector<double>& s = *estados[m];
        for (int j = 0; j < O; ++j) {
            double z = b1[j];
            for (int i = 0; i < E; ++i) z += W1[i * O + j] * s[i];
            h[j] = z > 0 ? z : 0;
        }
        int a = acciones[m];
        double q = b2[a];
        for (int j = 0; j < O; ++j) q += W2[j * S + a] * h[j];
        double g = (q - objetivos[m]) * escala;  // derivada de 1/2 (q - y)^2
        gb2[a] += g;
        for (int j = 0; j < O; ++j) {
            gW2[j * S + a] += g * h[j];
            if (h[j] > 0) {
                double gh = g * W2[j * S + a];
                gb1[j] += gh;
                for (int i = 0; i < E; ++i) gW1[i * O + j] += gh * s[i];
            }
        }
    }
    pasos++;
    const double b1a = 0.9, b2a = 0.999, eps = 1e-8;
    const double c1 = 1 - pow(b1a, (double)pasos), c2 = 1 - pow(b2a, (double)pasos);
    auto adam = [&](vector<double>& w, vector<double>& m, vector<double>& v, const vector<double>& g) {
        for (size_t i = 0; i < w.size(); ++i) {
            m[i] = b1a * m[i] + (1 - b1a) * g[i];
            v[i] = b2a * v[i] + (1 - b2a) * g[i] * g[i];
            w[i] -= tasa * (m[i] / c1) / (sqrt(v[i] / c2) + eps);
        }
    };
    adam(W1, m1, v1, gW1);
    adam(b1, mb1, vb1, gb1);
    adam(W2, m2, v2, gW2);
    adam(b2, mb2, vb2, gb2);
}

// ======================= Combinaciones (brazos) =======================

static const int VALORES_N[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
static const int NUM_N = 9;
static const int K = 3 * NUM_N;  // Uniforme, Huffman y LZW con cada N
static const uint64_t DICCIONARIO_LZW = 1 << 20;

static int metodoDe(int a) { return a / NUM_N; }
static int nDe(int a) { return VALORES_N[a % NUM_N]; }
static string nombreBrazo(int a) { return nombreMetodo(metodoDe(a)) + " N=" + to_string(nDe(a)); }

// Resultado exacto de codificar unos datos con una combinación (sin escribir el archivo)
struct Medida {
    uint64_t total = 0, cabecera = 0, carga = 0;
    double longitudMedia = 0;   // bits por símbolo de la fuente
    double varianza = 0;        // sigma^2 de las longitudes (bits^2)
    double varianzaMax = 0;     // Huffman con los empates resueltos hacia abajo (máxima varianza)
    double entropia = 0;        // H(X) con símbolos de N bits
    double klN = 0, klM = 0;    // N - H y log2 M - H
    uint64_t simbolos = 0, alfabeto = 0;
    int anchoUniforme = 0;
};

static double tiempoDesde(chrono::steady_clock::time_point t0) {
    return chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();
}

// Mide un método sobre una fuente ya fragmentada. Uniforme y Huffman se calculan con su fórmula
// exacta (el tamaño de la carga sólo depende de las longitudes); LZW se ejecuta.
static Medida medir(const Fuente& f, const vector<double>& P, const MetricasFuente& mf, int metodo) {
    Compresor compresor;
    Medida x;
    x.entropia = mf.entropia;
    x.klN = mf.klUniformeN;
    x.klM = mf.klUniformeM;
    x.simbolos = f.numSimbolos();
    x.alfabeto = f.tamAlfabeto();
    x.anchoUniforme = Compresor::bitsPara(f.tamAlfabeto());
    Codificado c;
    c.metodo = metodo;
    if (metodo == METODO_UNIFORME) {
        x.carga = (x.simbolos * x.anchoUniforme + 7) / 8;
        x.longitudMedia = x.anchoUniforme;
    } else if (metodo == METODO_HUFFMAN) {
        vector<uint8_t> L = Compresor::longitudesHuffman(f.frecuencias, true);
        MetricasCodigo mh = Sistema::metricasCodigo("Huffman", P, L, mf.entropia);
        uint64_t bits = 0;
        for (size_t k = 0; k < L.size(); ++k) bits += f.frecuencias[k] * L[k];
        x.carga = (bits + 7) / 8;
        x.longitudMedia = mh.longitudMedia;
        x.varianza = mh.varianza;
        x.varianzaMax = Sistema::metricasCodigo("Huffman", P, Compresor::longitudesHuffman(f.frecuencias, false), mf.entropia).varianza;
    } else {
        c = compresor.codificarLZW(f, DICCIONARIO_LZW, 0);
        x.carga = c.carga.size();
        x.longitudMedia = x.simbolos ? (double)c.bitsCarga / x.simbolos : 0;
        x.varianza = c.varianzaLZW;
    }
    x.cabecera = compresor.tamanoCabecera(c, f);
    x.total = x.cabecera + x.carga;
    return x;
}

// Mide las combinaciones pedidas sobre los mismos datos, fragmentando una sola vez por cada N
static vector<Medida> medirVarias(const string& ruta, const vector<unsigned char>& datos, const vector<int>& brazos) {
    ProcesadorFuente procesador;
    vector<Medida> r(brazos.size());
    for (int iN = 0; iN < NUM_N; ++iN) {
        bool hay = false;
        for (int a : brazos) hay |= a % NUM_N == iN;
        if (!hay) continue;
        Fuente f = procesador.fragmentar(ruta, datos, VALORES_N[iN]);
        vector<double> P = procesador.calcularProbabilidades(f);
        MetricasFuente mf = Sistema::analizarFuente(f, P);
        for (size_t i = 0; i < brazos.size(); ++i)
            if (brazos[i] % NUM_N == iN) r[i] = medir(f, P, mf, metodoDe(brazos[i]));
    }
    return r;
}

static vector<int> todosLosBrazos() {
    vector<int> v(K);
    for (int a = 0; a < K; ++a) v[a] = a;
    return v;
}

// Codifica de verdad con una combinación, arma el archivo comprimido y lo vuelve a abrir
static bool codificarYVerificar(const string& ruta, const vector<unsigned char>& datos, int a, vector<uint8_t>& comprimido,
                                double& msComp, double& msDescomp) {
    ProcesadorFuente procesador;
    Compresor compresor;
    auto t0 = chrono::steady_clock::now();
    Fuente f = procesador.fragmentar(ruta, datos, nDe(a));
    Codificado c = metodoDe(a) == METODO_UNIFORME ? compresor.codificarUniforme(f)
                 : metodoDe(a) == METODO_HUFFMAN ? compresor.codificarHuffman(f)
                 : compresor.codificarLZW(f, DICCIONARIO_LZW, 0);
    comprimido = compresor.serializar(c, f);
    msComp = tiempoDesde(t0);
    t0 = chrono::steady_clock::now();
    vector<unsigned char> salida;
    string error;
    bool ok = compresor.deserializar(comprimido, salida, error) && salida == datos;
    msDescomp = tiempoDesde(t0);
    return ok;
}


// Recompensa: fracción de bits ahorrados, 1 - comprimido/original, acotada a [-1, 1]
static double recompensa(uint64_t comprimido, size_t original) {
    return max(-1.0, min(1.0, 1.0 - (double)comprimido / original));
}

// ======================= Estado: lo que el agente observa de cada bloque =======================

static const int NUM_CARACTERISTICAS = 10;

static vector<double> caracteristicas(const vector<unsigned char>& d) {
    const size_t n = d.size();
    vector<double> f(NUM_CARACTERISTICAS, 0.0);
    f[9] = 1;
    if (n == 0) return f;
    array<uint32_t, 256> c8{};
    vector<uint32_t> c16(65536, 0);
    size_t texto = 0, ceros = 0, iguales = 0, altos = 0, suaves = 0, repetidos = 0;
    vector<uint32_t> visto(1 << 14, 0);
    for (size_t i = 0; i < n; ++i) {
        unsigned char b = d[i];
        c8[b]++;
        texto += (b >= 0x20 && b < 0x7F) || b == '\n' || b == '\r' || b == '\t';
        ceros += b == 0;
        altos += b >= 128;
        if (i) iguales += b == d[i - 1];
        if (i + 1 < n && i % 2 == 0) c16[b | (d[i + 1] << 8)]++;
        if (i >= 3) {
            uint32_t g = d[i - 3] | (d[i - 2] << 8) | (d[i - 1] << 16) | ((uint32_t)b << 24);
            uint32_t h = (g * 2654435761u) >> 18;
            if (visto[h] == g + 1) repetidos++;
            visto[h] = g + 1;
        }
    }
    size_t pares = n / 2;
    for (size_t i = 2; i + 1 < n; i += 2) {
        int a = (int16_t)(d[i - 2] | (d[i - 1] << 8)), b = (int16_t)(d[i] | (d[i + 1] << 8));
        suaves += abs(a - b) < 256;
    }
    double H8 = 0, H16 = 0, distintos = 0;
    for (uint32_t c : c8)
        if (c) {
            double p = (double)c / n;
            H8 -= p * log2(p);
            distintos++;
        }
    for (uint32_t c : c16)
        if (c) {
            double p = (double)c / pares;
            H16 -= p * log2(p);
        }
    f[0] = H8 / 8;
    f[1] = pares ? H16 / 16 : 0;
    f[2] = distintos / 256;
    f[3] = (double)texto / n;
    f[4] = (double)ceros / n;
    f[5] = n > 1 ? (double)iguales / (n - 1) : 0;
    f[6] = n > 3 ? (double)repetidos / (n - 3) : 0;
    f[7] = (double)altos / n;
    f[8] = pares > 1 ? (double)suaves / (pares - 1) : 0;
    return f;
}

// ======================= Agente DQN con detección de cambios =======================

class AgenteDQN {
public:
    uint64_t pasosDesdeCambio = 0;
    double epsilon = 1;

    AgenteDQN(const OpcionesAgente& op, bool detectar, uint64_t semilla)
        : op(op), detectar(detectar), red(NUM_CARACTERISTICAS, op.ocultas, K, op.tasa, semilla), rng(semilla ^ 0xABCDEF),
          ph(op.delta, op.lambda, op.minimoMuestras, false) {}

    vector<double> q(const vector<double>& s) const { return red.evaluar(s); }

    // Candidatas: las 'k' acciones con mayor Q. Con probabilidad ε la última se cambia por una al azar
    // (motivo 0 = hubo exploración, 1 = sólo explotación)
    vector<int> elegir(const vector<double>& s, int k, int& motivo) {
        epsilon = max(op.epsMin, eps0 * pow(op.decaimiento, (double)pasosDesdeCambio));
        vector<double> v = red.evaluar(s);
        vector<int> orden(K);
        for (int a = 0; a < K; ++a) orden[a] = a;
        stable_sort(orden.begin(), orden.end(), [&](int x, int y) { return v[x] > v[y]; });
        vector<int> c(orden.begin(), orden.begin() + k);
        motivo = 1;
        if (rng.uniforme() < epsilon) {
            motivo = 0;
            int azar;
            do azar = (int)(rng.siguiente() % K);
            while (k > 1 && find(c.begin(), c.end() - 1, azar) != c.end() - 1);
            c.back() = azar;
        }
        return c;
    }

    // Aprende de la recompensa observada; devuelve true si detectó un cambio en el entorno
    bool aprender(const vector<double>& s, int a, double r, double& error, bool finDePaso = true) {
        error = r - red.evaluar(s)[a];
        memoria.push_back({s, a, r});
        if (memoria.size() > op.memoria) memoria.pop_front();
        for (int u = 0; u < op.actualizaciones; ++u) {
            vector<const vector<double>*> est;
            vector<int> acc;
            vector<double> obj;
            for (int m = 0; m < op.lote; ++m) {
                const Experiencia& e = memoria[rng.siguiente() % memoria.size()];
                est.push_back(&e.s);
                acc.push_back(e.a);
                obj.push_back(e.r);  // gamma = 0: el objetivo es la recompensa inmediata
            }
            red.entrenar(est, acc, obj);
        }
        if (finDePaso) pasosDesdeCambio++;
        if (!detectar || !ph.actualizar(fabs(error))) return false;
        // Cambio detectado: las experiencias viejas ya no describen el entorno; se olvidan y se vuelve a explorar
        memoria.clear();
        ph.reiniciar();
        pasosDesdeCambio = 0;
        eps0 = op.epsCambio;
        return true;
    }

private:
    struct Experiencia {
        vector<double> s;
        int a;
        double r;
    };
    const OpcionesAgente& op;
    bool detectar;
    RedQ red;
    Aleatorio rng;
    PageHinkley ph;
    double eps0 = 1;
    deque<Experiencia> memoria;
};

// ======================= Decisión para el archivo completo =======================

// Regla: el menor tamaño total; si otra combinación queda a menos del 1% de ese tamaño, gana la de menor
// varianza de las longitudes del código (y a igual varianza, la más pequeña)
static int decidir(const vector<Medida>& medidas) {
    uint64_t minimo = UINT64_MAX;
    for (const Medida& m : medidas) minimo = min(minimo, m.total);
    int mejor = -1;
    for (size_t i = 0; i < medidas.size(); ++i) {
        const Medida& m = medidas[i];
        if (m.total > minimo * 1.01) continue;
        if (mejor < 0 || m.varianza < medidas[mejor].varianza - 1e-12 ||
            (fabs(m.varianza - medidas[mejor].varianza) <= 1e-12 && m.total < medidas[mejor].total)) mejor = (int)i;
    }
    return mejor;
}

static void escribirMedida(Json& j, const Medida& m, size_t original) {
    j.ent("total", m.total).ent("cabecera", m.cabecera).ent("carga", m.carga)
     .num("razon", m.total ? (double)original / m.total : 0).num("ahorro", 1.0 - (double)m.total / original)
     .num("longitud_media", m.longitudMedia).num("varianza", m.varianza)
     .num("entropia", m.entropia).num("kl_n", m.klN).ent("alfabeto", m.alfabeto)
     .num("eficiencia", m.longitudMedia > 0 ? m.entropia / m.longitudMedia : 0);
}

// ======================= Agente sobre el archivo =======================

int ejecutarAgente(const OpcionesAgente& op, const vector<unsigned char>& contenido) {
    auto t0 = chrono::steady_clock::now();
    const size_t tam = contenido.size();

    // El archivo se recorre como un flujo de bloques (múltiplos de 256 bytes, el símbolo más grande)
    size_t B = op.bytesBloque;
    if (B == 0) B = min<size_t>(65536, max<size_t>(1024, tam / 200 / 256 * 256));
    if (tam < 8 * B) B = max<size_t>(256, (tam / 8) / 256 * 256);
    const size_t numBloques = (tam + B - 1) / B;
    // Pasos: cada bloque una vez; si hay pocos, el archivo se recorre varias veces (épocas);
    // si hay muchos, se toman repartidos por todo el archivo
    const uint64_t T = op.pasos ? op.pasos : max<uint64_t>(200, min<uint64_t>(numBloques, 2000));
    auto bloqueDe = [&](uint64_t t) -> size_t { return numBloques >= T ? (size_t)(t * numBloques / T) : t % numBloques; };
    auto largoDe = [&](size_t b) { return min(tam, (b + 1) * B) - b * B; };

    AgenteDQN agente(op, op.detectar, op.semilla * 31 + 7);
    const vector<int> brazos = todosLosBrazos();
    vector<vector<double>> recompensasBloque(numBloques), estadoBloque(numBloques);

    struct Paso {
        size_t bloque;
        int a, motivo, mejor;
        bool alarma;
        double rw, rwMejor, epsilon;
    };
    vector<Paso> historial(T);
    uint64_t errores = 0, discrepancias = 0;

    for (uint64_t t = 0; t < T; ++t) {
        Paso& h = historial[t];
        h.bloque = bloqueDe(t);
        const size_t ini = h.bloque * B, largo = largoDe(h.bloque);
        vector<unsigned char> datos(contenido.begin() + ini, contenido.begin() + ini + largo);
        if (recompensasBloque[h.bloque].empty()) {
            // Sólo para mostrar qué tan bien eligió en cada bloque (el agente no lo ve)
            for (const Medida& m : medirVarias(op.ruta, datos, brazos)) recompensasBloque[h.bloque].push_back(recompensa(m.total, largo));
            estadoBloque[h.bloque] = caracteristicas(datos);
        }
        const vector<double>& rw = recompensasBloque[h.bloque];
        const vector<double>& s = estadoBloque[h.bloque];

        // El agente propone sus mejores codificaciones según Q, comprime el bloque con cada una y se queda con la más pequeña
        vector<int> candidatas = agente.elegir(s, op.candidatas, h.motivo);
        h.epsilon = agente.epsilon;
        h.a = -1;
        size_t mejorTam = 0;
        vector<double> obtenidas;
        for (int c : candidatas) {
            vector<uint8_t> comprimido;
            double msC, msD;
            if (!codificarYVerificar(op.ruta, datos, c, comprimido, msC, msD)) errores++;
            double r = recompensa(comprimido.size(), largo);
            if (r != rw[c]) discrepancias++;
            obtenidas.push_back(r);
            if (h.a < 0 || comprimido.size() < mejorTam) h.a = c, mejorTam = comprimido.size();
        }
        h.rw = recompensa(mejorTam, largo);
        h.alarma = false;
        for (size_t i = 0; i < candidatas.size(); ++i) {
            double e;
            h.alarma |= agente.aprender(s, candidatas[i], obtenidas[i], e, i + 1 == candidatas.size());
        }
        h.mejor = (int)(max_element(rw.begin(), rw.end()) - rw.begin());
        h.rwMejor = rw[h.mejor];
    }

    // --- Lo aprendido: Q medio de cada codificación sobre todos los bloques del archivo ---
    vector<double> qMedio(K, 0);
    size_t vistos = 0;
    for (size_t b = 0; b < numBloques; ++b) {
        if (estadoBloque[b].empty()) continue;
        vector<double> q = agente.q(estadoBloque[b]);
        for (int a = 0; a < K; ++a) qMedio[a] += q[a];
        vistos++;
    }
    for (double& x : qMedio) x /= max<size_t>(1, vistos);

    // --- Del bloque al archivo completo: en el archivo la tabla del código se reparte entre más
    //     símbolos, así que el mejor N puede cambiar. Para cada método se parte del N con mayor Q y se
    //     prueban N/4 … 4N sobre el archivo completo hasta que deja de mejorar ---
    vector<int> evaluadas;
    vector<Medida> medidas;
    vector<string> origen;
    auto evaluar = [&](const vector<int>& nuevas, const string& motivo) {
        vector<int> faltan;
        for (int a : nuevas)
            if (find(evaluadas.begin(), evaluadas.end(), a) == evaluadas.end() && find(faltan.begin(), faltan.end(), a) == faltan.end())
                faltan.push_back(a);
        vector<Medida> m = medirVarias(op.ruta, contenido, faltan);
        for (size_t i = 0; i < faltan.size(); ++i) {
            evaluadas.push_back(faltan[i]);
            medidas.push_back(m[i]);
            origen.push_back(motivo);
        }
    };
    for (int metodo = 0; metodo < 3; ++metodo) {
        int aQ = metodo * NUM_N;
        for (int i = 1; i < NUM_N; ++i)
            if (qMedio[metodo * NUM_N + i] > qMedio[aQ]) aQ = metodo * NUM_N + i;
        // Dos puntos de partida: el N con mayor Q y N = 8 (el tamaño puede no variar de forma suave con N)
        for (int a : {aQ, metodo * NUM_N}) {
            evaluar({a}, a == aQ ? "mayor Q del método" : "punto de partida N=8");
            while (true) {
                vector<int> vecinas;
                for (int d = -2; d <= 2; ++d)
                    if (d && a % NUM_N + d >= 0 && a % NUM_N + d < NUM_N) vecinas.push_back(a + d);
                evaluar(vecinas, "N vecino de " + nombreBrazo(a));
                auto totalDe = [&](int x) { return medidas[find(evaluadas.begin(), evaluadas.end(), x) - evaluadas.begin()].total; };
                int mejor = a;
                for (int x : evaluadas)
                    if (metodoDe(x) == metodo && totalDe(x) < totalDe(mejor)) mejor = x;
                if (mejor == a) break;
                a = mejor;
            }
        }
    }
    int iDecision = decidir(medidas);
    int decision = evaluadas[iDecision];

    // --- Compresión real del archivo completo y reconstrucción sólo desde lo comprimido ---
    fs::create_directories(op.salida);
    vector<uint8_t> comprimido;
    double msComp = 0, msDescomp = 0;
    bool identico = codificarYVerificar(op.ruta, contenido, decision, comprimido, msComp, msDescomp);
    bool tamanoCoincide = comprimido.size() == medidas[iDecision].total;
    string rutaComprimido = (fs::path(op.salida) / "agente.ti6g").string();
    {
        ofstream o(rutaComprimido, ios::binary);
        o.write((const char*)comprimido.data(), comprimido.size());
    }
    double msAgente = chrono::duration<double, milli>(chrono::steady_clock::now() - t0).count();

    // --- Verificación: las 27 combinaciones sobre el archivo completo ---
    bool exhaustivo = tam <= (64u << 20);
    vector<Medida> todas;
    int mejorReal = -1;
    if (exhaustivo) {
        todas = medirVarias(op.ruta, contenido, brazos);
        mejorReal = decidir(todas);
    }

    uint64_t alarmas = 0, aciertos = 0;
    for (auto& h : historial) {
        alarmas += h.alarma;
        aciertos += h.rw >= h.rwMejor - 0.01;
    }
    cout << "[Agente] " << T << " pasos sobre " << numBloques << " bloques de " << B << " bytes | decision: " << nombreBrazo(decision)
         << " -> " << comprimido.size() << " bytes | " << (identico ? "reconstruccion identica" : "ERROR de reconstruccion");
    if (exhaustivo) cout << " | mejor de las 27: " << nombreBrazo(mejorReal);
    cout << "\n";

    // ======================= JSON =======================
    ofstream archivoJson(op.json);
    Json j(archivoJson);
    j.objeto().str("modo", "agente");
    j.objeto("configuracion")
     .str("archivo", op.ruta).ent("tamano", tam).ent("bytes_bloque", B).ent("bloques", numBloques).ent("pasos", T)
     .ent("candidatas", op.candidatas).ent("ocultas", op.ocultas).num("tasa", op.tasa).ent("memoria", op.memoria)
     .num("decaimiento", op.decaimiento).num("eps_cambio", op.epsCambio).num("delta", op.delta).num("lambda", op.lambda)
     .fin();
    j.arreglo("acciones");
    for (int a = 0; a < K; ++a)
        j.objeto().ent("id", a).str("nombre", nombreBrazo(a)).str("metodo", nombreMetodo(metodoDe(a))).ent("n_bits", nDe(a))
         .num("q_medio", qMedio[a]).fin();
    j.fin();
    j.arreglo("evaluadas");
    for (size_t i = 0; i < evaluadas.size(); ++i) {
        j.objeto().ent("id", evaluadas[i]).str("nombre", nombreBrazo(evaluadas[i])).str("metodo", nombreMetodo(metodoDe(evaluadas[i])))
         .ent("n_bits", nDe(evaluadas[i])).str("origen", origen[i]).booleano("elegida", evaluadas[i] == decision);
        escribirMedida(j, medidas[i], tam);
        j.fin();
    }
    j.fin();
    j.objeto("decision")
     .ent("id", decision).str("nombre", nombreBrazo(decision)).str("metodo", nombreMetodo(metodoDe(decision))).ent("n_bits", nDe(decision))
     .ent("bytes_comprimido", comprimido.size()).booleano("identico", identico).booleano("tamano_coincide", tamanoCoincide)
     .str("archivo_comprimido", rutaComprimido).num("ms_agente", msAgente)
     .ent("evaluaciones_archivo", evaluadas.size());
    escribirMedida(j, medidas[iDecision], tam);
    j.fin();
    j.objeto("aprendizaje")
     .num("acierto_bloques", (double)aciertos / T).ent("alarmas", alarmas)
     .ent("errores_ida_vuelta", errores).ent("discrepancias", discrepancias).fin();
    j.objeto("verificacion").booleano("exhaustivo", exhaustivo);
    if (exhaustivo) {
        j.ent("mejor_real", mejorReal).str("mejor_real_nombre", nombreBrazo(mejorReal)).booleano("acierto", mejorReal == decision)
         .num("diferencia", (double)comprimido.size() / todas[mejorReal].total - 1);
        j.arreglo("todas");
        for (int a = 0; a < K; ++a) {
            j.objeto().ent("id", a).str("nombre", nombreBrazo(a)).str("metodo", nombreMetodo(metodoDe(a))).ent("n_bits", nDe(a));
            escribirMedida(j, todas[a], tam);
            j.fin();
        }
        j.fin();
    }
    j.fin();
    j.arreglo("pasos");
    for (uint64_t t = 0; t < T; ++t) {
        const Paso& h = historial[t];
        j.objeto().ent("t", t).ent("b", h.bloque).ent("a", h.a).ent("m", h.motivo).booleano("al", h.alarma)
         .num("rw", h.rw).ent("ao", h.mejor).num("rwo", h.rwMejor).num("eps", h.epsilon).fin();
    }
    j.fin();
    j.fin();
    archivoJson << "\n";
    return identico && tamanoCoincide && !errores && !discrepancias ? 0 : 4;
}
