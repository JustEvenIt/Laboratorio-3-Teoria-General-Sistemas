#ifndef CONTROL_ERROR_H
#define CONTROL_ERROR_H

#include <cstdint>
#include <string>
#include <vector>

// Código de bloque lineal sistemático (n, k) sobre GF(2):
//   mensaje m = [m_0 ... m_{k-1}], bits de paridad b = m P = [b_0 ... b_{n-k-1}]
//   palabra codificada c = [b | m] = m G,  con  G = [P | I_k]          (k x n)
//   matriz de verificación de paridad       H = [I_{n-k} | P^T]      ((n-k) x n),  G H^T = 0
//   recibido r = c + e,  síndrome s = r H^T = e H^T  (s = 0: no hay error detectable)
// Cada vector se guarda en un entero; su primer componente (c_0, m_0, s_0) es el bit más
// significativo, que es también el primero que se transmite.
class CodigoBloque {
public:
    std::string nombre;
    int n = 0, k = 0;
    std::vector<uint32_t> P;          // k filas de n-k bits
    std::vector<uint32_t> G;          // k filas de n bits
    std::vector<uint32_t> H;          // n-k filas de n bits
    int dMin = 0;                     // distancia mínima = menor peso de una palabra no nula
    int t = 0;                        // errores que corrige: (dMin - 1) / 2
    int dCota = 0;                    // mayor d_min posible para (n, k) según las cotas de Singleton, Hamming y Griesmer
    int dMejor = 0;                   // mayor d_min que encuentra la búsqueda de P para (n, k)
    bool dMejorProbado = false;       // se comprobó que ningún código (n, k) supera dMejor
    std::string construccion;         // "busqueda" (P elegida para n y k) o "matriz" (P dada)
    std::vector<uint64_t> pesos;      // A_w: palabras del código con peso w
    std::vector<uint32_t> lider;      // síndrome -> patrón de error de menor peso (líder de su clase lateral)
    std::vector<uint64_t> lideresPorPeso;
    std::vector<uint32_t> tablaCodificacion;  // m -> c (2^k entradas)

    // "n,k" (por ejemplo "7,4": se busca la P de mayor distancia mínima)
    // o "P:110,011,111,101" (filas de P separadas por coma)
    static bool crear(const std::string& especificacion, CodigoBloque& c, std::string& error);
    static int cotaDistancia(int n, int k);

    uint32_t codificar(uint32_t m) const { return tablaCodificacion[m]; }
    uint32_t sindrome(uint32_t r) const;
    bool perfecto() const;            // las esferas de radio t llenan todo el espacio: sum_{i<=t} C(n,i) = 2^(n-k)
    // En un BSC con probabilidad p, por bloque: FEC lo deja bien / ARQ no detecta el error
    double probabilidadCorrecto(double p) const;
    double probabilidadNoDetectado(double p) const;
};

// Un bloque de la transmisión, para mostrar el proceso
struct MuestraBloque {
    uint64_t indice = 0;
    uint32_t m = 0, c = 0, e = 0, r = 0, s = 0, eEstimado = 0, cEstimado = 0, mEstimado = 0;
    int intentos = 1;                 // ARQ: transmisiones del bloque
    uint32_t eFinal = 0, sFinal = 0;  // ARQ: e, r y s son del primer envío; éstos, del último
};

struct ResultadoControl {
    uint64_t bitsMensaje = 0, bloques = 0, relleno = 0;
    uint64_t transmisiones = 0;       // bloques enviados, incluidas las retransmisiones
    uint64_t bitsEnviados = 0;
    uint64_t erroresCanal = 0;        // bits invertidos por el canal
    uint64_t bloquesConError = 0;     // e != 0 (en ARQ, contando cada intento)
    uint64_t bloquesDetectados = 0;   // s != 0
    uint64_t bloquesNoDetectados = 0; // e != 0 pero e es una palabra del código: s = 0
    uint64_t bloquesCorregidos = 0;   // FEC: tenía error y quedó bien
    uint64_t bloquesMalCorregidos = 0;// FEC: el decodificador eligió otra palabra
    uint64_t bloquesFallidos = 0;     // ARQ: se agotaron los intentos
    uint64_t bitsCorregidos = 0;      // FEC: bits que invirtió el decodificador
    uint64_t bitsErroneos = 0;        // bits del mensaje distintos al original tras el control
    double IXYbit = 0;                // I(X;Y) por bit entre el flujo comprimido original y el recuperado
    std::vector<MuestraBloque> muestra;
};

class ControlError {
public:
    // FEC: cada bloque se corrige con la tabla de síndromes (decodificación por líder de clase lateral)
    static ResultadoControl fec(const CodigoBloque& cod, const std::vector<uint8_t>& carga, uint64_t bits,
                                double p, uint64_t semilla, std::vector<uint8_t>& recuperada);
    // ARQ: el código sólo detecta; si s != 0 el receptor pide retransmitir el bloque (canal de retorno ideal)
    static ResultadoControl arq(const CodigoBloque& cod, const std::vector<uint8_t>& carga, uint64_t bits,
                                double p, uint64_t semilla, int maxIntentos, std::vector<uint8_t>& recuperada);
    // I(X;Y) por bit entre dos flujos; también devuelve cuántos bits difieren
    static double informacionMutuaBits(const std::vector<uint8_t>& x, const std::vector<uint8_t>& y, uint64_t bits,
                                       uint64_t& diferentes);
};

std::string bitsDe(uint32_t valor, int largo);

#endif
