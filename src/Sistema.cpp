#include "../include/Sistema.h"
#include <algorithm>
#include <cmath>

using namespace std;

MetricasFuente Sistema::analizarFuente(const Fuente& f, const vector<double>& P) {
    MetricasFuente m;
    m.n = f.numSimbolos();
    m.M = f.tamAlfabeto();
    double pMax = 0, pMin = 1;
    for (double p : P) {
        if (p > 0) m.entropia -= p * log2(p);
        pMax = max(pMax, p);
        pMin = min(pMin, p);
    }
    m.entropiaMaxima = f.nBits;
    m.entropiaMaxAlfabeto = m.M > 0 ? log2((double)m.M) : 0;
    // Con U uniforme sobre K símbolos: D(P||U) = sum p log2(p K) = log2 K - H
    m.klUniformeN = max(0.0, m.entropiaMaxima - m.entropia);
    m.klUniformeM = max(0.0, m.entropiaMaxAlfabeto - m.entropia);
    m.redundancia = 1 - m.entropia / m.entropiaMaxima;
    m.infoMin = pMax > 0 ? -log2(pMax) : 0;
    m.infoMax = pMin > 0 ? -log2(pMin) : 0;
    m.infoTotalBits = m.entropia * m.n;
    return m;
}

MetricasCodigo Sistema::metricasCodigo(const string& nombre, const vector<double>& P,
                                       const vector<uint8_t>& longitudes, double entropia) {
    MetricasCodigo m;
    m.nombre = nombre;
    m.longitudMin = 255;
    // Longitud media: L = sum P_k L_k
    for (size_t k = 0; k < P.size(); ++k) {
        m.longitudMedia += P[k] * longitudes[k];
        m.kraft += ldexp(1.0, -longitudes[k]);
        m.longitudMin = min(m.longitudMin, (int)longitudes[k]);
        m.longitudMax = max(m.longitudMax, (int)longitudes[k]);
    }
    // Varianza de la codificación: sigma^2 = sum_{k=0}^{K-1} P_k (L_k - L)^2
    for (size_t k = 0; k < P.size(); ++k) {
        double d = longitudes[k] - m.longitudMedia;
        m.varianza += P[k] * d * d;
    }
    m.desviacion = sqrt(m.varianza);
    m.eficiencia = m.longitudMedia > 0 ? entropia / m.longitudMedia : 0;
    return m;
}

static double entropiaDeConteos(const vector<uint64_t>& conteos, double total) {
    double H = 0;
    for (uint64_t c : conteos)
        if (c > 0) H -= (c / total) * log2(c / total);
    return H;
}

MetricasMutua Sistema::informacionMutua(const vector<uint32_t>& X, const vector<uint32_t>& Y,
                                        const vector<uint64_t>& frecuenciasX, double HX) {
    MetricasMutua m;
    const size_t M = frecuenciasX.size();
    const double n = (double)X.size();
    m.HX = HX;
    if (X.empty()) return m;

    // P(y) incluye el símbolo de borrado (índice M)
    vector<uint64_t> frecY(M + 1, 0);
    for (uint32_t y : Y) frecY[y]++;
    m.HY = entropiaDeConteos(frecY, n);

    // P(x,y): con alfabetos pequeños se usa la matriz completa M x (M+1);
    // si no, la diagonal (x,x) más una tabla sólo para los errores
    if ((uint64_t)M * (M + 1) <= (1ULL << 24)) {
        vector<uint64_t> conjunta((size_t)M * (M + 1), 0);
        for (size_t i = 0; i < X.size(); ++i) {
            conjunta[(size_t)X[i] * (M + 1) + Y[i]]++;
            if (X[i] != Y[i]) m.errores++;
        }
        m.HXY = entropiaDeConteos(conjunta, n);
    } else {
        vector<uint64_t> diagonal(frecuenciasX);
        vector<uint64_t> fueraDiagonal;  // pares (x,y) con x != y; se ordenan para contarlos
        for (size_t i = 0; i < X.size(); ++i) {
            if (X[i] != Y[i]) {
                diagonal[X[i]]--;
                fueraDiagonal.push_back(((uint64_t)X[i] << 32) | Y[i]);
            }
        }
        m.errores = fueraDiagonal.size();
        m.HXY = entropiaDeConteos(diagonal, n);
        sort(fueraDiagonal.begin(), fueraDiagonal.end());
        for (size_t i = 0; i < fueraDiagonal.size();) {
            size_t j = i;
            while (j < fueraDiagonal.size() && fueraDiagonal[j] == fueraDiagonal[i]) ++j;
            double q = (j - i) / n;
            m.HXY -= q * log2(q);
            i = j;
        }
    }

    // I(X;Y) = H(X) + H(Y) - H(X,Y)
    m.IXY = max(0.0, m.HX + m.HY - m.HXY);
    m.HXdadoY = max(0.0, m.HX - m.IXY);
    m.tasaErrorSimbolo = m.errores / n;
    return m;
}
