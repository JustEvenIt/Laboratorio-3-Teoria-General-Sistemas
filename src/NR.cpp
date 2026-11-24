#include "../include/NR.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include "../include/Enlace.h"
#include "../include/GrafosBaseNR.h"

using namespace std;

namespace nr {

// ======================= Tablas MCS (TS 38.214 V17.8.0, Tablas 5.1.3.1-1 a 5.1.3.1-4) =======================

const vector<EntradaMCS>& tablaMCS(int tabla) {
    static const vector<EntradaMCS> t1 = {
        {2, 120}, {2, 157}, {2, 193}, {2, 251}, {2, 308}, {2, 379}, {2, 449}, {2, 526}, {2, 602}, {2, 679},
        {4, 340}, {4, 378}, {4, 434}, {4, 490}, {4, 553}, {4, 616}, {4, 658}, {6, 438}, {6, 466}, {6, 517},
        {6, 567}, {6, 616}, {6, 666}, {6, 719}, {6, 772}, {6, 822}, {6, 873}, {6, 910}, {6, 948}};
    static const vector<EntradaMCS> t2 = {
        {2, 120}, {2, 193}, {2, 308}, {2, 449}, {2, 602}, {4, 378}, {4, 434}, {4, 490}, {4, 553}, {4, 616},
        {4, 658}, {6, 466}, {6, 517}, {6, 567}, {6, 616}, {6, 666}, {6, 719}, {6, 772}, {6, 822}, {6, 873},
        {8, 682.5}, {8, 711}, {8, 754}, {8, 797}, {8, 841}, {8, 885}, {8, 916.5}, {8, 948}};
    static const vector<EntradaMCS> t3 = {
        {2, 30}, {2, 40}, {2, 50}, {2, 64}, {2, 78}, {2, 99}, {2, 120}, {2, 157}, {2, 193}, {2, 251},
        {2, 308}, {2, 379}, {2, 449}, {2, 526}, {2, 602}, {4, 340}, {4, 378}, {4, 434}, {4, 490}, {4, 553},
        {4, 616}, {6, 438}, {6, 466}, {6, 517}, {6, 567}, {6, 616}, {6, 666}, {6, 719}, {6, 772}};
    static const vector<EntradaMCS> t4 = {
        {2, 120}, {2, 193}, {2, 449}, {4, 378}, {4, 490}, {4, 616}, {6, 466}, {6, 517}, {6, 567}, {6, 616},
        {6, 666}, {6, 719}, {6, 772}, {6, 822}, {6, 873}, {8, 682.5}, {8, 711}, {8, 754}, {8, 797}, {8, 841},
        {8, 885}, {8, 916.5}, {8, 948}, {10, 805.5}, {10, 853}, {10, 900.5}, {10, 948}};
    switch (tabla) {
        case 1: return t1;
        case 3: return t3;
        case 4: return t4;
        default: return t2;
    }
}

// ======================= CRC (§5.1) =======================

int largoCRC(TipoCRC t) { return t == TipoCRC::CRC16 ? 16 : 24; }

static uint32_t polinomio(TipoCRC t) {
    switch (t) {
        case TipoCRC::CRC24A: return 0x864CFB;  // D24+D23+D18+D17+D14+D11+D10+D7+D6+D5+D4+D3+D+1
        case TipoCRC::CRC24B: return 0x800063;  // D24+D23+D6+D5+D+1
        default: return 0x1021;                 // D16+D12+D5+1
    }
}

static uint32_t residuo(const uint8_t* bits, size_t n, TipoCRC t) {
    const int L = largoCRC(t);
    const uint32_t g = polinomio(t), mascara = (1u << L) - 1;
    uint32_t r = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t realimentacion = ((r >> (L - 1)) & 1) ^ (bits[i] & 1);
        r = (r << 1) & mascara;
        if (realimentacion) r ^= g;
    }
    return r;
}

vector<uint8_t> calcularCRC(const uint8_t* bits, size_t n, TipoCRC t) {
    const int L = largoCRC(t);
    uint32_t r = residuo(bits, n, t);
    vector<uint8_t> p(L);
    for (int i = 0; i < L; ++i) p[i] = (r >> (L - 1 - i)) & 1;
    return p;
}

