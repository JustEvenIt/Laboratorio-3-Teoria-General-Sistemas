#include "../include/Compresor.h"
#include "../include/Bits.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>
#include <stdexcept>

using namespace std;

static const uint32_t NINGUNO = UINT32_MAX;
static const char FIRMA[4] = {'T', 'I', '6', 'G'};

string nombreMetodo(int metodo) {
    switch (metodo) {
        case METODO_UNIFORME: return "Uniforme";
        case METODO_HUFFMAN: return "Huffman";
        case METODO_LZW: return "LZW";
    }
    return "?";
}

int Compresor::bitsPara(uint64_t valores) {
    int b = 1;
    while (b < 64 && (1ULL << b) < valores) ++b;
    return b;
}

// --- HUFFMAN ---

vector<uint8_t> Compresor::longitudesHuffman(const vector<uint64_t>& frecuencias, bool minimaVarianza) {
    const size_t M = frecuencias.size();
    vector<uint8_t> L(M, 0);
    if (M == 0) return L;
    if (M == 1) {
        L[0] = 1;
        return L;
    }

    // Método de dos colas: hojas ordenadas por frecuencia y nodos internos en orden de creación
    vector<uint32_t> hojas(M);
    iota(hojas.begin(), hojas.end(), 0);
    stable_sort(hojas.begin(), hojas.end(), [&](uint32_t a, uint32_t b) { return frecuencias[a] < frecuencias[b]; });

    const size_t total = 2 * M - 1;
    vector<uint64_t> peso(total);
    vector<uint32_t> padre(total, 0);
    for (size_t i = 0; i < M; ++i) peso[i] = frecuencias[i];

    size_t sigHoja = 0, sigInterno = M, finInterno = M;
    auto tomar = [&]() -> uint32_t {
        bool hayHoja = sigHoja < M, hayInterno = sigInterno < finInterno;
        if (hayHoja && hayInterno) {
            uint64_t ph = peso[hojas[sigHoja]], pi = peso[sigInterno];
            bool usarHoja = minimaVarianza ? (ph <= pi) : (ph < pi);
            return usarHoja ? hojas[sigHoja++] : (uint32_t)sigInterno++;
        }
        return hayHoja ? hojas[sigHoja++] : (uint32_t)sigInterno++;
    };

    for (size_t k = M; k < total; ++k) {
        uint32_t a = tomar(), b = tomar();
        peso[k] = peso[a] + peso[b];
        padre[a] = padre[b] = (uint32_t)k;
        finInterno = k + 1;
    }

    // La longitud del código es la profundidad de la hoja (el padre siempre tiene índice mayor)
    vector<int> profundidad(total, 0);
    for (size_t i = total - 1; i-- > 0;) profundidad[i] = profundidad[padre[i]] + 1;
    for (size_t i = 0; i < M; ++i) {
        if (profundidad[i] > 63) throw runtime_error("Codigo de Huffman demasiado largo");
        L[i] = (uint8_t)profundidad[i];
    }
    return L;
}

vector<uint64_t> Compresor::codigosCanonicos(const vector<uint8_t>& longitudes) {
    int maxL = 0;
    for (uint8_t l : longitudes) maxL = max(maxL, (int)l);
    vector<uint64_t> cuenta(maxL + 2, 0), siguiente(maxL + 2, 0);
    for (uint8_t l : longitudes) cuenta[l]++;
    cuenta[0] = 0;
    uint64_t codigo = 0;
    for (int bits = 1; bits <= maxL; ++bits) {
        codigo = (codigo + cuenta[bits - 1]) << 1;
        siguiente[bits] = codigo;
    }
    vector<uint64_t> codigos(longitudes.size());
    for (size_t id = 0; id < longitudes.size(); ++id) codigos[id] = siguiente[longitudes[id]]++;
    return codigos;
}

// --- CODIFICACIÓN ---

