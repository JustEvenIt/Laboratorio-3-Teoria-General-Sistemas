#ifndef CIFRADO_H
#define CIFRADO_H

#include <cstddef>
#include <cstdint>

// AES-128 (FIPS-197) en modo contador (CTR, NIST SP 800-38A): es el algoritmo del cifrado 128-NEA2 de 5G.
// En modo contador se cifra una secuencia de bloques contador y el resultado se suma (XOR) a los datos,
// así que cifrar y descifrar son la misma operación y un bit errado sólo daña ese mismo bit.
class AES128 {
public:
    explicit AES128(const uint8_t clave[16]);
    void cifrarBloque(const uint8_t entrada[16], uint8_t salida[16]) const;

private:
    uint8_t subclaves[176];  // 11 subclaves de 16 bytes
};

// Cifra (o descifra) 'datos' en el lugar. 'contador' es el bloque contador inicial; se incrementa en sus
// 64 bits bajos. Con archivos grandes reparte el trabajo entre los núcleos (el resultado no cambia).
void cifrarCTR(const uint8_t clave[16], const uint8_t contador[16], uint8_t* datos, size_t n);

// Bloque contador inicial como en 128-NEA2 (TS 33.501): COUNT (32 bits) | BEARER (5) | DIRECTION (1) | ceros
void contadorNEA2(uint32_t count, uint8_t bearer, uint8_t direccion, uint8_t contador[16]);

#endif
