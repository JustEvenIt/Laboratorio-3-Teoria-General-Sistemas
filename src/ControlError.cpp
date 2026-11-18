#include "../include/ControlError.h"
#include <algorithm>
#include <cmath>
#include <random>
#include "../include/Bits.h"

using namespace std;

string bitsDe(uint32_t valor, int largo) {
    string s(largo, '0');
    for (int i = 0; i < largo; ++i)
        if ((valor >> (largo - 1 - i)) & 1) s[i] = '1';
    return s;
}

// Siguiente entero con la misma cantidad de unos (recorre las combinaciones de w posiciones)
static uint32_t siguienteCombinacion(uint32_t x) {
    uint32_t c = x & (0u - x), r = x + c;
    return (((r ^ x) >> 2) / c) | r;
}

static double binomial(int n, int i) {
    double b = 1;
    for (int j = 1; j <= i; ++j) b = b * (n - i + j) / j;
    return b;
}

int CodigoBloque::cotaDistancia(int n, int k) {
    // Mayor d que no descartan las cotas de un código lineal binario (n, k):
    //   Singleton  d <= n - k + 1
    //   Hamming    sum_{i<=t} C(n,i) <= 2^(n-k), t = (d-1)/2; con d par se aplica al código perforado (n-1, k, d-1)
    //   Griesmer   n >= sum_{i<k} ceil(d / 2^i)
    const int r = n - k;
    for (int d = r + 1; d >= 2; --d) {
        int t = (d - 1) / 2, largo = d % 2 ? n : n - 1, paridad = d % 2 ? r : r - 1;
        double esferas = 0;
        for (int i = 0; i <= t; ++i) esferas += binomial(largo, i);
        if (esferas > ldexp(1.0, paridad)) continue;
        int griesmer = 0;
        for (int i = 0; i < k; ++i) griesmer += i < 30 ? (d + (1 << i) - 1) >> i : 1;
        if (griesmer > n) continue;
        return d;
    }
    return 1;
}

// Búsqueda en profundidad de las k filas de P (de r bits) para que toda palabra no nula tenga peso >= objetivo.
// Se guardan las sumas de las filas ya elegidas: al agregar la fila v, las palabras nuevas son v + (esas sumas).
struct BusquedaP {
    int k = 0, objetivo = 0;
    int64_t presupuesto = 0;
    bool sinPresupuesto = false;      // la búsqueda se cortó: un fracaso ya no prueba que no exista el código
    vector<uint32_t> candidatos, filas, sumas{0};
    vector<uint8_t> pesoMensaje{0};

    bool buscar(size_t desde) {
        if ((int)filas.size() == k) return true;
        const size_t combinaciones = sumas.size();
        for (size_t idx = desde; idx < candidatos.size(); ++idx) {
            if (presupuesto <= 0) {
                sinPresupuesto = true;
                return false;
            }
            presupuesto -= (int64_t)combinaciones;
            uint32_t v = candidatos[idx];
            bool sirve = true;
            for (size_t j = 0; j < combinaciones && sirve; ++j)
                sirve = __builtin_popcount(v ^ sumas[j]) + pesoMensaje[j] + 1 >= objetivo;
            if (!sirve) continue;
            filas.push_back(v);
            for (size_t j = 0; j < combinaciones; ++j) {
                sumas.push_back(v ^ sumas[j]);
                pesoMensaje.push_back(pesoMensaje[j] + 1);
            }
            // Las filas sólo pueden repetirse si basta d = 2 (dos filas iguales dan una palabra de peso 2)
            if (buscar(objetivo <= 2 ? idx : idx + 1)) return true;
            sumas.resize(combinaciones);
            pesoMensaje.resize(combinaciones);
            filas.pop_back();
        }
        return false;
    }
};

