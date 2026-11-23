#ifndef NR_H
#define NR_H

#include <cstdint>
#include <random>
#include <vector>

// Capa física de 5G NR para la simulación: CRC, LDPC, adaptación de tasa, modulación y canal.
//   CRC y adaptación de tasa: 3GPP TS 38.212 §5.1, §5.4.2
//   LDPC (grafos base 1 y 2):  3GPP TS 38.212 §5.3.2
//   Modulación (QPSK a 1024QAM): 3GPP TS 38.211 §5.1
//   Tablas MCS:                3GPP TS 38.214 Tablas 5.1.3.1-1 a 5.1.3.1-4
// Los bits se guardan uno por byte (0 o 1). Los LLR son log P(b=0)/P(b=1): positivo significa 0.

namespace nr {

const uint8_t NULO = 2;  // bit de relleno (<NULL> en la norma)

// ---------- Tablas MCS ----------
struct EntradaMCS { int Qm; double R1024; };
// tabla: 1 = hasta 64QAM, 2 = hasta 256QAM, 3 = 64QAM de baja eficiencia (URLLC), 4 = hasta 1024QAM
const std::vector<EntradaMCS>& tablaMCS(int tabla);

// ---------- CRC (§5.1) ----------
enum class TipoCRC { CRC24A, CRC24B, CRC16 };
int largoCRC(TipoCRC t);
// Devuelve los L bits de paridad de 'bits' (sistemático, registro en cero, sin inversión)
std::vector<uint8_t> calcularCRC(const uint8_t* bits, size_t n, TipoCRC t);
bool verificarCRC(const uint8_t* bitsConCRC, size_t nTotal, TipoCRC t);

// ---------- LDPC (§5.3.2) ----------
class LDPC {
public:
    int grafo = 1, Zc = 0, iLS = 0;
    int filas = 0, columnas = 0, kb = 0;    // kb: columnas de información (22 o 10)
    int K = 0, N = 0;                       // K = kb·Zc bits de entrada; N = (columnas - 2)·Zc bits de salida
    struct Arista { int columna, desplazamiento; };
    std::vector<std::vector<Arista>> filasH;  // aristas de cada fila del grafo base, con P = V mod Zc

    // Código para (grafo, Zc); se arma una vez y queda en caché
    static const LDPC& obtener(int grafo, int Zc);

    // c: K bits de información (los de relleno valen 0). w: columnas·Zc bits con H·w = 0
    void codificar(const uint8_t* c, uint8_t* w) const;
    // L: columnas·Zc LLR (entrada: canal; salida: a posteriori). Min-sum normalizado, actualizando fila por fila.
    // 'filasActivas': filas del grafo que se usan (las de paridad que no se transmitieron no aportan).
    // Devuelve las iteraciones usadas; 'ok' indica si todas las ecuaciones de paridad activas se cumplen.
    int decodificar(std::vector<float>& L, int filasActivas, int maxIteraciones, bool& ok) const;
    bool cumpleParidad(const uint8_t* w, int filasActivas) const;

private:
    std::vector<uint64_t> inversaNucleo;    // inversa de la parte 4Zc x 4Zc de la paridad doble diagonal
    int palabrasNucleo = 0;
    void preparar();
};

int indiceConjunto(int Zc);  // iLS de la Tabla 5.3.2-1

// ---------- Segmentación y adaptación de tasa ----------
struct Segmentacion {
    int grafo = 1, Zc = 0, C = 1, Kprima = 0, K = 0, N = 0;
    int crcTB = 24, crcCB = 0;
};
Segmentacion segmentar(int64_t A, double R);   // §7.2.1, §7.2.2 y §5.2.2

// Bits de salida de cada bloque de código: E_r (§5.4.2.1)
std::vector<int> bitsPorBloque(int64_t G, int C, int Qm, int capas);
// Posición inicial k0 de la versión de redundancia (Tabla 5.4.2.1-2)
int inicioVersion(int rv, int grafo, int Ncb, int Zc);
// Selección de bits del buffer circular d (N bits, puede tener NULO) y entrelazado de bits (§5.4.2)
void adaptarTasa(const uint8_t* d, int N, int E, int k0, int Qm, uint8_t* f);
// Inverso: suma los LLR recibidos 'f' en el buffer 'buffer' (N posiciones), saltando los NULO de 'd'
void recuperarTasa(const float* f, int E, int k0, int Qm, const uint8_t* d, int N, float* buffer);

// ---------- Modulación (TS 38.211 §5.1) ----------
struct Complejo { float re, im; };
void modular(const uint8_t* bits, size_t nSimbolos, int Qm, Complejo* simbolos);
// LLR max-log de cada bit dado y = x + ruido con varianza compleja 'varianza' (por eje: varianza/2)
void demodular(const Complejo* y, const float* varianza, size_t nSimbolos, int Qm, float* llr);

// ---------- Canal ----------
// Canal con ruido gaussiano blanco aditivo (AWGN): a cada símbolo se le suma ruido. Con la SNR dada,
// la potencia media de la señal es 1 y la del ruido es N0 = 1/SNR.
struct Canal {
    double snr = 1;            // relación señal a ruido (lineal)
    std::mt19937_64 gen;
    std::normal_distribution<double> normal{0.0, 1.0};
    explicit Canal(uint64_t semilla) : gen(semilla) {}
    // Suma el ruido a los símbolos y devuelve la varianza del ruido de cada uno
    void transmitir(std::vector<Complejo>& x, std::vector<float>& varianza);
};

// ---------- Utilidades ----------
std::vector<uint8_t> bytesABits(const uint8_t* bytes, size_t n);
std::vector<uint8_t> bitsABytes(const uint8_t* bits, size_t nBits);

}  // namespace nr

#endif