bool verificarCRC(const uint8_t* bitsConCRC, size_t nTotal, TipoCRC t) { return residuo(bitsConCRC, nTotal, t) == 0; }

// ======================= LDPC (§5.3.2) =======================

int indiceConjunto(int Zc) {
    const int a[] = {2, 3, 5, 7, 9, 11, 13, 15};
    for (int i = 0; i < 8; ++i)
        for (int z = a[i]; z <= 384; z *= 2)
            if (z == Zc) return i;
    throw invalid_argument("Zc no está en la Tabla 5.3.2-1");
}

const LDPC& LDPC::obtener(int grafo, int Zc) {
    static map<pair<int, int>, unique_ptr<LDPC>> cache;
    static mutex candado;  // la simulación decodifica en varios hilos
    lock_guard<mutex> bloqueo(candado);
    auto& c = cache[{grafo, Zc}];
    if (!c) {
        c.reset(new LDPC());
        c->grafo = grafo;
        c->Zc = Zc;
        c->preparar();
    }
    return *c;
}

void LDPC::preparar() {
    iLS = indiceConjunto(Zc);
    filas = grafo == 1 ? 46 : 42;
    columnas = grafo == 1 ? 68 : 52;
    kb = grafo == 1 ? 22 : 10;
    K = kb * Zc;
    N = (columnas - 2) * Zc;
    const EntradaGrafoBase* tabla = grafo == 1 ? GRAFO_BASE_1 : GRAFO_BASE_2;
    const int entradas = grafo == 1 ? ENTRADAS_GRAFO_BASE_1 : ENTRADAS_GRAFO_BASE_2;
    filasH.assign(filas, {});
    for (int e = 0; e < entradas; ++e) filasH[tabla[e].fila].push_back({tabla[e].columna, tabla[e].v[iLS] % Zc});

    // Las filas de extensión (4 en adelante) tienen la identidad en su propia columna de paridad kb + fila
    for (int r = 4; r < filas; ++r) {
        bool identidad = false;
        for (auto& a : filasH[r]) {
            if (a.columna > kb + r) throw logic_error("grafo base con forma inesperada");
            if (a.columna == kb + r) identidad = a.desplazamiento == 0;
        }
        if (!identidad) throw logic_error("grafo base sin identidad en la paridad de extensión");
    }

    // Núcleo: filas 0-3 y columnas kb..kb+3. Se invierte la matriz 4Zc x 4Zc una sola vez (Gauss-Jordan en GF(2))
    const int n = 4 * Zc;
    palabrasNucleo = (n + 63) / 64;
    vector<uint64_t> A((size_t)n * palabrasNucleo, 0), I((size_t)n * palabrasNucleo, 0);
    auto bit = [&](vector<uint64_t>& M, int fila, int col) -> void { M[(size_t)fila * palabrasNucleo + col / 64] ^= 1ULL << (col % 64); };
    for (int r = 0; r < 4; ++r)
        for (auto& a : filasH[r])
            if (a.columna >= kb && a.columna < kb + 4)
                for (int z = 0; z < Zc; ++z) bit(A, r * Zc + z, (a.columna - kb) * Zc + (z + a.desplazamiento) % Zc);
    for (int i = 0; i < n; ++i) bit(I, i, i);
    for (int c = 0; c < n; ++c) {
        int piv = -1;
        for (int r = c; r < n; ++r)
            if ((A[(size_t)r * palabrasNucleo + c / 64] >> (c % 64)) & 1) { piv = r; break; }
        if (piv < 0) throw logic_error("el núcleo del LDPC no es invertible");
        if (piv != c)
            for (int w = 0; w < palabrasNucleo; ++w) {
                swap(A[(size_t)piv * palabrasNucleo + w], A[(size_t)c * palabrasNucleo + w]);
                swap(I[(size_t)piv * palabrasNucleo + w], I[(size_t)c * palabrasNucleo + w]);
            }
        for (int r = 0; r < n; ++r)
            if (r != c && ((A[(size_t)r * palabrasNucleo + c / 64] >> (c % 64)) & 1))
                for (int w = 0; w < palabrasNucleo; ++w) {
                    A[(size_t)r * palabrasNucleo + w] ^= A[(size_t)c * palabrasNucleo + w];
                    I[(size_t)r * palabrasNucleo + w] ^= I[(size_t)c * palabrasNucleo + w];
                }
    }
    inversaNucleo.swap(I);
}