// Devuelve las filas de P; 'optimo' queda en true si se probó que ningún código (n, k) tiene mayor d_min
// (porque d alcanza la cota o porque la búsqueda recorrió todas las matrices con d mayor sin encontrar ninguna)
static vector<uint32_t> buscarP(int n, int k, bool& optimo) {
    const int r = n - k;
    optimo = true;
    for (int objetivo = CodigoBloque::cotaDistancia(n, k); objetivo >= 3; --objetivo) {
        BusquedaP b;
        b.k = k;
        b.objetivo = objetivo;
        b.presupuesto = 20000000;
        for (uint32_t v = 1; v < (1u << r); ++v)
            if (__builtin_popcount(v) + 1 >= objetivo) b.candidatos.push_back(v);
        // Primero las filas de menor peso: dejan espacio para que las sumas sigan lejos de cero
        stable_sort(b.candidatos.begin(), b.candidatos.end(), [](uint32_t a, uint32_t c) {
            int pa = __builtin_popcount(a), pc = __builtin_popcount(c);
            return pa != pc ? pa < pc : a > c;
        });
        if (b.buscar(0)) return b.filas;
        if (b.sinPresupuesto) optimo = false;
    }
    // d = 2 se logra con cualquier fila no nula; se usan filas distintas, de mayor peso primero, mientras alcancen
    vector<uint32_t> candidatos;
    for (uint32_t v = 1; v < (1u << r); ++v) candidatos.push_back(v);
    stable_sort(candidatos.begin(), candidatos.end(), [](uint32_t a, uint32_t c) {
        int pa = __builtin_popcount(a), pc = __builtin_popcount(c);
        return pa != pc ? pa > pc : a > c;
    });
    vector<uint32_t> filas;
    for (int i = 0; i < k; ++i) filas.push_back(candidatos[i % candidatos.size()]);
    return filas;
}

