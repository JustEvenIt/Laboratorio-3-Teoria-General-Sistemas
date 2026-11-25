#ifndef PROCESADOR_FUENTE_H
#define PROCESADOR_FUENTE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Secuencia de índices de símbolos guardada con el ancho justo: 1 byte si el alfabeto puede tener hasta 256 símbolos
// (N = 8), 2 bytes hasta 65 536 y 4 bytes en otro caso. Con archivos grandes es lo que más memoria ocupa: con N = 8 y un
// archivo de 700 MB son 700 MB en lugar de 2.8 GB.
class SecuenciaIds {
public:
    // n índices, cada uno menor que maxSimbolos
    void preparar(size_t n, uint64_t maxSimbolos) {
        ancho = maxSimbolos <= 256 ? 1 : maxSimbolos <= 65536 ? 2 : 4;
        tam = n;
        bytes.assign(n * ancho, 0);
    }
    size_t size() const { return tam; }
    bool empty() const { return tam == 0; }
    uint32_t operator[](size_t i) const {
        if (ancho == 1) return bytes[i];
        if (ancho == 2) { uint16_t v; std::memcpy(&v, &bytes[2 * i], 2); return v; }
        uint32_t v;
        std::memcpy(&v, &bytes[4 * i], 4);
        return v;
    }
    void poner(size_t i, uint32_t v) {
        if (ancho == 1) bytes[i] = (uint8_t)v;
        else if (ancho == 2) { const uint16_t w = (uint16_t)v; std::memcpy(&bytes[2 * i], &w, 2); }
        else std::memcpy(&bytes[4 * i], &v, 4);
    }
    int bytesPorIndice() const { return ancho; }
    class Iterador {
    public:
        Iterador(const SecuenciaIds* s, size_t i) : s(s), i(i) {}
        uint32_t operator*() const { return (*s)[i]; }
        Iterador& operator++() { ++i; return *this; }
        bool operator!=(const Iterador& o) const { return i != o.i; }
    private:
        const SecuenciaIds* s;
        size_t i;
    };
    Iterador begin() const { return Iterador(this, 0); }
    Iterador end() const { return Iterador(this, tam); }

private:
    std::vector<uint8_t> bytes;
    size_t tam = 0;
    int ancho = 4;
};

// Fuente discreta: el archivo visto como una secuencia de símbolos de N bits
struct Fuente {
    std::string ruta;
    std::string tipo;     // audio, video, imagen, texto, ...
    std::string formato;  // WAV, MP3, PNG, ...
    int nBits = 8;
    size_t bytesPorSimbolo = 1;
    uint64_t tamanoOriginal = 0;          // bytes reales del archivo
    std::vector<unsigned char> datos;     // contenido rellenado con ceros hasta un múltiplo de N bits
    SecuenciaIds ids;                     // secuencia de símbolos (índice en el alfabeto)
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