void LDPC::codificar(const uint8_t* c, uint8_t* w) const {
    copy(c, c + K, w);
    // λ: aporte de la información a las 4 primeras filas; la paridad del núcleo es p = A^-1 λ
    const int n = 4 * Zc;
    vector<uint64_t> lambda(palabrasNucleo, 0);
    for (int r = 0; r < 4; ++r)
        for (auto& a : filasH[r])
            if (a.columna < kb) {
                const uint8_t* col = c + (size_t)a.columna * Zc;
                for (int z = 0; z < Zc; ++z)
                    if (col[(z + a.desplazamiento) % Zc]) lambda[(r * Zc + z) / 64] ^= 1ULL << ((r * Zc + z) % 64);
            }
    for (int i = 0; i < n; ++i) {
        const uint64_t* fila = &inversaNucleo[(size_t)i * palabrasNucleo];
        uint64_t x = 0;
        for (int k = 0; k < palabrasNucleo; ++k) x ^= fila[k] & lambda[k];
        w[K + i] = (uint8_t)__builtin_parityll(x);
    }
    // Filas de extensión: cada una define su bit de paridad a partir de las columnas anteriores
    for (int r = 4; r < filas; ++r) {
        uint8_t* p = w + (size_t)(kb + r) * Zc;
        fill(p, p + Zc, 0);
        for (auto& a : filasH[r]) {
            if (a.columna == kb + r) continue;
            const uint8_t* col = w + (size_t)a.columna * Zc;
            for (int z = 0; z < Zc; ++z) p[z] ^= col[(z + a.desplazamiento) % Zc];
        }
    }
}

bool LDPC::cumpleParidad(const uint8_t* w, int filasActivas) const {
    vector<uint8_t> s(Zc);
    for (int r = 0; r < filasActivas; ++r) {
        fill(s.begin(), s.end(), 0);
        for (auto& a : filasH[r]) {
            const uint8_t* col = w + (size_t)a.columna * Zc;
            const int d = a.desplazamiento;
            for (int z = 0; z < Zc - d; ++z) s[z] ^= col[z + d] & 1;
            for (int z = Zc - d; z < Zc; ++z) s[z] ^= col[z + d - Zc] & 1;
        }
        for (int z = 0; z < Zc; ++z)
            if (s[z]) return false;
    }
    return true;
}