bool CodigoBloque::crear(const string& especificacion, CodigoBloque& c, string& error) {
    // Nombres anteriores: equivalen a dar n y k
    string esp = especificacion;
    if (esp == "hamming74") esp = "7,4";
    else if (esp == "repeticion51") esp = "5,1";
    else if (esp == "repeticion31") esp = "3,1";

    c = CodigoBloque();
    string filas;
    size_t coma = esp.find(',');
    bool porNK = esp.rfind("P:", 0) != 0 && coma != string::npos && coma > 0 && coma + 1 < esp.size() &&
                 esp.find_first_not_of("0123456789,") == string::npos && esp.find(',', coma + 1) == string::npos;
    if (porNK) {
        int n = stoi(esp.substr(0, coma)), k = stoi(esp.substr(coma + 1));
        if (n > 24 || k < 1 || k >= n || k > 20 || n - k > 16) {
            error = "Con n y k: 1 <= k < n <= 24, k <= 20 y n - k <= 16";
            return false;
        }
        for (uint32_t v : buscarP(n, k, c.dMejorProbado)) filas += (filas.empty() ? "" : ",") + bitsDe(v, n - k);
        c.construccion = "busqueda";
    } else if (esp.rfind("P:", 0) == 0) {
        filas = esp.substr(2);
        c.construccion = "matriz";
    } else {
        error = "Código desconocido: " + especificacion + " (use n,k como 7,4 o P:filas como P:110,011,111,101)";
        return false;
    }

    // Filas de P: cadenas de 0 y 1 separadas por coma, punto y coma o espacio
    int columnas = -1;
    string fila;
    for (size_t i = 0; i <= filas.size(); ++i) {
        char ch = i < filas.size() ? filas[i] : ',';
        if (ch == '0' || ch == '1') fila += ch;
        else if (ch == ',' || ch == ';' || ch == ' ') {
            if (fila.empty()) continue;
            if (columnas >= 0 && (int)fila.size() != columnas) {
                error = "Todas las filas de P deben tener la misma cantidad de columnas (n - k)";
                return false;
            }
            columnas = (int)fila.size();
            uint32_t v = 0;
            for (char b : fila) v = (v << 1) | (uint32_t)(b - '0');
            c.P.push_back(v);
            fila.clear();
        } else {
            error = string("Carácter no válido en la matriz P: '") + ch + "'";
            return false;
        }
    }
    c.k = (int)c.P.size();
    c.n = c.k + max(columnas, 0);
    const int r = c.n - c.k;
    if (c.k < 1 || r < 1 || c.n > 24 || c.k > 20 || r > 16) {
        error = "La matriz P debe tener entre 1 y 20 filas (k) y entre 1 y 16 columnas (n - k), con n <= 24";
        return false;
    }
    c.dCota = cotaDistancia(c.n, c.k);
    if (c.construccion == "matriz") {  // para comparar: la mejor P que encuentra la búsqueda con estos n y k
        CodigoBloque mejor;
        string e;
        if (crear(to_string(c.n) + "," + to_string(c.k), mejor, e)) {
            c.dMejor = mejor.dMin;
            c.dMejorProbado = mejor.dMejorProbado;
        }
    }

    // G = [P | I_k]: la fila i lleva la fila i de P y un 1 en la posición n-k+i
    for (int i = 0; i < c.k; ++i) c.G.push_back((c.P[i] << c.k) | (1u << (c.k - 1 - i)));
    // H = [I_{n-k} | P^T]: la fila j lleva un 1 en la posición j y la columna j de P
    for (int j = 0; j < r; ++j) {
        uint32_t columna = 0;
        for (int i = 0; i < c.k; ++i) columna = (columna << 1) | ((c.P[i] >> (r - 1 - j)) & 1);
        c.H.push_back((1u << (c.n - 1 - j)) | columna);
    }

    // Todas las palabras del código: c = m G (suma módulo 2 de las filas de G donde m_i = 1)
    c.tablaCodificacion.assign(1u << c.k, 0);
    c.pesos.assign(c.n + 1, 0);
    c.pesos[0] = 1;
    c.dMin = c.n + 1;
    for (uint32_t m = 1; m < (1u << c.k); ++m) {
        int i = __builtin_ctz(m);  // m difiere de m sin su bit más bajo sólo en la fila k-1-i
        c.tablaCodificacion[m] = c.tablaCodificacion[m & (m - 1)] ^ c.G[c.k - 1 - i];
        int w = __builtin_popcount(c.tablaCodificacion[m]);
        c.pesos[w]++;
        c.dMin = min(c.dMin, w);
    }
    if (c.k == 0 || c.dMin > c.n) c.dMin = 0;
    if (c.construccion != "matriz") c.dMejor = c.dMin;
    c.t = max(0, (c.dMin - 1) / 2);

    const string nk = "(" + to_string(c.n) + "," + to_string(c.k) + ")";
    if (c.construccion == "matriz") c.nombre = "Personalizado " + nk;
    else if (c.k == 1 && c.dMin == c.n) c.nombre = "Repetición " + nk;
    else if (r == 1) c.nombre = "Paridad simple " + nk;
    else if (c.n == (1 << r) - 1 && c.dMin == 3) c.nombre = "Hamming " + nk;
    else if (c.n == (1 << (r - 1)) && c.dMin == 4) c.nombre = "Hamming extendido " + nk;
    else if (c.n == 23 && c.k == 12 && c.dMin == 7) c.nombre = "Golay " + nk;
    else if (c.n == 24 && c.k == 12 && c.dMin == 8) c.nombre = "Golay extendido " + nk;
    else c.nombre = "Código " + nk;

    // Tabla de síndromes: se recorren los patrones de error de menor a mayor peso; el primero que da
    // cada síndrome es el líder de su clase lateral (el error más probable con p < 1/2)
    const uint32_t totalSindromes = 1u << r;
    c.lider.assign(totalSindromes, 0);
    c.lideresPorPeso.assign(c.n + 1, 0);
    vector<bool> asignado(totalSindromes, false);
    uint32_t asignados = 0;
    for (int w = 0; w <= c.n && asignados < totalSindromes; ++w) {
        for (uint32_t e = (1u << w) - 1; e < (1u << c.n) && asignados < totalSindromes; e = siguienteCombinacion(e)) {
            uint32_t s = c.sindrome(e);
            if (!asignado[s]) {
                asignado[s] = true;
                c.lider[s] = e;
                c.lideresPorPeso[w]++;
                ++asignados;
            }
            if (w == 0) break;
        }
    }
    return true;
}

uint32_t CodigoBloque::sindrome(uint32_t r) const {
    // s_j = suma módulo 2 de r_i h_{j,i}: paridad de r AND (fila j de H)
    uint32_t s = 0;
    for (uint32_t fila : H) s = (s << 1) | (uint32_t)__builtin_parity(r & fila);
    return s;
}

bool CodigoBloque::perfecto() const {
    double esferas = 0;
    for (int i = 0; i <= t; ++i) esferas += binomial(n, i);
    return esferas == ldexp(1.0, n - k);
}

double CodigoBloque::probabilidadCorrecto(double p) const {
    // El bloque queda bien si el error del canal es justamente el líder de su clase lateral
    double total = 0;
    for (int w = 0; w <= n; ++w) total += lideresPorPeso[w] * pow(p, w) * pow(1 - p, n - w);
    return min(1.0, total);
}

