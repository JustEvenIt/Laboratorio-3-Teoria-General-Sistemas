#ifndef SIMULACION_H
#define SIMULACION_H

#include <cstdint>
#include <string>

// Simulación de enlace de bajada 5G NR / 6G con el archivo comprimido:
//   cifrado AES-128-CTR -> bloques de transporte (TS 38.214) -> CRC y segmentación (TS 38.212) -> código de canal
//   -> adaptación de tasa -> QPSK..1024QAM (TS 38.211) -> capas MIMO -> canal con ruido gaussiano (AWGN) -> LLR
//   -> decodificación -> CRC -> HARQ -> descifrado -> descompresión
// Se comparan tres caminos con el mismo canal: LDPC de 5G NR, un código de bloque (n, k) y sin código.
struct OpcionesSimulacion {
    std::string escenario = "embb";   // embb | urllc | 6g
    double snrDb = 20;                // relación señal a ruido total en el receptor (dB)
    std::string fec = "7,4";          // código de bloque para comparar (n,k o P:filas; "ninguno" lo omite)
    int harqMax = 4;                  // envíos máximos por bloque de transporte
    int iteraciones = 25;             // iteraciones máximas del decodificador LDPC
    int capas = 0;                    // capas MIMO: 1, 2, 4 u 8 (0: las elige el agente)
    bool cifrar = true;               // AES-128-CTR antes de transmitir
    std::string clave = "2b7e151628aed2a6abf7158809cf4f3c";  // clave AES de 128 bits en hexadecimal
    uint64_t semilla = 42;
    uint64_t maxBytes = 0;            // 0: todo el archivo comprimido
    int paquetes = -1;                // prueba de confiabilidad con paquetes de 32 bytes (-1: valor del escenario)
    int bloquesTraza = 40;            // bloques de transporte que se registran uno por uno (paso a paso y visualización)
    std::string progreso;             // archivo donde se escribe el avance (vacío: no se escribe)
    std::string salida = "data/salida";
    // Curvas BLER frente a SNR
    double snrDesde = 0, snrHasta = 0, snrPaso = 1;
    int bloquesCurva = 100;
};

int ejecutarSimulacion(const std::string& rutaOriginal, const std::string& rutaComprimido, const OpcionesSimulacion& op,
                       const std::string& rutaJson);
int ejecutarCurvas(const OpcionesSimulacion& op, const std::string& rutaJson);
// Sólo la prueba de confiabilidad: mensajes de 32 bytes que deben llegar bien en 1 ms (300 000 si no se indica)
int ejecutarConfiabilidad(const OpcionesSimulacion& op, const std::string& rutaJson);

#endif
