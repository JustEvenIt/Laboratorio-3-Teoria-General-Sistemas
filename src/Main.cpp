#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include "../include/AgenteRL.h"
#include "../include/Canal.h"
#include "../include/Compresor.h"
#include "../include/ControlError.h"
#include "../include/Enlace.h"
#include "../include/Json.h"
#include "../include/ProcesadorFuente.h"
#include "../include/Simulacion.h"
#include "../include/Sistema.h"

using namespace std;
namespace fs = std::filesystem;

struct Opciones {
    string ruta;
    int nBits = 8;
    string metodo = "auto";        // auto | uniforme | huffman | lzw
    double p = 0.0;                // probabilidad de error del canal BSC
    string fec = "7,4";            // código de bloque: n,k o P:filas (ninguno omite el control de error)
    int arqMax = 8;                // ARQ: transmisiones máximas por bloque
    uint64_t semilla = 42;
    string salida = "data/salida";
    string json;
    size_t pasosTraza = 200;
    size_t muestra = 16;
    uint64_t entradasDiccionario = 1 << 20;
    vector<int> barrido;           // modo comparación de varios N
    bool agente = false;           // modo agente: decide la mejor codificación
    OpcionesAgente ag;
    bool enlace = false;           // modo enlace 5G NR / requisitos 5G y 6G
    string comprimido;             // archivo .ti6g que se transmite
    ParametrosEnlace en;
    bool simular = false;          // modo simulación 5G/6G del archivo comprimido
    bool curvas = false;           // modo curvas BLER frente a SNR
    bool confiabilidad = false;    // modo prueba de confiabilidad con mensajes de 32 bytes
    OpcionesSimulacion sim;
};

static void uso() {
    cerr << "Uso: codificador_6g <archivo> <N bits> [--metodo auto|uniforme|huffman|lzw] [--p 0.001]\n"
            "                    [--fec n,k (ej. 7,4) | P:110,011,111,101 | ninguno] [--arq-max 8]\n"
            "                    [--semilla 42] [--salida DIR] [--json RUTA] [--traza 200] [--muestra 16]\n"
            "     codificador_6g <archivo> --barrido 8,16,32,... [--json RUTA]\n"
            "     codificador_6g <archivo> --agente [--bloque 0] [--pasos 0]\n"
            "                    [--semilla 42] [--candidatas 3] [--tasa 0.003] [--ocultas 64] [--memoria 1000] [--decaimiento 0.97]\n"
            "                    [--eps-cambio 0.6] [--eps-min 0.02] [--ph 0.02,1] [--sin-detector] [--salida DIR] [--json RUTA]\n"
            "     codificador_6g <archivo> --enlace --comprimido ARCHIVO.ti6g [--snr 20] [--capas 4]\n"
            "                    [--servicio embb|urllc] [--tabla-mcs 256|64] [--json RUTA]\n"
            "     codificador_6g <archivo> --simular --comprimido ARCHIVO.ti6g [--escenario embb|urllc|6g] [--snr 20]\n"
            "                    [--capas auto|1|2|4|8] [--fec 7,4|ninguno] [--harq 4] [--iteraciones 25] [--max-bytes 0]\n"
            "                    [--sin-cifrado] [--clave 32 cifras hex] [--paquetes -1] [--bloques-traza 40]\n"
            "                    [--progreso RUTA] [--semilla 42] [--salida DIR] [--json RUTA]\n"
            "     codificador_6g --curvas [--escenario embb] [--snr 20] [--fec 7,4]\n"
            "     codificador_6g --confiabilidad [--escenario urllc] [--snr 10] [--paquetes 300000] [--json RUTA]\n"
            "                    [--snr-desde A --snr-hasta B --snr-paso 1] [--bloques-curva 100] [--json RUTA]\n"
            "N debe ser multiplo de 8, entre 8 y 32768.\n";
}

static bool nValido(int n) { return n >= 8 && n <= 32768 && n % 8 == 0; }

// --- Utilidades de presentación ---