int LDPC::decodificar(vector<float>& L, int filasActivas, int maxIteraciones, bool& ok) const {
    const float alfa = 0.75f;  // min-sum normalizado
    filasActivas = max(4, min(filasActivas, filas));
    // Mensajes de cada fila hacia sus variables: grado x Zc
    vector<vector<float>> R(filasActivas);
    size_t gradoMax = 0;
    for (int r = 0; r < filasActivas; ++r) {
        R[r].assign(filasH[r].size() * Zc, 0.0f);
        gradoMax = max(gradoMax, filasH[r].size());
    }
    vector<float> Q(gradoMax * Zc), min1(Zc), min2(Zc);
    vector<int> posMin(Zc);
    vector<uint8_t> signo(Zc), duro((size_t)columnas * Zc);
    ok = false;
    int it = 0;
    for (it = 1; it <= maxIteraciones; ++it) {
        for (int r = 0; r < filasActivas; ++r) {
            const auto& aristas = filasH[r];
            const int grado = (int)aristas.size();
            float* Rr = R[r].data();
            fill(min1.begin(), min1.end(), 1e30f);
            fill(min2.begin(), min2.end(), 1e30f);
            fill(signo.begin(), signo.end(), 0);
            // Variable -> fila: Q = L - R (con la permutación circular de cada arista)
            for (int e = 0; e < grado; ++e) {
                const float* Lc = &L[(size_t)aristas[e].columna * Zc];
                const int s = aristas[e].desplazamiento;
                float* q = &Q[(size_t)e * Zc];
                const float* re = Rr + (size_t)e * Zc;
                for (int z = 0; z < Zc - s; ++z) q[z] = Lc[z + s] - re[z];
                for (int z = Zc - s; z < Zc; ++z) q[z] = Lc[z + s - Zc] - re[z];
                for (int z = 0; z < Zc; ++z) {
                    const float a = fabsf(q[z]);
                    signo[z] ^= (uint8_t)(q[z] < 0);
                    const bool menor = a < min1[z];
                    min2[z] = menor ? min1[z] : fminf(min2[z], a);
                    posMin[z] = menor ? e : posMin[z];
                    min1[z] = menor ? a : min1[z];
                }
            }
            // Fila -> variable: signo del producto y el menor de los otros valores, escalado por alfa
            for (int e = 0; e < grado; ++e) {
                float* Lc = &L[(size_t)aristas[e].columna * Zc];
                const int s = aristas[e].desplazamiento;
                const float* q = &Q[(size_t)e * Zc];
                float* re = Rr + (size_t)e * Zc;
                for (int z = 0; z < Zc; ++z) {
                    const float m = alfa * (posMin[z] == e ? min2[z] : min1[z]);
                    re[z] = (signo[z] ^ (uint8_t)(q[z] < 0)) ? -m : m;
                }
                for (int z = 0; z < Zc - s; ++z) Lc[z + s] = q[z] + re[z];
                for (int z = Zc - s; z < Zc; ++z) Lc[z + s - Zc] = q[z] + re[z];
            }
        }
        const size_t usadas = (size_t)min(columnas, kb + filasActivas) * Zc;
        for (size_t i = 0; i < usadas; ++i) duro[i] = L[i] < 0;
        if (cumpleParidad(duro.data(), filasActivas)) {
            ok = true;
            break;
        }
    }
    return min(it, maxIteraciones);
}

// ======================= Segmentación y adaptación de tasa (§5.2.2, §5.4.2) =======================

Segmentacion segmentar(int64_t A, double R) {
    SegmentacionNR s = segmentarNR(A, R);
    Segmentacion r;
    r.grafo = s.grafoBase;
    r.Zc = s.Zc;
    r.C = (int)s.C;
    r.Kprima = (int)s.Kprima;
    r.K = (int)s.K;
    r.N = (int)s.N;
    r.crcTB = s.crcTB;
    r.crcCB = s.crcCB;
    return r;
}

vector<int> bitsPorBloque(int64_t G, int C, int Qm, int capas) {
    const int64_t q = G / ((int64_t)Qm * capas);
    vector<int> E(C);
    for (int r = 0; r < C; ++r)
        E[r] = (int)((int64_t)capas * Qm * (r <= C - (int)(q % C) - 1 ? q / C : (q + C - 1) / C));
    return E;
}

int inicioVersion(int rv, int grafo, int Ncb, int Zc) {
    static const int num1[] = {0, 17, 33, 56}, num2[] = {0, 13, 25, 43};
    const int den = grafo == 1 ? 66 : 50;
    const int num = grafo == 1 ? num1[rv & 3] : num2[rv & 3];
    return (int)(((int64_t)num * Ncb / ((int64_t)den * Zc)) * Zc);
}

void adaptarTasa(const uint8_t* d, int N, int E, int k0, int Qm, uint8_t* f) {
    vector<uint8_t> e(E);
    for (int k = 0, j = 0; k < E; ++j) {
        uint8_t b = d[(k0 + j) % N];
        if (b != NULO) e[k++] = b;
    }
    const int filasEnt = E / Qm;
    for (int j = 0; j < filasEnt; ++j)
        for (int i = 0; i < Qm; ++i) f[i + j * Qm] = e[i * filasEnt + j];
}

void recuperarTasa(const float* f, int E, int k0, int Qm, const uint8_t* d, int N, float* buffer) {
    vector<float> e(E);
    const int filasEnt = E / Qm;
    for (int j = 0; j < filasEnt; ++j)
        for (int i = 0; i < Qm; ++i) e[i * filasEnt + j] = f[i + j * Qm];
    for (int k = 0, j = 0; k < E; ++j) {
        int pos = (k0 + j) % N;
        if (d[pos] != NULO) buffer[pos] += e[k++];
    }
}