double CodigoBloque::probabilidadNoDetectado(double p) const {
    // El error no se detecta si e es una palabra no nula del código (s = 0)
    double total = 0;
    for (int w = 1; w <= n; ++w) total += pesos[w] * pow(p, w) * pow(1 - p, n - w);
    return total;
}

// Errores de un BSC sobre el flujo que se va transmitiendo: en vez de sortear cada bit se salta
// al siguiente bit erróneo con una distribución geométrica (igual que Canal::transmitirBSC)
class GeneradorErrores {
    mt19937_64 gen;
    uniform_real_distribution<double> uniforme{0.0, 1.0};
    double logQ = 0;
    uint64_t posicion = 0, siguiente = UINT64_MAX;

    uint64_t salto() {
        double u = uniforme(gen);
        if (u <= 0) u = 1e-300;
        double s = floor(log(u) / logQ);
        return s > 1e18 ? (uint64_t)1e18 : (uint64_t)s;
    }

public:
    GeneradorErrores(double p, uint64_t semilla) : gen(semilla) {
        if (p > 0) {
            logQ = p < 1 ? log1p(-p) : -INFINITY;
            siguiente = salto();
        }
    }

    // Patrón de error e para los próximos n bits (bit más significativo = primero transmitido)
    uint32_t patron(int n) {
        uint32_t e = 0;
        while (siguiente < posicion + (uint64_t)n) {
            e |= 1u << (n - 1 - (int)(siguiente - posicion));
            siguiente += 1 + salto();
        }
        posicion += n;
        return e;
    }
};

// Lee n <= 32 bits del flujo desde 'pos'; lo que pasa de 'total' se completa con ceros
static uint32_t leerBits(const vector<uint8_t>& v, uint64_t pos, int n, uint64_t total) {
    uint64_t b = pos >> 3, w = 0;
    int desplazamiento = (int)(pos & 7);
    for (int i = 0; i < 5; ++i) w = (w << 8) | (b + i < v.size() ? v[b + i] : 0);
    uint32_t valor = (uint32_t)((w >> (40 - desplazamiento - n)) & ((1ULL << n) - 1));
    if (pos + n > total) {
        int sobrantes = (int)(pos + n - total);
        valor &= ~((1u << sobrantes) - 1);
    }
    return valor;
}

static void cerrarRecuperada(EscritorBits& w, uint64_t bits, vector<uint8_t>& recuperada) {
    w.cerrar();
    recuperada = move(w.bytes);
    recuperada.resize((bits + 7) / 8, 0);
    if (bits % 8) recuperada.back() &= (uint8_t)(0xFF << (8 - bits % 8));
}

ResultadoControl ControlError::fec(const CodigoBloque& cod, const vector<uint8_t>& carga, uint64_t bits,
                                   double p, uint64_t semilla, vector<uint8_t>& recuperada) {
    ResultadoControl res;
    const int n = cod.n, k = cod.k;
    const uint32_t mascaraK = (1u << k) - 1;
    res.bitsMensaje = bits;
    res.bloques = (bits + k - 1) / k;
    res.relleno = res.bloques * k - bits;
    res.transmisiones = res.bloques;
    res.bitsEnviados = res.bloques * n;

    GeneradorErrores canal(p, semilla);
    EscritorBits salida;
    salida.bytes.reserve((bits + 7) / 8 + 8);
    size_t conError = 0;
    for (uint64_t i = 0; i < res.bloques; ++i) {
        uint32_t m = leerBits(carga, i * k, k, bits);
        uint32_t c = cod.codificar(m);              // c = m G
        uint32_t e = canal.patron(n);
        uint32_t r = c ^ e;                         // r = c + e
        uint32_t s = cod.sindrome(r);               // s = r H^T
        uint32_t eEst = cod.lider[s];
        uint32_t cEst = r ^ eEst;                   // c^ = r + e^
        uint32_t mEst = cEst & mascaraK;            // el mensaje son los últimos k bits de c = [b | m]
        salida.escribir(mEst, k);

        res.erroresCanal += __builtin_popcount(e);
        res.bitsCorregidos += __builtin_popcount(eEst);
        if (e) res.bloquesConError++;
        if (s) res.bloquesDetectados++;
        if (e && !s) res.bloquesNoDetectados++;
        if (e && cEst == c) res.bloquesCorregidos++;
        if (s && cEst != c) res.bloquesMalCorregidos++;

        bool mostrar = i < 4 || (e && conError < 12);
        if (mostrar) {
            if (e) conError++;
            res.muestra.push_back({i, m, c, e, r, s, eEst, cEst, mEst, 1});
        }
    }
    cerrarRecuperada(salida, bits, recuperada);
    res.IXYbit = informacionMutuaBits(carga, recuperada, bits, res.bitsErroneos);
    return res;
}

