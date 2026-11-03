#include "../include/ProcesadorFuente.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

using namespace std;

static inline uint64_t mezclar(uint64_t h) {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

// Hash de un bloque de bytes de cualquier tamaño (8 bytes por iteración)
static uint64_t hashBloque(const unsigned char* p, size_t largo) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ largo;
    size_t i = 0;
    for (; i + 8 <= largo; i += 8) {
        uint64_t palabra;
        memcpy(&palabra, p + i, 8);
        h = mezclar(h ^ palabra);
    }
    uint64_t resto = 0;
    for (size_t j = 0; i < largo; ++i, ++j) resto |= (uint64_t)p[i] << (8 * j);
    return mezclar(h ^ resto ^ 0x5bd1e995ULL);
}

bool ProcesadorFuente::leerArchivo(const string& ruta, vector<unsigned char>& contenido, string& error) {
    ifstream archivo(ruta, ios::binary | ios::ate);
    if (!archivo.is_open()) {
        error = "No se pudo abrir el archivo: " + ruta;
        return false;
    }
    streamsize tam = archivo.tellg();
    archivo.seekg(0);
    contenido.resize((size_t)tam);
    if (tam > 0 && !archivo.read((char*)contenido.data(), tam)) {
        error = "Error leyendo el archivo: " + ruta;
        return false;
    }
    return true;
}

Fuente ProcesadorFuente::fragmentar(const string& ruta, const vector<unsigned char>& contenido, int nBits) {
    Fuente f;
    f.ruta = ruta;
    f.nBits = nBits;
    f.bytesPorSimbolo = nBits / 8;
    f.tamanoOriginal = contenido.size();
    detectarTipo(ruta, contenido, f.tipo, f.formato);

    const size_t B = f.bytesPorSimbolo;
    const size_t nBloques = (contenido.size() + B - 1) / B;

    // El último bloque se completa con ceros; el tamaño original permite quitarlos al reconstruir
    f.datos.assign(contenido.begin(), contenido.end());
    f.datos.resize(nBloques * B, 0);
    f.ids.resize(nBloques);

    // Tabla hash de direccionamiento abierto: guarda id+1 (0 = vacío)
    size_t capacidad = 1 << 16;
    vector<uint32_t> tabla(capacidad, 0);
    vector<uint64_t> hashes;

    for (size_t b = 0; b < nBloques; ++b) {
        const unsigned char* p = f.datos.data() + b * B;
        uint64_t h = hashBloque(p, B);
        size_t i = h & (capacidad - 1);
        uint32_t id;
        while (true) {
            uint32_t entrada = tabla[i];
            if (entrada == 0) {
                id = (uint32_t)f.frecuencias.size();
                tabla[i] = id + 1;
                hashes.push_back(h);
                f.primerBloque.push_back(b);
                f.frecuencias.push_back(0);
                break;
            }
            id = entrada - 1;
            if (hashes[id] == h && memcmp(f.datos.data() + f.primerBloque[id] * B, p, B) == 0) break;
            i = (i + 1) & (capacidad - 1);
        }
        f.ids[b] = id;
        f.frecuencias[id]++;

        // Mantener la ocupación por debajo del 50%
        if (f.frecuencias.size() * 2 > capacidad) {
            capacidad *= 2;
            tabla.assign(capacidad, 0);
            for (uint32_t k = 0; k < hashes.size(); ++k) {
                size_t j = hashes[k] & (capacidad - 1);
                while (tabla[j] != 0) j = (j + 1) & (capacidad - 1);
                tabla[j] = k + 1;
            }
        }
    }
    return f;
}

vector<double> ProcesadorFuente::calcularProbabilidades(const Fuente& fuente) {
    vector<double> probabilidades(fuente.tamAlfabeto());
    double total = (double)fuente.numSimbolos();
    for (size_t k = 0; k < probabilidades.size(); ++k) probabilidades[k] = fuente.frecuencias[k] / total;
    return probabilidades;
}

void ProcesadorFuente::detectarTipo(const string& ruta, const vector<unsigned char>& c, string& tipo, string& formato) {
    auto empieza = [&](size_t pos, const char* firma) {
        size_t n = strlen(firma);
        return c.size() >= pos + n && memcmp(c.data() + pos, firma, n) == 0;
    };

    string ext;
    size_t punto = ruta.find_last_of('.');
    if (punto != string::npos) ext = ruta.substr(punto + 1);
    for (char& ch : ext) ch = (char)tolower((unsigned char)ch);

    if (empieza(0, "RIFF") && empieza(8, "WAVE")) { tipo = "audio"; formato = "WAV"; }
    else if (empieza(0, "RIFF") && empieza(8, "AVI ")) { tipo = "video"; formato = "AVI"; }
    else if (empieza(0, "RIFF") && empieza(8, "WEBP")) { tipo = "imagen"; formato = "WEBP"; }
    else if (c.size() >= 3 && c[0] == 0xFF && c[1] == 0xD8 && c[2] == 0xFF) { tipo = "imagen"; formato = "JPEG"; }
    else if (empieza(0, "\x89PNG")) { tipo = "imagen"; formato = "PNG"; }
    else if (empieza(0, "GIF8")) { tipo = "imagen"; formato = "GIF"; }
    else if (empieza(0, "BM") && ext == "bmp") { tipo = "imagen"; formato = "BMP"; }
    else if (empieza(0, "ID3") || (c.size() >= 2 && c[0] == 0xFF && (c[1] & 0xE0) == 0xE0)) { tipo = "audio"; formato = "MP3"; }
    else if (empieza(0, "OggS")) { tipo = "audio"; formato = "OGG"; }
    else if (empieza(0, "fLaC")) { tipo = "audio"; formato = "FLAC"; }
    else if (empieza(4, "ftyp")) {
        if (empieza(8, "M4A")) { tipo = "audio"; formato = "M4A"; }
        else if (empieza(8, "qt")) { tipo = "video"; formato = "MOV"; }
        else { tipo = "video"; formato = "MP4"; }
    }
    else if (c.size() >= 4 && c[0] == 0x1A && c[1] == 0x45 && c[2] == 0xDF && c[3] == 0xA3) { tipo = "video"; formato = "MKV/WebM"; }
    else if (empieza(0, "%PDF")) { tipo = "documento"; formato = "PDF"; }
    else if (empieza(0, "PK\x03\x04")) { tipo = "comprimido"; formato = "ZIP/Office"; }
    else {
        // Texto: casi todos los bytes son imprimibles, espacios o UTF-8, y no hay bytes nulos
        size_t muestra = min<size_t>(c.size(), 1 << 16), validos = 0;
        bool hayNulo = false;
        for (size_t i = 0; i < muestra; ++i) {
            unsigned char b = c[i];
            if (b == 0) hayNulo = true;
            if (b >= 0x20 || b == '\n' || b == '\r' || b == '\t') validos++;
        }
        if (muestra > 0 && !hayNulo && validos >= muestra * 0.95) {
            tipo = "texto";
            formato = ext.empty() ? "TXT" : ext;
            for (char& ch : formato) ch = (char)toupper((unsigned char)ch);
        } else {
            tipo = "binario";
            formato = ext.empty() ? "desconocido" : ext;
        }
    }
}