static string hexBloque(const unsigned char* p, size_t largo, size_t maxBytes = 16) {
    static const char* d = "0123456789ABCDEF";
    string s;
    for (size_t i = 0; i < min(largo, maxBytes); ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    if (largo > maxBytes) s += "…";
    return s;
}

static string asciiBloque(const unsigned char* p, size_t largo, size_t maxBytes = 16) {
    string s;
    for (size_t i = 0; i < min(largo, maxBytes); ++i) s += (p[i] >= 0x20 && p[i] < 0x7F) ? (char)p[i] : '.';
    if (largo > maxBytes) s += "…";
    return s;
}

static string bitsTexto(uint64_t codigo, int largo) {
    string s(largo, '0');
    for (int i = 0; i < largo; ++i)
        if ((codigo >> (largo - 1 - i)) & 1) s[i] = '1';
    return s;
}

static string bitsFlujo(const vector<uint8_t>& bytes, uint64_t totalBits, uint64_t maxBits) {
    string s;
    for (uint64_t i = 0; i < min(totalBits, maxBits); ++i) s += ((bytes[i >> 3] >> (7 - (i & 7))) & 1) ? '1' : '0';
    return s;
}

// Vista corta de varios símbolos consecutivos: "41|42|43 (+5)"
static string vistaSecuencia(const Fuente& f, uint64_t inicio, uint64_t largo, bool ascii) {
    string s;
    uint64_t mostrar = min<uint64_t>(largo, 4);
    size_t bytes = f.bytesPorSimbolo <= 2 ? f.bytesPorSimbolo : 4;
    for (uint64_t i = 0; i < mostrar; ++i) {
        const unsigned char* p = f.simbolo(f.ids[inicio + i]);
        if (i) s += ascii ? "" : "|";
        s += ascii ? asciiBloque(p, f.bytesPorSimbolo, bytes) : hexBloque(p, f.bytesPorSimbolo, bytes);
    }
    if (largo > mostrar) s += " (+" + to_string(largo - mostrar) + ")";
    return s;
}

static double ms(chrono::steady_clock::time_point desde) {
    return chrono::duration<double, milli>(chrono::steady_clock::now() - desde).count();
}

static uint64_t bytesDeBits(uint64_t bits) { return (bits + 7) / 8; }

static void escribirMetricasCodigo(Json& j, const MetricasCodigo& m) {
    j.num("longitud_media", m.longitudMedia).num("varianza", m.varianza).num("desviacion", m.desviacion)
     .num("eficiencia", m.eficiencia).num("kraft", m.kraft)
     .ent("longitud_min", m.longitudMin).ent("longitud_max", m.longitudMax);
}

// --- Modo barrido: compara varios N sobre el mismo archivo ---

static int ejecutarBarrido(const Opciones& op, const vector<unsigned char>& contenido) {
    ProcesadorFuente procesador;
    Compresor compresor;
    ofstream archivoJson;
    ostream* salidaJson = &cout;
    if (!op.json.empty()) {
        archivoJson.open(op.json);
        salidaJson = &archivoJson;
    }
    Json j(*salidaJson);
    j.objeto().str("modo", "barrido").ent("tamano_original", contenido.size()).arreglo("filas");

    for (int N : op.barrido) {
        auto t = chrono::steady_clock::now();
        Fuente f = procesador.fragmentar(op.ruta, contenido, N);
        vector<double> P = procesador.calcularProbabilidades(f);
        MetricasFuente mf = Sistema::analizarFuente(f, P);

        vector<uint8_t> Lmin = Compresor::longitudesHuffman(f.frecuencias, true);
        vector<uint8_t> Lmax = Compresor::longitudesHuffman(f.frecuencias, false);
        MetricasCodigo mh = Sistema::metricasCodigo("Huffman", P, Lmin, mf.entropia);
        MetricasCodigo mhMax = Sistema::metricasCodigo("Huffman", P, Lmax, mf.entropia);
        int ancho = Compresor::bitsPara(f.tamAlfabeto());

        Codificado cu, ch, cl = compresor.codificarLZW(f, op.entradasDiccionario, 0);
        cu.metodo = METODO_UNIFORME;
        ch.metodo = METODO_HUFFMAN;
        uint64_t bitsHuffman = 0;
        for (size_t k = 0; k < Lmin.size(); ++k) bitsHuffman += f.frecuencias[k] * Lmin[k];
        uint64_t totU = compresor.tamanoCabecera(cu, f) + bytesDeBits((uint64_t)f.numSimbolos() * ancho);
        uint64_t totH = compresor.tamanoCabecera(ch, f) + bytesDeBits(bitsHuffman);
        uint64_t totL = compresor.tamanoCabecera(cl, f) + cl.carga.size();
        uint64_t mejor = min({totU, totH, totL});

        j.objeto()
         .ent("n_bits", N).ent("simbolos", mf.n).ent("alfabeto", mf.M)
         .num("entropia", mf.entropia).num("entropia_por_bit", mf.entropia / N)
         .num("kl_uniforme_n", mf.klUniformeN).num("redundancia", mf.redundancia)
         .ent("ancho_uniforme", ancho)
         .num("huffman_longitud_media", mh.longitudMedia).num("huffman_varianza", mh.varianza)
         .num("huffman_varianza_max", mhMax.varianza).num("huffman_eficiencia", mh.eficiencia)
         .num("lzw_bits_por_simbolo", (double)cl.bitsCarga / mf.n).num("lzw_varianza", cl.varianzaLZW)
         .ent("bytes_alfabeto", (int64_t)(f.tamAlfabeto() * f.bytesPorSimbolo))
         .ent("total_uniforme", totU).ent("total_huffman", totH).ent("total_lzw", totL)
         .str("mejor", mejor == totH ? "Huffman" : mejor == totL ? "LZW" : "Uniforme")
         .num("razon_mejor", (double)contenido.size() / mejor)
         .num("tiempo_ms", ms(t))
         .fin();
        cout << "[Barrido] N=" << N << " H=" << mf.entropia << " M=" << mf.M << " mejor=" << mejor << " bytes\n";
    }
    j.fin().fin();
    *salidaJson << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    Opciones op;
    vector<string> posicionales;
    for (int i = 1; i < argc; ++i) {
        string a = argv[i];
        auto valor = [&]() -> string {
            if (i + 1 >= argc) { uso(); exit(2); }
            return argv[++i];
        };
        try {
            if (a == "--metodo") op.metodo = valor();
            else if (a == "--p") op.p = stod(valor());
            else if (a == "--fec") op.fec = valor();
            else if (a == "--arq-max") op.arqMax = stoi(valor());
            else if (a == "--semilla") op.semilla = stoull(valor());
            else if (a == "--salida") op.salida = valor();
            else if (a == "--json") op.json = valor();
            else if (a == "--traza") op.pasosTraza = stoul(valor());
            else if (a == "--muestra") op.muestra = stoul(valor());
            else if (a == "--diccionario") op.entradasDiccionario = stoull(valor());
            else if (a == "--agente") op.agente = true;
            else if (a == "--bloque") op.ag.bytesBloque = stoul(valor());
            else if (a == "--pasos") op.ag.pasos = stoull(valor());
            else if (a == "--ocultas") op.ag.ocultas = stoi(valor());
            else if (a == "--tasa") op.ag.tasa = stod(valor());
            else if (a == "--memoria") op.ag.memoria = stoul(valor());
            else if (a == "--decaimiento") op.ag.decaimiento = stod(valor());
            else if (a == "--eps-cambio") op.ag.epsCambio = stod(valor());
            else if (a == "--eps-min") op.ag.epsMin = stod(valor());
            else if (a == "--candidatas") op.ag.candidatas = stoi(valor());
            else if (a == "--sin-detector") op.ag.detectar = false;
            else if (a == "--ph") {
                string v = valor();
                size_t coma = v.find(',');
                if (coma == string::npos) throw invalid_argument(v);
                op.ag.delta = stod(v.substr(0, coma));
                op.ag.lambda = stod(v.substr(coma + 1));
            }
            else if (a == "--enlace") op.enlace = true;
            else if (a == "--comprimido") op.comprimido = valor();
            else if (a == "--snr") op.en.snrDb = op.sim.snrDb = stod(valor());
            else if (a == "--capas") {  // en --simular, "auto" deja que el agente elija
                const string v = valor();
                op.sim.capas = v == "auto" ? 0 : stoi(v);
                if (v != "auto") op.en.capas = op.sim.capas;
            }
            else if (a == "--sin-cifrado") op.sim.cifrar = false;
            else if (a == "--clave") op.sim.clave = valor();
            else if (a == "--bloques-traza") op.sim.bloquesTraza = stoi(valor());
            else if (a == "--progreso") op.sim.progreso = valor();
            else if (a == "--simular") op.simular = true;
            else if (a == "--curvas") op.curvas = true;
            else if (a == "--confiabilidad") op.confiabilidad = true;
            else if (a == "--escenario") op.sim.escenario = valor();
            else if (a == "--harq") op.sim.harqMax = stoi(valor());
            else if (a == "--iteraciones") op.sim.iteraciones = stoi(valor());
            else if (a == "--max-bytes") op.sim.maxBytes = stoull(valor());
            else if (a == "--paquetes") op.sim.paquetes = stoi(valor());
            else if (a == "--snr-desde") op.sim.snrDesde = stod(valor());
            else if (a == "--snr-hasta") op.sim.snrHasta = stod(valor());
            else if (a == "--snr-paso") op.sim.snrPaso = stod(valor());
            else if (a == "--bloques-curva") op.sim.bloquesCurva = stoi(valor());
            else if (a == "--servicio") op.en.servicio = valor();
            else if (a == "--tabla-mcs") op.en.tablaMCS = stoi(valor());
            else if (a == "--barrido") {
                stringstream ss(valor());
                string item;
                while (getline(ss, item, ',')) op.barrido.push_back(stoi(item));
            }
            else if (a.rfind("--", 0) == 0) { uso(); return 2; }
            else posicionales.push_back(a);
        } catch (const exception&) {
            cerr << "Valor invalido para " << a << "\n";
            return 2;
        }
    }
    if (op.simular || op.curvas || op.confiabilidad) {
        const bool valido = (op.sim.escenario == "embb" || op.sim.escenario == "urllc" || op.sim.escenario == "6g") &&
                            op.sim.snrDb >= -20 && op.sim.snrDb <= 60 && op.sim.harqMax >= 1 && op.sim.harqMax <= 8 &&
                            op.sim.iteraciones >= 1 && op.sim.iteraciones <= 100 && op.sim.bloquesCurva >= 1 &&
                            op.sim.bloquesCurva <= 100000 && op.sim.bloquesTraza >= 0 && op.sim.bloquesTraza <= 1000 &&
                            (op.sim.capas == 0 || op.sim.capas == 1 || op.sim.capas == 2 || op.sim.capas == 4 ||
                             op.sim.capas == 8);
        if (!valido) { uso(); return 2; }
        op.sim.fec = op.fec;
        op.sim.semilla = op.semilla;
        op.sim.salida = op.salida;
        if (op.curvas) return ejecutarCurvas(op.sim, op.json.empty() ? (fs::path(op.salida) / "curvas.json").string() : op.json);
        if (op.confiabilidad)
            return ejecutarConfiabilidad(op.sim, op.json.empty() ? (fs::path(op.salida) / "confiabilidad.json").string() : op.json);
        if (posicionales.empty() || op.comprimido.empty()) { uso(); return 2; }
        return ejecutarSimulacion(posicionales[0], op.comprimido, op.sim,
                                  op.json.empty() ? (fs::path(op.salida) / "simulacion.json").string() : op.json);
    }
    if (posicionales.empty()) { uso(); return 2; }
    op.ruta = posicionales[0];
    if (posicionales.size() > 1) op.nBits = atoi(posicionales[1].c_str());

    for (int N : op.barrido.empty() ? vector<int>{op.nBits} : op.barrido) {
        if (!nValido(N)) {
            cerr << "N=" << N << " no es valido: debe ser multiplo de 8 entre 8 y 32768.\n";
            return 2;
        }
    }
    if (op.p < 0 || op.p > 0.5) {
        cerr << "La probabilidad de error del canal debe estar entre 0 y 0.5\n";
        return 2;
    }
    CodigoBloque codigo;
    const bool usarControl = op.fec != "ninguno";
    {
        string errorCodigo;
        if (usarControl && !CodigoBloque::crear(op.fec, codigo, errorCodigo)) { cerr << errorCodigo << "\n"; return 2; }
        if (op.arqMax < 1 || op.arqMax > 64) { cerr << "--arq-max debe estar entre 1 y 64\n"; return 2; }
    }

    ProcesadorFuente procesador;
    Compresor compresor;
    vector<unsigned char> contenido;
    string error;
    if (!procesador.leerArchivo(op.ruta, contenido, error)) { cerr << error << "\n"; return 1; }
    if (contenido.empty()) { cerr << "El archivo esta vacio.\n"; return 1; }

    if (!op.barrido.empty()) return ejecutarBarrido(op, contenido);
    if (op.enlace) {
        const int capasValidas[] = {1, 2, 4, 8};
        error_code ec;
        uint64_t bytesComprimido = op.comprimido.empty() ? 0 : fs::file_size(op.comprimido, ec);
        if (op.comprimido.empty() || ec || find(begin(capasValidas), end(capasValidas), op.en.capas) == end(capasValidas) ||
            (op.en.servicio != "embb" && op.en.servicio != "urllc") || (op.en.tablaMCS != 256 && op.en.tablaMCS != 64) ||
            op.en.snrDb < -20 || op.en.snrDb > 60) {
            uso();
            return 2;
        }
        return ejecutarEnlace(op.ruta, contenido.size(), bytesComprimido, op.en,
                              op.json.empty() ? (fs::path(op.salida) / "enlace.json").string() : op.json);
    }
    if (op.agente) {
        if ((op.ag.bytesBloque != 0 && op.ag.bytesBloque < 256) ||
            op.ag.bytesBloque > (1u << 20) || (op.ag.pasos != 0 && op.ag.pasos < 10) || op.ag.pasos > 100000 || op.ag.ocultas < 1 || op.ag.tasa <= 0 ||
            op.ag.memoria < 1 || op.ag.candidatas < 1 || op.ag.candidatas > 27 || op.ag.decaimiento <= 0 || op.ag.decaimiento > 1 || op.ag.epsMin < 0 || op.ag.epsCambio > 1) {
            uso();
            return 2;
        }
        op.ag.ruta = op.ruta;
        op.ag.salida = op.salida;
        op.ag.json = op.json.empty() ? (fs::path(op.salida) / "agente.json").string() : op.json;
        op.ag.semilla = op.semilla;
        return ejecutarAgente(op.ag, contenido);
    }

    // ===== ETAPA 1: FUENTE =====
    auto t = chrono::steady_clock::now();
    Fuente f = procesador.fragmentar(op.ruta, contenido, op.nBits);
    vector<double> P = procesador.calcularProbabilidades(f);
    MetricasFuente mf = Sistema::analizarFuente(f, P);
    double tFuente = ms(t);
    contenido.clear();  // f.datos ya contiene el archivo; se libera la copia
    contenido.shrink_to_fit();
    const uint64_t n = f.numSimbolos();
    const uint32_t M = (uint32_t)f.tamAlfabeto();
    cout << "[Fuente] " << op.ruta << " | tipo " << f.tipo << " (" << f.formato << ") | " << f.tamanoOriginal << " bytes\n"
         << "[Fuente] N=" << op.nBits << " bits/simbolo -> " << n << " simbolos, " << M << " distintos\n"
         << "[Fuente] H=" << mf.entropia << " bits/simbolo (max " << mf.entropiaMaxima << "), KL(P||U)=" << mf.klUniformeN << "\n";

    // ===== ETAPA 2: CODIFICACIÓN (uniforme / no uniforme / adaptable) =====
    // Cada flujo se mide apenas se codifica y se libera si ya no puede ser el elegido: con archivos grandes cada uno
    // ocupa cientos de MB y tenerlos todos a la vez podía agotar la memoria
    int fijo = op.metodo == "uniforme" ? METODO_UNIFORME : op.metodo == "huffman" ? METODO_HUFFMAN
             : op.metodo == "lzw" ? METODO_LZW : -1;
    Codificado* codigos[3];
    uint64_t totales[3], bytesCarga[3];
    int mejorHasta = -1;
    auto medir = [&](int k) {
        bytesCarga[k] = codigos[k]->carga.size();
        totales[k] = compresor.tamanoCabecera(*codigos[k], f) + bytesCarga[k];
        if (fijo >= 0) {
            if (k != fijo) vector<uint8_t>().swap(codigos[k]->carga);
        } else if (mejorHasta < 0 || totales[k] < totales[mejorHasta]) {
            if (mejorHasta >= 0) vector<uint8_t>().swap(codigos[mejorHasta]->carga);
            mejorHasta = k;
        } else {
            vector<uint8_t>().swap(codigos[k]->carga);
        }
    };
    t = chrono::steady_clock::now();
    Codificado cu = compresor.codificarUniforme(f);
    codigos[0] = &cu;
    medir(0);
    Codificado ch = compresor.codificarHuffman(f);
    codigos[1] = &ch;
    medir(1);
    double tCodificacion = ms(t);
    t = chrono::steady_clock::now();
    Codificado cl = compresor.codificarLZW(f, op.entradasDiccionario, op.pasosTraza);
    codigos[2] = &cl;
    medir(2);
    double tLZW = ms(t);

    vector<uint8_t> Luniforme(M, (uint8_t)cu.anchoUniforme);
    vector<uint8_t> LhuffmanMax = Compresor::longitudesHuffman(f.frecuencias, false);
    MetricasCodigo mu = Sistema::metricasCodigo("Uniforme", P, Luniforme, mf.entropia);
    MetricasCodigo mh = Sistema::metricasCodigo("Huffman", P, ch.longitudes, mf.entropia);
    MetricasCodigo mhMax = Sistema::metricasCodigo("Huffman (empates abajo)", P, LhuffmanMax, mf.entropia);
    double lzwMedia = (double)cl.bitsCarga / n;
    vector<uint64_t> codigosHuffman = Compresor::codigosCanonicos(ch.longitudes);


    int elegido;
    if (op.metodo == "uniforme") elegido = METODO_UNIFORME;
    else if (op.metodo == "huffman") elegido = METODO_HUFFMAN;
    else if (op.metodo == "lzw") elegido = METODO_LZW;
    else elegido = (int)(min_element(totales, totales + 3) - totales);  // adaptable: el más pequeño
    Codificado& c = *codigos[elegido];
    string flujoEnviado = bitsFlujo(c.carga, c.bitsCarga, 256);
    cout << "[Codificacion] Uniforme L=" << mu.longitudMedia << " | Huffman L=" << mh.longitudMedia
         << " sigma^2=" << mh.varianza << " | LZW " << lzwMedia << " bits/simbolo\n"
         << "[Codificacion] Metodo elegido: " << nombreMetodo(elegido) << (op.metodo == "auto" ? " (adaptable)" : "") << "\n";

    // ===== ETAPA 3: COMPRESIÓN (archivo autocontenido) =====
    fs::create_directories(op.salida);
    string ext = fs::path(op.ruta).extension().string();
    string rutaComprimido = (fs::path(op.salida) / "comprimido.ti6g").string();
    string rutaReconstruido = (fs::path(op.salida) / ("reconstruido" + ext)).string();
    string rutaCanal = (fs::path(op.salida) / ("reconstruido_canal" + ext)).string();
    if (!compresor.guardar(rutaComprimido, c, f)) { cerr << "No se pudo escribir " << rutaComprimido << "\n"; return 1; }
    uint64_t tamComprimido = fs::file_size(rutaComprimido);
    cout << "[Compresion] " << f.tamanoOriginal << " -> " << tamComprimido << " bytes ("
         << (double)f.tamanoOriginal / tamComprimido << ":1)\n";

    // ===== ETAPA 4: RECONSTRUCCIÓN (sólo a partir del archivo comprimido) =====
    t = chrono::steady_clock::now();
    vector<unsigned char> reconstruido;
    if (!compresor.descomprimirArchivo(rutaComprimido, reconstruido, error)) { cerr << error << "\n"; return 1; }
    double tReconstruccion = ms(t);
    bool exacto = reconstruido.size() == f.tamanoOriginal && equal(reconstruido.begin(), reconstruido.end(), f.datos.begin());
    {
        ofstream o(rutaReconstruido, ios::binary);
        o.write((const char*)reconstruido.data(), reconstruido.size());
    }
    uint64_t tamReconstruido = reconstruido.size();
    reconstruido.resize(min<uint64_t>(tamReconstruido, op.muestra * f.bytesPorSimbolo));  // sólo se muestra el inicio
    reconstruido.shrink_to_fit();
    cout << "[Reconstruccion] " << (exacto ? "Identica al original (sin perdida)" : "DIFERENTE al original") << "\n";

    // ===== ETAPA 5: CANAL BSC Y INFORMACIÓN MUTUA =====
    // Lo que obtiene el receptor con un flujo comprimido recibido: se decodifica símbolo por símbolo, se escribe el
    // archivo (⊥ se escribe como ceros) y se acumula I(X;Y) entre la fuente y lo recibido, sin guardar la secuencia
    auto recibirYMedir = [&](const uint8_t* carga, const string& ruta, vector<uint32_t>* muestra) {
        AcumuladorMutua acumulador(f.frecuencias, mf.entropia);
        ofstream o(ruta, ios::binary);
        vector<unsigned char> borrado(f.bytesPorSimbolo, 0);
        uint64_t i = 0, escritos = 0;
        compresor.decodificarCon(c, carga, c.bitsCarga, n, M, [&](uint32_t id) {
            acumulador.agregar(f.ids[i], id);
            const unsigned char* p = id < M ? f.simbolo(id) : borrado.data();
            uint64_t b = min<uint64_t>(f.bytesPorSimbolo, f.tamanoOriginal - escritos);
            o.write((const char*)p, b);
            escritos += b;
            if (muestra && muestra->size() < op.muestra) muestra->push_back(id);
            ++i;
        });
        return acumulador.resultado();
    };
    t = chrono::steady_clock::now();
    vector<uint8_t> cargaOriginal;  // flujo comprimido tal como sale del transmisor, para el control de error
    if (usarControl) cargaOriginal = c.carga;
    // El flujo se modifica en el lugar: desde aquí c.carga es lo que llega al receptor
    ResultadoCanal rc = Canal::transmitirBSC(c.carga, c.bitsCarga, op.p, op.semilla);
    MetricasMutua mm;
    vector<uint32_t> idsCanal;
    if (rc.errores01 + rc.errores10 > 0) {
        mm = recibirYMedir(c.carga.data(), rutaCanal, &idsCanal);  // idsCanal guarda sólo los primeros símbolos
    } else {
        mm.HX = mm.HY = mm.HXY = mm.IXY = mf.entropia;
    }
    double tCanal = ms(t);
    cout << "[Canal] BSC p=" << op.p << " | errores de bit: " << (rc.errores01 + rc.errores10)
         << " | I(X;Y) simbolos=" << mm.IXY << " de H(X)=" << mf.entropia << "\n";

    // ===== ETAPA 6: CONTROL DE ERROR (código de bloque lineal: corrección FEC y retransmisión ARQ) =====
    // El mismo flujo comprimido pasa por el mismo BSC (misma p y semilla), ahora protegido con el código
    struct Recuperacion {
        ResultadoControl r;
        MetricasMutua mm;
        string archivo;
        bool identico = true;
        double ms = 0;
    };
    Recuperacion recFEC, recARQ;
    if (usarControl) {
        auto recuperar = [&](Recuperacion& x, vector<uint8_t>& recuperada, const string& sufijo) {
            x.identico = x.r.bitsErroneos == 0;
            if (x.identico) {  // el flujo comprimido llegó intacto: la reconstrucción es la original
                x.mm.HX = x.mm.HY = x.mm.HXY = x.mm.IXY = mf.entropia;
                return;
            }
            x.archivo = (fs::path(op.salida) / ("reconstruido_" + sufijo + ext)).string();
            x.mm = recibirYMedir(recuperada.data(), x.archivo, nullptr);
        };
        vector<uint8_t> recuperada;
        t = chrono::steady_clock::now();
        recFEC.r = ControlError::fec(codigo, cargaOriginal, c.bitsCarga, op.p, op.semilla, recuperada);
        recuperar(recFEC, recuperada, "fec");
        recFEC.ms = ms(t);
        t = chrono::steady_clock::now();
        recARQ.r = ControlError::arq(codigo, cargaOriginal, c.bitsCarga, op.p, op.semilla, op.arqMax, recuperada);
        recuperar(recARQ, recuperada, "arq");
        recARQ.ms = ms(t);
        cout << "[Control de error] " << codigo.nombre << " d_min=" << codigo.dMin << " corrige " << codigo.t << "\n"
             << "[Control de error] FEC: " << recFEC.r.erroresCanal << " errores en el canal -> " << recFEC.r.bitsErroneos
             << " bits errados | ARQ: " << recARQ.r.transmisiones - recARQ.r.bloques << " retransmisiones -> "
             << recARQ.r.bitsErroneos << " bits errados\n";
    }

    // ===== RESULTADOS PARA LA INTERFAZ =====
    if (op.json.empty()) return exacto ? 0 : 3;
    ofstream archivoJson(op.json);
    Json j(archivoJson);
    j.objeto().str("modo", "completo");

    j.objeto("fuente")
     .str("ruta", op.ruta).str("tipo", f.tipo).str("formato", f.formato)
     .ent("tamano_original", f.tamanoOriginal).ent("n_bits", op.nBits).ent("bytes_por_simbolo", f.bytesPorSimbolo)
     .ent("simbolos", n).ent("alfabeto", M).ent("relleno_bits", (f.datos.size() - f.tamanoOriginal) * 8)
     .num("entropia", mf.entropia).num("entropia_maxima", mf.entropiaMaxima)
     .num("entropia_max_alfabeto", mf.entropiaMaxAlfabeto).num("entropia_por_bit", mf.entropia / op.nBits)
     .num("kl_uniforme_n", mf.klUniformeN).num("kl_uniforme_m", mf.klUniformeM)
     .num("redundancia", mf.redundancia).num("info_min", mf.infoMin).num("info_max", mf.infoMax)
     .num("info_total_bits", mf.infoTotalBits).num("limite_shannon_bytes", mf.infoTotalBits / 8)
     .fin();

    // Símbolos más frecuentes con su cantidad de información I(x) = -log2 P(x)
    vector<uint32_t> orden(M);
    for (uint32_t k = 0; k < M; ++k) orden[k] = k;
    size_t top = min<size_t>(20, M);
    partial_sort(orden.begin(), orden.begin() + top, orden.end(),
                 [&](uint32_t a, uint32_t b) { return f.frecuencias[a] > f.frecuencias[b]; });
    j.arreglo("top_simbolos");
    for (size_t i = 0; i < top; ++i) {
        uint32_t id = orden[i];
        j.objeto().ent("id", id)
         .str("hex", hexBloque(f.simbolo(id), f.bytesPorSimbolo)).str("ascii", asciiBloque(f.simbolo(id), f.bytesPorSimbolo))
         .ent("frecuencia", f.frecuencias[id]).num("p", P[id]).num("informacion", -log2(P[id]))
         .ent("long_huffman", ch.longitudes[id])
         .str("codigo_huffman", ch.longitudes[id] <= 64 ? bitsTexto(codigosHuffman[id], ch.longitudes[id]) : "")
         .fin();
    }
    j.fin();

    // Distribución de longitudes del código de Huffman (base de la varianza)
    map<int, pair<uint64_t, double>> histograma;
    for (uint32_t k = 0; k < M; ++k) {
        histograma[ch.longitudes[k]].first++;
        histograma[ch.longitudes[k]].second += P[k];
    }
    j.arreglo("longitudes_huffman");
    for (auto& [L, v] : histograma) j.objeto().ent("longitud", L).ent("simbolos", v.first).num("probabilidad", v.second).fin();
    j.fin();

    j.objeto("codificacion");
    j.str("metodo_elegido", nombreMetodo(elegido)).booleano("adaptable", op.metodo == "auto");
    j.objeto("sin_codificar").num("longitud_media", op.nBits).num("varianza", 0).num("eficiencia", mf.entropia / op.nBits)
     .ent("bytes", f.tamanoOriginal).fin();
    const char* claves[3] = {"uniforme", "huffman", "lzw"};
    for (int k = 0; k < 3; ++k) {
        Codificado& ck = *codigos[k];
        j.objeto(claves[k]);
        if (k == METODO_UNIFORME) escribirMetricasCodigo(j, mu);
        else if (k == METODO_HUFFMAN) {
            escribirMetricasCodigo(j, mh);
            j.num("varianza_empates_abajo", mhMax.varianza).ent("longitud_max_empates_abajo", mhMax.longitudMax);
        } else {
            j.num("longitud_media", lzwMedia).num("varianza", cl.varianzaLZW).num("desviacion", sqrt(cl.varianzaLZW))
             .num("eficiencia", lzwMedia > 0 ? mf.entropia / lzwMedia : 0)
             .ent("codigos_emitidos", cl.codigosEmitidos).ent("diccionario_max", cl.capDiccionario)
             .ent("ancho_final", Compresor::bitsPara(min<uint64_t>(M + cl.codigosEmitidos, cl.capDiccionario)));
        }
        uint64_t cab = compresor.tamanoCabecera(ck, f);
        j.ent("bits_carga", ck.bitsCarga).ent("bytes_carga", bytesCarga[k]).ent("bytes_cabecera", cab)
         .ent("bytes_total", totales[k]).num("razon", (double)f.tamanoOriginal / totales[k])
         .fin();
    }
    j.fin();

    // Secuencias: fuente -> códigos -> reconstrucción -> canal
    j.arreglo("muestra");
    for (uint64_t i = 0; i < min<uint64_t>(op.muestra, n); ++i) {
        uint32_t id = f.ids[i];
        const unsigned char* rec = reconstruido.data() + i * f.bytesPorSimbolo;
        size_t largoRec = min<uint64_t>(f.bytesPorSimbolo, reconstruido.size() - i * f.bytesPorSimbolo);
        j.objeto().ent("i", i).ent("id", id)
         .str("hex", hexBloque(f.simbolo(id), f.bytesPorSimbolo)).str("ascii", asciiBloque(f.simbolo(id), f.bytesPorSimbolo))
         .num("p", P[id]).num("informacion", -log2(P[id]))
         .str("uniforme", bitsTexto(id, cu.anchoUniforme))
         .str("huffman", ch.longitudes[id] <= 64 ? bitsTexto(codigosHuffman[id], ch.longitudes[id]) : "")
         .str("reconstruido", hexBloque(rec, largoRec));
        if (!idsCanal.empty())
            j.str("canal", idsCanal[i] < M ? hexBloque(f.simbolo(idsCanal[i]), f.bytesPorSimbolo) : "⊥ (borrado)");
        j.fin();
    }
    j.fin();

    j.arreglo("traza_lzw");
    for (size_t i = 0; i < cl.traza.size(); ++i) {
        const PasoLZW& p = cl.traza[i];
        j.objeto().ent("paso", i + 1).ent("posicion", p.inicio).ent("largo", p.largo)
         .str("patron", vistaSecuencia(f, p.inicio, p.largo, false))
         .str("patron_ascii", vistaSecuencia(f, p.inicio, p.largo, true))
         .str("siguiente", p.siguiente == UINT32_MAX ? "fin" : hexBloque(f.simbolo(p.siguiente), f.bytesPorSimbolo, f.bytesPorSimbolo <= 2 ? 2 : 4))
         .ent("codigo", p.codigo).ent("ancho", p.ancho).ent("nueva_entrada", p.nuevaEntrada)
         .fin();
    }
    j.fin();

    j.objeto("compresion")
     .str("archivo", rutaComprimido).ent("bytes", tamComprimido)
     .ent("bytes_cabecera", compresor.tamanoCabecera(c, f)).ent("bytes_carga", bytesCarga[elegido])
     .num("razon", (double)f.tamanoOriginal / tamComprimido)
     .num("ahorro", 1.0 - (double)tamComprimido / f.tamanoOriginal)
     .num("bits_por_byte", 8.0 * tamComprimido / f.tamanoOriginal)
     .str("flujo_bits", flujoEnviado)
     .fin();

    j.objeto("reconstruccion")
     .str("archivo", rutaReconstruido).ent("bytes", tamReconstruido).booleano("identico", exacto)
     .fin();

    j.objeto("canal")
     .num("p", rc.p).ent("semilla", op.semilla).ent("bits", rc.bits).ent("unos", rc.unosX)
     .ent("errores_0a1", rc.errores01).ent("errores_1a0", rc.errores10).num("tasa_error_bit", rc.tasaErrorBit)
     .num("px0", rc.px[0]).num("px1", rc.px[1]).num("py0", rc.py[0]).num("py1", rc.py[1])
     .num("HX", rc.HX).num("HY", rc.HY).num("HY_dado_X", rc.HYdadoX).num("HX_dado_Y", rc.HXdadoY)
     .num("IXY", rc.IXY).num("IXY_medida", rc.IXYmedida).num("capacidad", rc.capacidad);
    j.arreglo("transicion");
    for (int x = 0; x < 2; ++x) j.arreglo().num(nullptr, rc.transicion[x][0]).num(nullptr, rc.transicion[x][1]).fin();
    j.fin().arreglo("transicion_medida");
    for (int x = 0; x < 2; ++x) j.arreglo().num(nullptr, rc.transicionMedida[x][0]).num(nullptr, rc.transicionMedida[x][1]).fin();
    j.fin().arreglo("conjunta");
    for (int x = 0; x < 2; ++x) j.arreglo().num(nullptr, rc.conjunta[x][0]).num(nullptr, rc.conjunta[x][1]).fin();
    j.fin();
    j.str("flujo_recibido", bitsFlujo(c.carga, c.bitsCarga, 256))
     .num("simbolos_HX", mm.HX).num("simbolos_HY", mm.HY).num("simbolos_HXY", mm.HXY)
     .num("simbolos_IXY", mm.IXY).num("simbolos_HX_dado_Y", mm.HXdadoY)
     .ent("simbolos_errados", mm.errores).num("tasa_error_simbolo", mm.tasaErrorSimbolo)
     .str("archivo", idsCanal.empty() ? "" : rutaCanal)
     .fin();

    if (usarControl) {
        const int nc = codigo.n, kc = codigo.k, rc_ = nc - kc;
        const double R = (double)kc / nc;
        j.objeto("control_error");

        // Estructura del código: P, G = [P | I_k], H = [I_{n-k} | P^T], palabras y síndromes
        j.objeto("codigo")
         .str("nombre", codigo.nombre).str("especificacion", op.fec).ent("n", nc).ent("k", kc).num("tasa", R)
         .ent("d_min", codigo.dMin).ent("corrige", codigo.t).ent("detecta", codigo.dMin - 1)
         .booleano("perfecto", codigo.perfecto()).ent("d_cota", codigo.dCota).str("construccion", codigo.construccion)
         .ent("d_mejor", codigo.dMejor).booleano("d_mejor_probado", codigo.dMejorProbado)
         .booleano("GHt_cero", all_of(codigo.G.begin(), codigo.G.end(), [&](uint32_t g) { return codigo.sindrome(g) == 0; }));
        j.arreglo("P");
        for (uint32_t x : codigo.P) j.str(nullptr, bitsDe(x, rc_));
        j.fin().arreglo("G");
        for (uint32_t x : codigo.G) j.str(nullptr, bitsDe(x, nc));
        j.fin().arreglo("H");
        for (uint32_t x : codigo.H) j.str(nullptr, bitsDe(x, nc));
        j.fin();
        const uint32_t totalPalabras = 1u << kc, totalSindromes = 1u << rc_;
        j.ent("total_palabras", totalPalabras).arreglo("palabras");
        for (uint32_t m = 0; m < min<uint32_t>(totalPalabras, 64); ++m) {
            uint32_t cw = codigo.codificar(m);
            j.objeto().str("m", bitsDe(m, kc)).str("b", bitsDe(cw >> kc, rc_)).str("c", bitsDe(cw, nc))
             .ent("peso", __builtin_popcount(cw)).fin();
        }
        j.fin().arreglo("distribucion_pesos");
        for (int w = 0; w <= nc; ++w)
            if (codigo.pesos[w]) j.objeto().ent("peso", w).ent("palabras", codigo.pesos[w]).fin();
        j.fin();
        // Tabla de síndromes ordenada por el peso del error: primero s = 0, luego los errores de un bit por posición
        vector<uint32_t> sindromes(totalSindromes);
        for (uint32_t s = 0; s < totalSindromes; ++s) sindromes[s] = s;
        sort(sindromes.begin(), sindromes.end(), [&](uint32_t a, uint32_t b) {
            int pa = __builtin_popcount(codigo.lider[a]), pb = __builtin_popcount(codigo.lider[b]);
            return pa != pb ? pa < pb : codigo.lider[a] > codigo.lider[b];
        });
        j.ent("total_sindromes", totalSindromes).arreglo("sindromes");
        for (uint32_t i = 0; i < min<uint32_t>(totalSindromes, 64); ++i) {
            uint32_t s = sindromes[i], e = codigo.lider[s];
            int peso = __builtin_popcount(e);
            j.objeto().str("s", bitsDe(s, rc_)).str("e", bitsDe(e, nc)).ent("peso", peso)
             .ent("posicion", peso == 1 ? nc - __builtin_ctz(e) : 0).fin();
        }
        j.fin().arreglo("lideres_por_peso");
        for (int w = 0; w <= nc; ++w)
            if (codigo.lideresPorPeso[w]) j.objeto().ent("peso", w).ent("lideres", codigo.lideresPorPeso[w]).fin();
        j.fin().fin();

        // Valores teóricos por bloque en un BSC con probabilidad p
        const double pLimpio = pow(1 - op.p, nc);
        const double pNoDetectado = codigo.probabilidadNoDetectado(op.p);
        const double pRetransmitir = max(0.0, 1 - pLimpio - pNoDetectado);  // s != 0
        const double qM = pow(pRetransmitir, op.arqMax);
        const double esperadas = pRetransmitir < 1 ? (1 - qM) / (1 - pRetransmitir) : op.arqMax;
        j.objeto("teoria")
         .num("p", op.p).num("tasa", R).num("capacidad", rc.capacidad).booleano("tasa_menor_capacidad", R < rc.capacidad)
         .num("p_bloque_sin_control", 1 - pow(1 - op.p, kc))
         .num("p_bloque_fec", max(0.0, 1 - codigo.probabilidadCorrecto(op.p)))
         .num("p_no_detectado", pNoDetectado).num("p_retransmitir", pRetransmitir)
         .num("transmisiones_esperadas", esperadas).num("eficiencia_arq", R / esperadas)
         .num("p_bloque_arq", min(1.0, pNoDetectado * esperadas + qM))
         .ent("arq_max", op.arqMax)
         .fin();

        // Sin control de error: lo que ya pasó por el canal en la etapa 5
        uint64_t erroresSin = rc.errores01 + rc.errores10;
        j.objeto("sin_control")
         .ent("bits_mensaje", c.bitsCarga).ent("bits_enviados", c.bitsCarga).ent("errores_canal", erroresSin)
         .num("ber_canal", rc.tasaErrorBit).ent("bits_erroneos", erroresSin).num("ber_residual", rc.tasaErrorBit)
         .num("IXY_bit", rc.IXYmedida).num("simbolos_HY", mm.HY).num("simbolos_HXY", mm.HXY).num("simbolos_IXY", mm.IXY)
         .num("simbolos_HX_dado_Y", mm.HXdadoY).ent("simbolos_errados", mm.errores).num("tasa_error_simbolo", mm.tasaErrorSimbolo)
         .booleano("identico", erroresSin == 0).str("archivo", idsCanal.empty() ? "" : rutaCanal)
         .fin();

        auto escribirControl = [&](const char* clave, const Recuperacion& x, bool esARQ) {
            const ResultadoControl& r = x.r;
            j.objeto(clave)
             .ent("bits_mensaje", r.bitsMensaje).ent("bloques", r.bloques).ent("relleno_bits", r.relleno)
             .ent("transmisiones", r.transmisiones).ent("retransmisiones", r.transmisiones - r.bloques)
             .ent("bits_enviados", r.bitsEnviados).ent("errores_canal", r.erroresCanal)
             .num("ber_canal", r.bitsEnviados ? (double)r.erroresCanal / r.bitsEnviados : 0)
             .ent("bloques_con_error", r.bloquesConError).ent("bloques_detectados", r.bloquesDetectados)
             .ent("bloques_no_detectados", r.bloquesNoDetectados).ent("bloques_corregidos", r.bloquesCorregidos)
             .ent("bloques_mal_corregidos", r.bloquesMalCorregidos).ent("bloques_fallidos", r.bloquesFallidos)
             .ent("bits_corregidos", r.bitsCorregidos).ent("bits_erroneos", r.bitsErroneos)
             .num("ber_residual", r.bitsMensaje ? (double)r.bitsErroneos / r.bitsMensaje : 0)
             .num("eficiencia", r.bitsEnviados ? (double)r.bitsMensaje / r.bitsEnviados : 0)
             .num("IXY_bit", r.IXYbit).num("simbolos_HY", x.mm.HY).num("simbolos_HXY", x.mm.HXY)
             .num("simbolos_IXY", x.mm.IXY).num("simbolos_HX_dado_Y", x.mm.HXdadoY)
             .ent("simbolos_errados", x.mm.errores).num("tasa_error_simbolo", x.mm.tasaErrorSimbolo)
             .booleano("identico", x.identico).str("archivo", x.archivo).num("tiempo_ms", x.ms);
            j.arreglo("muestra");
            for (const MuestraBloque& b : r.muestra) {
                string estado;
                if (esARQ) {
                    if (b.sFinal) estado = "Falló: " + to_string(b.intentos) + " envíos con error";
                    else if (b.eFinal) estado = "Error no detectado";
                    else if (b.intentos > 1)
                        estado = "Correcto tras " + to_string(b.intentos - 1) + (b.intentos > 2 ? " retransmisiones" : " retransmisión");
                    else estado = "Sin error";
                } else {
                    if (!b.e) estado = "Sin error";
                    else if (!b.s) estado = "Error no detectado";
                    else if (b.cEstimado == b.c) estado = "Corregido";
                    else estado = "Mal corregido";
                }
                j.objeto().ent("bloque", b.indice + 1).str("m", bitsDe(b.m, kc)).str("c", bitsDe(b.c, nc))
                 .str("e", bitsDe(b.e, nc)).str("r", bitsDe(b.r, nc)).str("s", bitsDe(b.s, rc_))
                 .str("e_est", bitsDe(b.eEstimado, nc)).str("c_est", bitsDe(b.cEstimado, nc)).str("m_est", bitsDe(b.mEstimado, kc))
                 .ent("intentos", b.intentos).str("estado", estado).fin();
            }
            j.fin().fin();
        };
        escribirControl("fec", recFEC, false);
        escribirControl("arq", recARQ, true);
        j.fin();
    }

    j.objeto("tiempos_ms")
     .num("fuente", tFuente).num("uniforme_huffman", tCodificacion).num("lzw", tLZW)
     .num("reconstruccion", tReconstruccion).num("canal", tCanal)
     .num("fec", recFEC.ms).num("arq", recARQ.ms)
     .fin();

    j.fin();
    archivoJson << "\n";
    return exacto ? 0 : 3;
}