Codificado Compresor::codificarUniforme(const Fuente& f) {
    Codificado c;
    c.metodo = METODO_UNIFORME;
    c.anchoUniforme = bitsPara(f.tamAlfabeto());
    EscritorBits w;
    // Tamaño exacto reservado de una vez: si el vector crece duplicándose, por un momento ocupa la copia vieja y la nueva
    w.bytes.reserve((f.numSimbolos() * (uint64_t)c.anchoUniforme + 7) / 8 + 8);
    for (uint32_t id : f.ids) w.escribir(id, c.anchoUniforme);
    w.cerrar();
    c.bitsCarga = w.totalBits;
    c.carga = move(w.bytes);
    return c;
}

Codificado Compresor::codificarHuffman(const Fuente& f) {
    Codificado c;
    c.metodo = METODO_HUFFMAN;
    c.longitudes = longitudesHuffman(f.frecuencias, true);
    vector<uint64_t> codigos = codigosCanonicos(c.longitudes);
    EscritorBits w;
    uint64_t bits = 0;
    for (size_t id = 0; id < c.longitudes.size(); ++id) bits += f.frecuencias[id] * c.longitudes[id];
    w.bytes.reserve((bits + 7) / 8 + 8);  // tamaño exacto, de una vez
    for (uint32_t id : f.ids) w.escribir(codigos[id], c.longitudes[id]);
    w.cerrar();
    c.bitsCarga = w.totalBits;
    c.carga = move(w.bytes);
    return c;
}

// Diccionario LZW: (código del prefijo, símbolo) -> código, con direccionamiento abierto
class MapaLZW {
private:
    vector<uint64_t> claves;
    vector<uint32_t> valores;
    uint64_t mascara;
    static const uint64_t VACIO = UINT64_MAX;

public:
    explicit MapaLZW(uint64_t entradas) {
        uint64_t cap = 16;
        while (cap < entradas * 2) cap <<= 1;
        claves.assign(cap, VACIO);
        valores.resize(cap);
        mascara = cap - 1;
    }

    static uint64_t mezclar(uint64_t k) {
        k ^= k >> 31;
        k *= 0x7fb5d329728ea185ULL;
        k ^= k >> 27;
        return k;
    }

    bool buscar(uint64_t clave, uint32_t& valor) const {
        for (uint64_t i = mezclar(clave) & mascara;; i = (i + 1) & mascara) {
            if (claves[i] == VACIO) return false;
            if (claves[i] == clave) {
                valor = valores[i];
                return true;
            }
        }
    }

    void insertar(uint64_t clave, uint32_t valor) {
        uint64_t i = mezclar(clave) & mascara;
        while (claves[i] != VACIO) i = (i + 1) & mascara;
        claves[i] = clave;
        valores[i] = valor;
    }
};

