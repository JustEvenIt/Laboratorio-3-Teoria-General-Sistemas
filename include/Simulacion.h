#ifndef SIMULACION_H
#define SIMULACION_H

#include <cstdint>
#include <string>

// Simulación de enlace de bajada 5G NR / 6G con el archivo comprimido:
//   bloques de transporte (TS 38.214) -> CRC y segmentación (TS 38.212) -> código de canal -> adaptación de tasa
//   -> QPSK..1024QAM (TS 38.211) -> canal con ruido gaussiano (AWGN) -> LLR -> decodificación -> CRC -> HARQ
// Se comparan tres caminos con el mismo canal: LDPC de 5G NR, un código de bloque (n, k) y sin código.
struct OpcionesSimulacion {
    std::string escenario = "embb";   // embb | urllc | 6g
    double snrDb = 20;                // relación señal a ruido del canal AWGN (dB)
    std::string fec = "7,4";          // código de bloque para comparar (n,k o P:filas; "ninguno" lo omite)
    int harqMax = 4;                  // transmisiones máximas por bloque de transporte
    int iteraciones = 25;             // iteraciones máximas del decodificador LDPC
    uint64_t semilla = 42;
    uint64_t maxBytes = 0;            // 0: todo el archivo comprimido
    int paquetes = -1;                // prueba de confiabilidad con paquetes de 32 bytes (-1: valor del escenario)
    std::string salida = "data/salida";
    // Curvas BLER frente a SNR
    double snrDesde = 0, snrHasta = 0, snrPaso = 1;
    int bloquesCurva = 100;
};

int ejecutarSimulacion(const std::string& rutaOriginal, const std::string& rutaComprimido, const OpcionesSimulacion& op,
                       const std::string& rutaJson);
int ejecutarCurvas(const OpcionesSimulacion& op, const std::string& rutaJson);

#endif
