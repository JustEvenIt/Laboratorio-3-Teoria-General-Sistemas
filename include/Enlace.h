#ifndef ENLACE_H
#define ENLACE_H

#include <cstdint>
#include <string>

// Transmisión del archivo por un enlace descendente 5G NR (3GPP Release 15-17) y comparación con los
// requisitos de IMT-2020 (5G, ITU-R M.2410) y los objetivos de IMT-2030 (6G, ITU-R M.2160).

struct ParametrosEnlace {
    double snrDb = 20;          // SNR por capa en el receptor
    int capas = 4;              // capas MIMO (1, 2, 4 u 8; con más de 4 se usan dos palabras de código)
    std::string servicio = "embb";  // embb | urllc
    int tablaMCS = 256;         // 256 (TS 38.214 Tabla 5.1.3.1-2) o 64 (Tabla 5.1.3.1-1)
};

// Tamaño del bloque de transporte, TS 38.214 §5.1.3.2
int64_t tbsNR(int nPRB, int simbolos, int dmrsPorPRB, int overheadPorPRB, double R, int Qm, int capas);

// Codificación de canal del bloque de transporte, TS 38.212 §5.2.2 y §7.2.2
struct SegmentacionNR {
    int grafoBase;      // 1 o 2
    int crcTB;          // bits de CRC del bloque de transporte (16 o 24)
    int64_t C;          // bloques de código
    int crcCB;          // bits de CRC de cada bloque de código (0 o 24)
    int64_t Kprima;     // bits por bloque de código antes del relleno
    int Zc;             // tamaño de elevación (lifting size)
    int64_t K;          // bits por bloque de código a la entrada del LDPC
    int64_t relleno;    // bits de relleno (filler) por bloque de código
    int64_t N;          // bits por bloque de código a la salida del LDPC (66·Zc o 50·Zc)
};
SegmentacionNR segmentarNR(int64_t A, double R);

int ejecutarEnlace(const std::string& ruta, uint64_t bytesOriginal, uint64_t bytesComprimido,
                   const ParametrosEnlace& p, const std::string& rutaJson);

#endif
