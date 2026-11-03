#ifndef BITS_H
#define BITS_H

#include <cstdint>
#include <vector>

// Escritura de un flujo de bits empaquetado (bit más significativo primero)
class EscritorBits {
private:
    uint64_t acumulador = 0;
    int bitsAcumulados = 0;

public:
    std::vector<uint8_t> bytes;
    uint64_t totalBits = 0;

    void escribir(uint64_t valor, int n) {
        if (n <= 0) return;
        if (n > 32) {
            escribir(valor >> 32, n - 32);
            valor &= 0xFFFFFFFFULL;
            n = 32;
        }
        acumulador = (acumulador << n) | (valor & ((1ULL << n) - 1));
        bitsAcumulados += n;
        totalBits += n;
        while (bitsAcumulados >= 8) {
            bitsAcumulados -= 8;
            bytes.push_back((uint8_t)(acumulador >> bitsAcumulados));
        }
        acumulador &= (1ULL << bitsAcumulados) - 1;
    }

    // Completa el último byte con ceros
    void cerrar() {
        if (bitsAcumulados > 0) {
            bytes.push_back((uint8_t)(acumulador << (8 - bitsAcumulados)));
            acumulador = 0;
            bitsAcumulados = 0;
        }
    }
};

// Lectura de un flujo de bits; si se acaba marca 'agotado' en lugar de fallar
class LectorBits {
private:
    const uint8_t* datos;
    uint64_t totalBits;
    uint64_t posicion = 0;

public:
    bool agotado = false;

    LectorBits(const uint8_t* datos, uint64_t totalBits) : datos(datos), totalBits(totalBits) {}

    int bit() {
        if (posicion >= totalBits) {
            agotado = true;
            return 0;
        }
        int b = (datos[posicion >> 3] >> (7 - (posicion & 7))) & 1;
        ++posicion;
        return b;
    }

    uint64_t leer(int n) {
        uint64_t valor = 0;
        for (int i = 0; i < n; ++i) valor = (valor << 1) | (uint64_t)bit();
        return valor;
    }
};

#endif