ResultadoControl ControlError::arq(const CodigoBloque& cod, const vector<uint8_t>& carga, uint64_t bits,
                                   double p, uint64_t semilla, int maxIntentos, vector<uint8_t>& recuperada) {
    ResultadoControl res;
    const int n = cod.n, k = cod.k;
    const uint32_t mascaraK = (1u << k) - 1;
    res.bitsMensaje = bits;
    res.bloques = (bits + k - 1) / k;
    res.relleno = res.bloques * k - bits;

    GeneradorErrores canal(p, semilla);
    EscritorBits salida;
    salida.bytes.reserve((bits + 7) / 8 + 8);
    size_t especiales = 0;
    for (uint64_t i = 0; i < res.bloques; ++i) {
        uint32_t m = leerBits(carga, i * k, k, bits);
        uint32_t c = cod.codificar(m);
        uint32_t e, r, s, e1 = 0, r1 = 0, s1 = 0;
        int intentos = 0;
        do {  // se envía el bloque; con s != 0 el receptor responde NAK y se repite
            ++intentos;
            e = canal.patron(n);
            r = c ^ e;
            s = cod.sindrome(r);
            if (intentos == 1) { e1 = e; r1 = r; s1 = s; }
            res.erroresCanal += __builtin_popcount(e);
            if (e) res.bloquesConError++;
            if (s) res.bloquesDetectados++;
            else if (e) res.bloquesNoDetectados++;
        } while (s != 0 && intentos < maxIntentos);
        res.transmisiones += intentos;
        if (s != 0) res.bloquesFallidos++;
        uint32_t mEst = r & mascaraK;  // se entrega lo último recibido, sin corregir
        salida.escribir(mEst, k);

        bool mostrar = i < 3 || ((intentos > 1 || (e && !s)) && especiales < 12);
        if (mostrar) {
            if (i >= 3) especiales++;
            res.muestra.push_back({i, m, c, e1, r1, s1, 0, r, mEst, intentos, e, s});
        }
    }
    res.bitsEnviados = res.transmisiones * n;
    cerrarRecuperada(salida, bits, recuperada);
    res.IXYbit = informacionMutuaBits(carga, recuperada, bits, res.bitsErroneos);
    return res;
}

double ControlError::informacionMutuaBits(const vector<uint8_t>& x, const vector<uint8_t>& y, uint64_t bits,
                                          uint64_t& diferentes) {
    // Conteos de los pares (x, y); los bits de relleno valen 0 en los dos flujos
    uint64_t n11 = 0, n10 = 0, n01 = 0;
    size_t bytes = min<size_t>({x.size(), y.size(), (size_t)((bits + 7) / 8)});
    for (size_t i = 0; i < bytes; ++i) {
        n11 += __builtin_popcount(x[i] & y[i]);
        n10 += __builtin_popcount(x[i] & (uint8_t)~y[i]);
        n01 += __builtin_popcount((uint8_t)~x[i] & y[i]);
    }
    diferentes = n10 + n01;
    if (bits == 0) return 0;
    double total = (double)bits;
    double conjunta[2][2] = {{(bits - n11 - n10 - n01) / total, n01 / total}, {n10 / total, n11 / total}};
    double px[2] = {conjunta[0][0] + conjunta[0][1], conjunta[1][0] + conjunta[1][1]};
    double py[2] = {conjunta[0][0] + conjunta[1][0], conjunta[0][1] + conjunta[1][1]};
    double I = 0;
    for (int a = 0; a < 2; ++a)
        for (int b = 0; b < 2; ++b)
            if (conjunta[a][b] > 0) I += conjunta[a][b] * log2(conjunta[a][b] / (px[a] * py[b]));
    return max(0.0, I);
}
