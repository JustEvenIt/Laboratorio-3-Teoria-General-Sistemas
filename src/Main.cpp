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
#include "../include/Json.h"
#include "../include/ProcesadorFuente.h"
#include "../include/Sistema.h"

using namespace std;
namespace fs = std::filesystem;

struct Opciones {
    string ruta;
    int nBits = 8;
    string metodo = "auto";        // auto | uniforme | huffman | lzw
    double p = 0.0;                // probabilidad de error del canal BSC
    uint64_t semilla = 42;
    string salida = "data/salida";
    string json;
    size_t pasosTraza = 200;
    size_t muestra = 16;
    uint64_t entradasDiccionario = 1 << 20;
    vector<int> barrido;           // modo comparación de varios N
    bool agente = false;           // modo agente: decide la mejor codificación
    OpcionesAgente ag;
};

static void uso() {
    cerr << "Uso: codificador_6g <archivo> <N bits> [--metodo auto|uniforme|huffman|lzw] [--p 0.001]\n"
            "                    [--semilla 42] [--salida DIR] [--json RUTA] [--traza 200] [--muestra 16]\n"
            "     codificador_6g <archivo> --barrido 8,16,32,... [--json RUTA]\n"
            "     codificador_6g <archivo> --agente [--bloque 0] [--pasos 0]\n"
            "                    [--semilla 42] [--candidatas 3] [--tasa 0.003] [--ocultas 64] [--memoria 1000] [--decaimiento 0.97]\n"
            "                    [--eps-cambio 0.6] [--eps-min 0.02] [--ph 0.02,1] [--sin-detector] [--salida DIR] [--json RUTA]\n"
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

    ProcesadorFuente procesador;
    Compresor compresor;
    vector<unsigned char> contenido;
    string error;
    if (!procesador.leerArchivo(op.ruta, contenido, error)) { cerr << error << "\n"; return 1; }
    if (contenido.empty()) { cerr << "El archivo esta vacio.\n"; return 1; }

    if (!op.barrido.empty()) return ejecutarBarrido(op, contenido);
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
    t = chrono::steady_clock::now();
    Codificado cu = compresor.codificarUniforme(f);
    Codificado ch = compresor.codificarHuffman(f);
    double tCodificacion = ms(t);
    t = chrono::steady_clock::now();
    Codificado cl = compresor.codificarLZW(f, op.entradasDiccionario, op.pasosTraza);
    double tLZW = ms(t);

    vector<uint8_t> Luniforme(M, (uint8_t)cu.anchoUniforme);
    vector<uint8_t> LhuffmanMax = Compresor::longitudesHuffman(f.frecuencias, false);
    MetricasCodigo mu = Sistema::metricasCodigo("Uniforme", P, Luniforme, mf.entropia);
    MetricasCodigo mh = Sistema::metricasCodigo("Huffman", P, ch.longitudes, mf.entropia);
    MetricasCodigo mhMax = Sistema::metricasCodigo("Huffman (empates abajo)", P, LhuffmanMax, mf.entropia);
    double lzwMedia = (double)cl.bitsCarga / n;
    vector<uint64_t> codigosHuffman = Compresor::codigosCanonicos(ch.longitudes);

    Codificado* codigos[3] = {&cu, &ch, &cl};
    uint64_t totales[3], bytesCarga[3];
    for (int k = 0; k < 3; ++k) {
        bytesCarga[k] = codigos[k]->carga.size();
        totales[k] = compresor.tamanoCabecera(*codigos[k], f) + bytesCarga[k];
    }

    int elegido;
    if (op.metodo == "uniforme") elegido = METODO_UNIFORME;
    else if (op.metodo == "huffman") elegido = METODO_HUFFMAN;
    else if (op.metodo == "lzw") elegido = METODO_LZW;
    else elegido = (int)(min_element(totales, totales + 3) - totales);  // adaptable: el más pequeño
    Codificado& c = *codigos[elegido];
    for (int k = 0; k < 3; ++k)
        if (k != elegido) vector<uint8_t>().swap(codigos[k]->carga);  // ya se midieron; liberar memoria
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
    t = chrono::steady_clock::now();
    // El flujo se modifica en el lugar: desde aquí c.carga es lo que llega al receptor
    ResultadoCanal rc = Canal::transmitirBSC(c.carga, c.bitsCarga, op.p, op.semilla);
    MetricasMutua mm;
    vector<uint32_t> idsCanal;
    if (rc.errores01 + rc.errores10 > 0) {
        idsCanal = compresor.decodificar(c, n, M);
        mm = Sistema::informacionMutua(f.ids, idsCanal, f.frecuencias, mf.entropia);
        ofstream o(rutaCanal, ios::binary);
        vector<unsigned char> bloque(f.bytesPorSimbolo, 0);
        uint64_t escritos = 0;
        for (uint32_t id : idsCanal) {
            const unsigned char* p = id < M ? f.simbolo(id) : bloque.data();
            uint64_t b = min<uint64_t>(f.bytesPorSimbolo, f.tamanoOriginal - escritos);
            o.write((const char*)p, b);
            escritos += b;
        }
    } else {
        mm.HX = mm.HY = mm.HXY = mm.IXY = mf.entropia;
    }
    double tCanal = ms(t);
    cout << "[Canal] BSC p=" << op.p << " | errores de bit: " << (rc.errores01 + rc.errores10)
         << " | I(X;Y) simbolos=" << mm.IXY << " de H(X)=" << mf.entropia << "\n";

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

    j.objeto("tiempos_ms")
     .num("fuente", tFuente).num("uniforme_huffman", tCodificacion).num("lzw", tLZW)
     .num("reconstruccion", tReconstruccion).num("canal", tCanal)
     .fin();

    j.fin();
    archivoJson << "\n";
    return exacto ? 0 : 3;
}