Codificado Compresor::codificarLZW(const Fuente& f, uint64_t entradasExtra, size_t pasosTraza) {
    Codificado c;
    c.metodo = METODO_LZW;
    const uint64_t n = f.numSimbolos(), M = f.tamAlfabeto();
    if (n == 0) return c;

    // El diccionario arranca con el alfabeto de la fuente (M códigos) y crece hasta 'cap'
    entradasExtra = min(entradasExtra, n);
    c.capDiccionario = M + entradasExtra;
    MapaLZW mapa(entradasExtra + 1);
    EscritorBits w;
    // Como mucho se emite un código por símbolo, de a lo sumo bitsPara(cap) bits. Se reserva ese máximo de una vez: el
    // sistema sólo ocupa memoria real en las páginas que se escriben, y así el vector nunca se copia al crecer
    w.bytes.reserve((n * (uint64_t)bitsPara(c.capDiccionario) + 7) / 8 + 8);

    uint64_t tamDiccionario = M;
    uint32_t patron = f.ids[0];
    uint64_t inicio = 0;
    double sumaCuadrados = 0;  // sum(b_j^2 / m_j) para la varianza por símbolo

    auto emitir = [&](uint64_t fin, uint32_t siguiente, int64_t nueva) {
        int ancho = bitsPara(tamDiccionario);
        w.escribir(patron, ancho);
        c.codigosEmitidos++;
        uint64_t largo = fin - inicio;
        sumaCuadrados += (double)ancho * ancho / (double)largo;
        if (c.traza.size() < pasosTraza) c.traza.push_back({inicio, largo, siguiente, patron, ancho, nueva});
    };

    for (uint64_t i = 1; i < n; ++i) {
        uint32_t s = f.ids[i];
        uint64_t clave = ((uint64_t)patron << 32) | s;
        uint32_t codigo;
        if (mapa.buscar(clave, codigo)) {
            patron = codigo;  // el patrón + s ya existe: seguir extendiéndolo
            continue;
        }
        bool agrega = tamDiccionario < c.capDiccionario;
        emitir(i, s, agrega ? (int64_t)tamDiccionario : -1);
        if (agrega) {
            mapa.insertar(clave, (uint32_t)tamDiccionario);
            tamDiccionario++;
        }
        patron = s;
        inicio = i;
    }
    emitir(n, NINGUNO, -1);

    w.cerrar();
    c.bitsCarga = w.totalBits;
    c.carga = move(w.bytes);
    double media = (double)c.bitsCarga / n;
    c.varianzaLZW = max(0.0, sumaCuadrados / n - media * media);
    return c;
}

// --- DECODIFICACIÓN ---

vector<uint32_t> Compresor::decodificar(const Codificado& c, uint64_t n, uint32_t M) {
    vector<uint32_t> salida;
    salida.reserve(n);
    decodificarCon(c, c.carga.data(), c.bitsCarga, n, M, [&](uint32_t id) { salida.push_back(id); });
    return salida;
}

