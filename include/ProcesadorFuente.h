#ifndef PROCESADOR_FUENTE_H
#define PROCESADOR_FUENTE_H

#include <cstdint>
#include <string>
#include <vector>

// Fuente discreta: el archivo visto como una secuencia de símbolos de N bits
struct Fuente {
    std::string ruta;
    std::string tipo;     // audio, video, imagen, texto, ...
    std::string formato;  // WAV, MP3, PNG, ...
    int nBits = 8;
    size_t bytesPorSimbolo = 1;
    uint64_t tamanoOriginal = 0;          // bytes reales del archivo
    std::vector<unsigned char> datos;     // contenido rellenado con ceros hasta un múltiplo de N bits
    std::vector<uint32_t> ids;            // secuencia de símbolos (índice en el alfabeto)
    std::vector<uint64_t> primerBloque;   // bloque donde aparece por primera vez cada símbolo
    std::vector<uint64_t> frecuencias;    // ocurrencias de cada símbolo del alfabeto

    size_t numSimbolos() const { return ids.size(); }
    size_t tamAlfabeto() const { return frecuencias.size(); }
    const unsigned char* simbolo(uint32_t id) const { return datos.data() + primerBloque[id] * bytesPorSimbolo; }
};

class ProcesadorFuente {
public:
    // Lectura estricta en modo binario de cualquier archivo
    bool leerArchivo(const std::string& ruta, std::vector<unsigned char>& contenido, std::string& error);

    // Divide el contenido en bloques exactos de N bits y construye el alfabeto de la fuente
    Fuente fragmentar(const std::string& ruta, const std::vector<unsigned char>& contenido, int nBits);

    // Probabilidad P(x_k) = frecuencia / total de cada símbolo del alfabeto
    std::vector<double> calcularProbabilidades(const Fuente& fuente);

    // Identifica audio / video / imagen / texto por la firma del archivo
    static void detectarTipo(const std::string& ruta, const std::vector<unsigned char>& contenido,
                             std::string& tipo, std::string& formato);
};

#endif
