#include "../include/Canal.h"
#include <algorithm>
#include <cmath>
#include <random>

using namespace std;

static double plog(double p) { return p > 0 ? -p * log2(p) : 0.0; }

double Canal::entropiaBinaria(double p) { return plog(p) + plog(1 - p); }

// I(X;Y) = sum P(x,y) log2( P(x,y) / (P(x) P(y)) )
static double informacionMutua(const double conjunta[2][2]) {
    double px[2] = {conjunta[0][0] + conjunta[0][1], conjunta[1][0] + conjunta[1][1]};
    double py[2] = {conjunta[0][0] + conjunta[1][0], conjunta[0][1] + conjunta[1][1]};
    double I = 0;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            if (conjunta[x][y] > 0) I += conjunta[x][y] * log2(conjunta[x][y] / (px[x] * py[y]));
    return max(0.0, I);
}

ResultadoCanal Canal::transmitirBSC(vector<uint8_t>& bytes, uint64_t nBits, double p, uint64_t semilla) {
    ResultadoCanal r;
    p = min(1.0, max(0.0, p));
    r.p = p;
    r.bits = nBits;
    if (nBits == 0) return r;

    // Los bits de relleno del último byte son ceros, así que no alteran el conteo
    for (uint8_t b : bytes) r.unosX += __builtin_popcount(b);

    // Errores independientes (sin memoria): se salta directamente al siguiente bit erróneo
    // con una distribución geométrica, en vez de sortear cada bit
    if (p > 0) {
        mt19937_64 gen(semilla);
        uniform_real_distribution<double> uniforme(0.0, 1.0);
        double logQ = log1p(-p);
        uint64_t pos = 0;
        while (true) {
            uint64_t salto = 0;
            if (p < 1) {
                double u = uniforme(gen);
                if (u <= 0) u = 1e-300;
                double s = floor(log(u) / logQ);
                if (s >= (double)(nBits - pos)) break;
                salto = (uint64_t)s;
            }
            pos += salto;
            if (pos >= nBits) break;
            uint8_t mascara = (uint8_t)(0x80 >> (pos & 7));
            if (bytes[pos >> 3] & mascara) r.errores10++;
            else r.errores01++;
            bytes[pos >> 3] ^= mascara;
            ++pos;
        }
    }

    // Modelo teórico
    r.px[1] = (double)r.unosX / nBits;
    r.px[0] = 1 - r.px[1];
    r.transicion[0][0] = r.transicion[1][1] = 1 - p;
    r.transicion[0][1] = r.transicion[1][0] = p;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y) r.conjunta[x][y] = r.transicion[x][y] * r.px[x];
    r.py[0] = r.conjunta[0][0] + r.conjunta[1][0];
    r.py[1] = r.conjunta[0][1] + r.conjunta[1][1];
    r.HX = entropiaBinaria(r.px[1]);
    r.HY = entropiaBinaria(r.py[1]);
    r.HYdadoX = entropiaBinaria(p);
    r.IXY = max(0.0, r.HY - r.HYdadoX);
    r.HXdadoY = max(0.0, r.HX - r.IXY);
    r.capacidad = 1 - entropiaBinaria(p);

    // Medición sobre la transmisión simulada
    uint64_t ceros = nBits - r.unosX;
    double conteo[2][2] = {{(double)(ceros - r.errores01), (double)r.errores01},
                           {(double)r.errores10, (double)(r.unosX - r.errores10)}};
    double conjuntaMedida[2][2];
    for (int x = 0; x < 2; ++x) {
        double fila = conteo[x][0] + conteo[x][1];
        for (int y = 0; y < 2; ++y) {
            r.transicionMedida[x][y] = fila > 0 ? conteo[x][y] / fila : 0;
            conjuntaMedida[x][y] = conteo[x][y] / nBits;
        }
    }
    r.IXYmedida = informacionMutua(conjuntaMedida);
    r.tasaErrorBit = (double)(r.errores01 + r.errores10) / nBits;
    return r;
}