void Compresor::decodificarCon(const Codificado& c, const uint8_t* carga, uint64_t bitsCarga, uint64_t n, uint32_t M,
                               const function<void(uint32_t)>& emitir) {
    const uint32_t BORRADO = M;
    uint64_t emitidos = 0;
    auto dar = [&](uint32_t id) {
        if (emitidos < n) {
            emitir(id);
            ++emitidos;
        }
    };
    LectorBits r(carga, bitsCarga);

    if (c.metodo == METODO_UNIFORME) {
        for (uint64_t i = 0; i < n; ++i) {
            uint64_t v = r.leer(c.anchoUniforme);
            dar(r.agotado || v >= M ? BORRADO : (uint32_t)v);
        }
    }
    else if (c.metodo == METODO_HUFFMAN) {
        // Tablas canónicas: por longitud, primer código, cantidad y posición en la lista ordenada
        int maxL = 0;
        for (uint8_t l : c.longitudes) maxL = max(maxL, (int)l);
        vector<uint64_t> cuenta(maxL + 2, 0), primerCodigo(maxL + 2, 0), desplazamiento(maxL + 2, 0);
        for (uint8_t l : c.longitudes) cuenta[l]++;
        cuenta[0] = 0;
        uint64_t codigo = 0;
        for (int b = 1; b <= maxL; ++b) {
            codigo = (codigo + cuenta[b - 1]) << 1;
            primerCodigo[b] = codigo;
            desplazamiento[b + 1] = desplazamiento[b] + cuenta[b];
        }
        vector<uint32_t> ordenados(c.longitudes.size());
        vector<uint64_t> llenado(desplazamiento);
        for (uint32_t id = 0; id < c.longitudes.size(); ++id) ordenados[llenado[c.longitudes[id]]++] = id;

        for (uint64_t i = 0; i < n; ++i) {
            uint64_t cod = 0;
            uint32_t simbolo = BORRADO;
            for (int l = 1; l <= maxL; ++l) {
                cod = (cod << 1) | (uint64_t)r.bit();
                if (cuenta[l] && cod - primerCodigo[l] < cuenta[l]) {
                    simbolo = ordenados[desplazamiento[l] + (cod - primerCodigo[l])];
                    break;
                }
            }
            dar(r.agotado ? BORRADO : simbolo);
        }
    }
    else if (c.metodo == METODO_LZW) {
        // Entradas >= M: prefijo + último símbolo. Las entradas "borrado" tienen largo 1.
        const uint64_t cap = c.capDiccionario;
        vector<uint32_t> prefijo, ultimo, primero, frase;
        vector<uint64_t> largo;

        auto primeroDe = [&](uint64_t cod) -> uint32_t { return cod < M ? (uint32_t)cod : primero[cod - M]; };
        auto largoDe = [&](uint64_t cod) -> uint64_t { return cod < M ? 1 : largo[cod - M]; };
        auto agregar = [&](uint32_t pre, uint32_t s) {
            if (pre == NINGUNO) {
                prefijo.push_back(NINGUNO); ultimo.push_back(BORRADO); primero.push_back(BORRADO); largo.push_back(1);
            } else {
                prefijo.push_back(pre); ultimo.push_back(s); primero.push_back(primeroDe(pre)); largo.push_back(largoDe(pre) + 1);
            }
        };
        // La frase se arma de atrás hacia adelante siguiendo los prefijos y se entrega en orden
        auto expandir = [&](uint64_t cod) {
            const uint64_t len = largoDe(cod);
            frase.resize(len);
            for (uint64_t pos = len; pos > 0;) {
                if (cod < M) { frase[--pos] = (uint32_t)cod; break; }
                frase[--pos] = ultimo[cod - M];
                cod = prefijo[cod - M];
            }
            for (uint32_t id : frase) dar(id);
        };

        uint32_t previo = NINGUNO;
        for (uint64_t k = 0; emitidos < n; ++k) {
            int ancho = bitsPara(min<uint64_t>(M + k, cap));
            uint64_t codigo = r.leer(ancho);
            if (r.agotado) break;
            uint64_t conocidos = k == 0 ? M : min<uint64_t>(M + k - 1, cap);
            bool agrega = k >= 1 && M + k - 1 < cap;

            if (codigo < conocidos) {
                if (agrega) agregar(previo, primeroDe(codigo));
                expandir(codigo);
                previo = (uint32_t)codigo;
            } else if (agrega && codigo == conocidos) {
                // Caso especial cScSc: el código se define con el patrón anterior + su primer símbolo
                agregar(previo, previo == NINGUNO ? BORRADO : primeroDe(previo));
                expandir(codigo);
                previo = (uint32_t)codigo;
            } else {
                if (agrega) agregar(NINGUNO, BORRADO);
                dar(BORRADO);
                previo = NINGUNO;
            }
        }
    }
    while (emitidos < n) dar(BORRADO);  // lo que faltó por errores o por fin del flujo
}

// --- ARCHIVO COMPRIMIDO ---

static void escribirU64(vector<uint8_t>& o, uint64_t v) {
    for (int i = 0; i < 8; ++i) o.push_back((uint8_t)(v >> (8 * i)));
}

uint64_t Compresor::tamanoCabecera(const Codificado& c, const Fuente& f) {
    // firma + versión + método + N + tamaño original + n + M + bits de carga
    uint64_t base = 4 + 1 + 1 + 4 + 8 + 8 + 8 + 8;
    uint64_t alfabeto = (uint64_t)f.tamAlfabeto() * f.bytesPorSimbolo;
    uint64_t tabla = c.metodo == METODO_UNIFORME ? 1 : c.metodo == METODO_HUFFMAN ? f.tamAlfabeto() : 8;
    return base + alfabeto + tabla;
}

