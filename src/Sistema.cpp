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

AcumuladorMutua::AcumuladorMutua(const vector<uint64_t>& frecuenciasX, double HX)
    : M(frecuenciasX.size()), HX(HX), completa((uint64_t)frecuenciasX.size() * (frecuenciasX.size() + 1) <= (1ULL << 24)) {
    frecY.assign(M + 1, 0);  // P(y) incluye el símbolo de borrado (índice M)
    if (completa) conjunta.assign(M * (M + 1), 0);
    else diagonal = frecuenciasX;
}

void AcumuladorMutua::agregar(uint32_t x, uint32_t y) {
    ++n;
    frecY[y]++;
    if (x != y) errores++;
    if (completa) conjunta[(size_t)x * (M + 1) + y]++;
    else if (x != y) {
        diagonal[x]--;
        fueraDiagonal.push_back(((uint64_t)x << 32) | y);
    }
}

MetricasMutua AcumuladorMutua::resultado() {
    MetricasMutua m;
    m.HX = HX;
    if (n == 0) return m;
    const double total = (double)n;
    m.HY = entropiaDeConteos(frecY, total);
    if (completa) m.HXY = entropiaDeConteos(conjunta, total);
    else {
        m.HXY = entropiaDeConteos(diagonal, total);
        sort(fueraDiagonal.begin(), fueraDiagonal.end());
        for (size_t i = 0; i < fueraDiagonal.size();) {
            size_t j = i;
            while (j < fueraDiagonal.size() && fueraDiagonal[j] == fueraDiagonal[i]) ++j;
            double q = (j - i) / total;
            m.HXY -= q * log2(q);
            i = j;
        }
    }
    m.errores = errores;
    m.IXY = max(0.0, m.HX + m.HY - m.HXY);
    m.HXdadoY = max(0.0, m.HX - m.IXY);
    m.tasaErrorSimbolo = errores / total;
    return m;
}

MetricasMutua Sistema::informacionMutua(const vector<uint32_t>& X, const vector<uint32_t>& Y,
                                        const vector<uint64_t>& frecuenciasX, double HX) {
    AcumuladorMutua acumulador(frecuenciasX, HX);
    for (size_t i = 0; i < X.size(); ++i) acumulador.agregar(X[i], Y[i]);
    return acumulador.resultado();
}
