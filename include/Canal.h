#ifndef CANAL_H
#define CANAL_H

#include <cstdint>
#include <vector>

// Canal binario simétrico (BSC), discreto y sin memoria:
// X = {0,1}, Y = {0,1}, P(y1|x0) = P(y0|x1) = p, P(y0|x0) = P(y1|x1) = 1 - p
struct ResultadoCanal {
    double p = 0;
    uint64_t bits = 0, unosX = 0;
    uint64_t errores01 = 0, errores10 = 0;  // x=0 recibido como 1, x=1 recibido como 0

    // Distribuciones teóricas a partir de P(x) del flujo codificado y de la matriz de transición
    double px[2] = {0, 0}, py[2] = {0, 0};
    double transicion[2][2] = {{0, 0}, {0, 0}};  // P(y_k | x_j)
    double conjunta[2][2] = {{0, 0}, {0, 0}};    // P(x_j, y_k) = P(y_k | x_j) P(x_j)
    double HX = 0, HY = 0, HYdadoX = 0, HXdadoY = 0, IXY = 0, capacidad = 0;

    // Valores medidos en la transmisión simulada
    double transicionMedida[2][2] = {{0, 0}, {0, 0}};
    double IXYmedida = 0, tasaErrorBit = 0;
};

class Canal {
public:
    static double entropiaBinaria(double p);
    // Invierte cada bit del flujo con probabilidad p (modifica 'bytes')
    static ResultadoCanal transmitirBSC(std::vector<uint8_t>& bytes, uint64_t nBits, double p, uint64_t semilla);
};

#endif