vector<uint8_t> Compresor::serializar(const Codificado& c, const Fuente& f) {
    vector<uint8_t> o;
    o.reserve(tamanoCabecera(c, f) + c.carga.size());
    o.insert(o.end(), FIRMA, FIRMA + 4);
    o.push_back(1);
    o.push_back((uint8_t)c.metodo);
    for (int i = 0; i < 4; ++i) o.push_back((uint8_t)((uint32_t)f.nBits >> (8 * i)));
    escribirU64(o, f.tamanoOriginal);
    escribirU64(o, f.numSimbolos());
    escribirU64(o, f.tamAlfabeto());
    for (uint32_t id = 0; id < f.tamAlfabeto(); ++id) o.insert(o.end(), f.simbolo(id), f.simbolo(id) + f.bytesPorSimbolo);
    if (c.metodo == METODO_UNIFORME) o.push_back((uint8_t)c.anchoUniforme);
    else if (c.metodo == METODO_HUFFMAN) o.insert(o.end(), c.longitudes.begin(), c.longitudes.end());
    else escribirU64(o, c.capDiccionario);
    escribirU64(o, c.bitsCarga);
    o.insert(o.end(), c.carga.begin(), c.carga.end());
    return o;
}

bool Compresor::guardar(const string& ruta, const Codificado& c, const Fuente& f) {
    ofstream o(ruta, ios::binary);
    if (!o.is_open()) return false;
    vector<uint8_t> bytes = serializar(c, f);
    o.write((const char*)bytes.data(), bytes.size());
    return (bool)o;
}

bool Compresor::descomprimirArchivo(const string& ruta, vector<unsigned char>& salida, string& error) {
    ifstream in(ruta, ios::binary | ios::ate);
    if (!in.is_open()) { error = "No se pudo abrir " + ruta; return false; }
    vector<uint8_t> d((size_t)in.tellg());
    in.seekg(0);
    in.read((char*)d.data(), d.size());
    return deserializar(d, salida, error);
}

bool Compresor::deserializar(const vector<uint8_t>& d, vector<unsigned char>& salida, string& error, uint64_t maxSalida) {
    size_t pos = 0;
    auto hay = [&](uint64_t bytes) { return bytes <= d.size() - pos; };
    auto leerU = [&](int bytes) {
        uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v |= (uint64_t)d[pos + i] << (8 * i);
        pos += bytes;
        return v;
    };

    if (d.size() < 34 || memcmp(d.data(), FIRMA, 4) != 0) { error = "No es un archivo .ti6g valido"; return false; }
    pos = 5;
    Codificado c;
    c.metodo = d[pos++];
    uint64_t nBits = leerU(4), tamOriginal = leerU(8), n = leerU(8), M = leerU(8);
    uint64_t B = nBits / 8;
    if (B == 0 || M > n || !hay(M * B) || tamOriginal > maxSalida || (n > 0 && n - 1 > tamOriginal / B)) {
        error = "Cabecera corrupta";
        return false;
    }
    const unsigned char* alfabeto = d.data() + pos;
    pos += M * B;

    if (c.metodo == METODO_UNIFORME) { if (!hay(1)) { error = "Cabecera corrupta"; return false; } c.anchoUniforme = d[pos++]; }
    else if (c.metodo == METODO_HUFFMAN) {
        if (!hay(M)) { error = "Cabecera corrupta"; return false; }
        c.longitudes.assign(d.begin() + pos, d.begin() + pos + M);
        pos += M;
    }
    else if (c.metodo == METODO_LZW) { if (!hay(8)) { error = "Cabecera corrupta"; return false; } c.capDiccionario = leerU(8); }
    else { error = "Metodo desconocido"; return false; }

    if (!hay(8)) { error = "Cabecera corrupta"; return false; }
    c.bitsCarga = leerU(8);
    if (!hay((c.bitsCarga + 7) / 8)) { error = "Carga incompleta"; return false; }

    // Cada símbolo se escribe apenas se decodifica (los borrados quedan en cero)
    salida.assign(n * B, 0);
    uint64_t i = 0;
    decodificarCon(c, d.data() + pos, c.bitsCarga, n, (uint32_t)M, [&](uint32_t id) {
        if (id < M) memcpy(salida.data() + i * B, alfabeto + (uint64_t)id * B, B);
        ++i;
    });
    salida.resize(min<uint64_t>(tamOriginal, salida.size()));
    return true;
}