// ======================= Modulación (TS 38.211 §5.1) =======================

// Niveles de un eje con m bits: A = (1-2a0)[2^(m-1) - (1-2a1)[2^(m-2) - ... [2 - (1-2a_(m-1))]]], normalizados
static vector<float> calcularNiveles(int m) {
    vector<float> nivel;
    if (m >= 1) {
        const float escala = 1.0f / sqrtf(2.0f * ((1 << (2 * m)) - 1) / 3.0f);
        nivel.resize(1 << m);
        for (int p = 0; p < (1 << m); ++p) {
            auto a = [&](int k) { return 1 - 2 * ((p >> (m - 1 - k)) & 1); };
            int t = 1;
            if (m >= 2) {
                t = 2 - a(m - 1);
                for (int k = m - 2; k >= 1; --k) t = (1 << (m - k)) - a(k) * t;
            }
            nivel[p] = a(0) * t * escala;
        }
    }
    return nivel;
}

// Se calculan una sola vez al iniciar (seguro con varios hilos)
static const vector<float>& niveles(int m) {
    static const vector<float> tabla[6] = {calcularNiveles(0), calcularNiveles(1), calcularNiveles(2),
                                           calcularNiveles(3), calcularNiveles(4), calcularNiveles(5)};
    return tabla[m];
}

void modular(const uint8_t* bits, size_t nSimbolos, int Qm, Complejo* simbolos) {
    const int m = Qm / 2;
    const vector<float>& nv = niveles(m);
    for (size_t s = 0; s < nSimbolos; ++s) {
        const uint8_t* b = bits + s * Qm;
        int pI = 0, pQ = 0;
        for (int k = 0; k < m; ++k) {
            pI = (pI << 1) | b[2 * k];
            pQ = (pQ << 1) | b[2 * k + 1];
        }
        simbolos[s] = {nv[pI], nv[pQ]};
    }
}

void demodular(const Complejo* y, const float* varianza, size_t nSimbolos, int Qm, float* llr) {
    const int m = Qm / 2;
    const vector<float>& nv = niveles(m);
    const int P = 1 << m;
    for (size_t s = 0; s < nSimbolos; ++s) {
        const float inv = 1.0f / max(varianza[s], 1e-12f);
        for (int eje = 0; eje < 2; ++eje) {
            const float v = eje == 0 ? y[s].re : y[s].im;
            float m0[5], m1[5];
            for (int k = 0; k < m; ++k) m0[k] = m1[k] = 1e30f;
            for (int p = 0; p < P; ++p) {
                float d = (v - nv[p]) * (v - nv[p]);
                for (int k = 0; k < m; ++k) {
                    if ((p >> (m - 1 - k)) & 1) m1[k] = min(m1[k], d);
                    else m0[k] = min(m0[k], d);
                }
            }
            for (int k = 0; k < m; ++k) llr[s * Qm + 2 * k + eje] = (m1[k] - m0[k]) * inv;
        }
    }
}

// ======================= Canal =======================

void Canal::transmitir(vector<Complejo>& x, vector<float>& varianza) {
    const double N0 = 1.0 / snr;
    const double sigma = sqrt(N0 / 2);  // la mitad del ruido en cada eje (I y Q)
    varianza.assign(x.size(), (float)N0);
    for (auto& s : x) {
        s.re += (float)(normal(gen) * sigma);
        s.im += (float)(normal(gen) * sigma);
    }
}

// ======================= Utilidades =======================

vector<uint8_t> bytesABits(const uint8_t* bytes, size_t n) {
    vector<uint8_t> b(n * 8);
    for (size_t i = 0; i < n; ++i)
        for (int k = 0; k < 8; ++k) b[i * 8 + k] = (bytes[i] >> (7 - k)) & 1;
    return b;
}

vector<uint8_t> bitsABytes(const uint8_t* bits, size_t nBits) {
    vector<uint8_t> o((nBits + 7) / 8, 0);
    for (size_t i = 0; i < nBits; ++i)
        if (bits[i] & 1) o[i / 8] |= (uint8_t)(0x80 >> (i % 8));
    return o;
}

}  // namespace nr
