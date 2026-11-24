#ifndef COMPRESOR_H
#define COMPRESOR_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "ProcesadorFuente.h"

enum Metodo : uint8_t { METODO_UNIFORME = 0, METODO_HUFFMAN = 1, METODO_LZW = 2 };

std::string nombreMetodo(int metodo);

// Un paso del algoritmo LZW, para mostrar el proceso
struct PasoLZW {
    uint64_t inicio;        // posición en la secuencia donde empieza el patrón emitido
    uint64_t largo;         // símbolos que cubre el patrón
    uint32_t siguiente;     // símbolo que rompió la coincidencia (UINT32_MAX al final)
    uint32_t codigo;        // código emitido
    int ancho;              // bits usados para el código
    int64_t nuevaEntrada;   // código asignado al patrón + siguiente (-1 si no se agregó)
};

struct Codificado {
    int metodo = METODO_UNIFORME;
    std::vector<uint8_t> carga;       // flujo de bits empaquetado
    uint64_t bitsCarga = 0;
    int anchoUniforme = 0;            // Uniforme: bits por símbolo
    std::vector<uint8_t> longitudes;  // Huffman: longitud L_k del código de cada símbolo
    uint64_t capDiccionario = 0;      // LZW: tamaño máximo del diccionario
    uint64_t codigosEmitidos = 0;     // LZW
    double varianzaLZW = 0;           // LZW: varianza de bits por símbolo de la fuente
    std::vector<PasoLZW> traza;
};

class Compresor {
public:
    // Bits necesarios para representar 'valores' códigos distintos (mínimo 1)
    static int bitsPara(uint64_t valores);

    // Longitudes de un código de Huffman. Con minimaVarianza los empates se resuelven a favor
    // de los símbolos originales (el nodo combinado se ubica lo más arriba posible).
    static std::vector<uint8_t> longitudesHuffman(const std::vector<uint64_t>& frecuencias, bool minimaVarianza);
    static std::vector<uint64_t> codigosCanonicos(const std::vector<uint8_t>& longitudes);

    Codificado codificarUniforme(const Fuente& f);
    Codificado codificarHuffman(const Fuente& f);
    Codificado codificarLZW(const Fuente& f, uint64_t entradasExtra, size_t pasosTraza);

    // Decodifica exactamente n símbolos. Los códigos inválidos (por errores del canal)
    // se convierten en el símbolo de borrado M, en lugar de detener el proceso.
    std::vector<uint32_t> decodificar(const Codificado& c, uint64_t n, uint32_t M);
    // Igual, pero entrega cada símbolo a 'emitir' en orden, sin guardar la secuencia completa
    // (con archivos grandes, la secuencia ocupa 4 bytes por símbolo). 'c' aporta el método y su tabla.
    void decodificarCon(const Codificado& c, const uint8_t* carga, uint64_t bitsCarga, uint64_t n, uint32_t M,
                        const std::function<void(uint32_t)>& emitir);

    // Archivo comprimido autocontenido: cabecera + alfabeto + tabla del código + carga
    uint64_t tamanoCabecera(const Codificado& c, const Fuente& f);
    std::vector<uint8_t> serializar(const Codificado& c, const Fuente& f);
    bool guardar(const std::string& ruta, const Codificado& c, const Fuente& f);
    // Reconstruye el contenido original usando sólo los bytes del formato comprimido
    // 'maxSalida': tamaño máximo aceptado para el archivo original (protege de una cabecera dañada por el canal)
    bool deserializar(const std::vector<uint8_t>& bytes, std::vector<unsigned char>& salida, std::string& error,
                      uint64_t maxSalida = UINT64_MAX);
    bool descomprimirArchivo(const std::string& ruta, std::vector<unsigned char>& salida, std::string& error);
};

#endif
