#ifndef AGENTE_RL_H
#define AGENTE_RL_H

#include <cstdint>
#include <string>
#include <vector>

// Generador pseudoaleatorio (splitmix64): el mismo agente da el mismo resultado con la misma semilla
struct Aleatorio {
    uint64_t estado;
    explicit Aleatorio(uint64_t semilla) : estado(semilla) {}
    uint64_t siguiente() {
        uint64_t z = (estado += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double uniforme() { return ((siguiente() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }  // (0,1)
    double normal();
};

// Prueba de Page-Hinkley de dos lados: detecta un cambio en la media de una señal
class PageHinkley {
private:
    double delta, lambda;
    int minimo;
    bool dosLados;
    uint64_t n = 0;
    double media = 0, sumaArriba = 0, minArriba = 0, sumaAbajo = 0, maxAbajo = 0;

public:
    // Con dosLados = false sólo avisa cuando la media sube (por ejemplo, cuando el error crece)
    PageHinkley(double delta = 0, double lambda = 1, int minimo = 1, bool dosLados = true)
        : delta(delta), lambda(lambda), minimo(minimo), dosLados(dosLados) {}
    bool actualizar(double x);
    void reiniciar();
};

// Red neuronal Q(s, ·): perceptrón de una capa oculta (ReLU) entrenado con Adam
class RedQ {
public:
    RedQ(int entradas, int ocultas, int salidas, double tasa, uint64_t semilla);
    std::vector<double> evaluar(const std::vector<double>& s) const;
    // Un paso de gradiente sobre un minilote: error cuadrático sólo en la acción tomada
    void entrenar(const std::vector<const std::vector<double>*>& estados, const std::vector<int>& acciones,
                  const std::vector<double>& objetivos);

private:
    int E, O, S;
    double tasa;
    uint64_t pasos = 0;
    std::vector<double> W1, b1, W2, b2;                      // parámetros
    std::vector<double> m1, v1, mb1, vb1, m2, v2, mb2, vb2;  // momentos de Adam
};

// Agente DQN que decide la codificación (método + N) de un archivo. Recorre el archivo como un flujo de bloques:
// estado = estadísticas del bloque, acción = una de las 27 combinaciones, recompensa = fracción de
// bits ahorrados. Como la codificación de un bloque no cambia los bloques siguientes, el objetivo es
// la recompensa inmediata (factor de descuento gamma = 0). Mecanismo adaptativo: Page-Hinkley sobre el
// error de predicción; si detecta un cambio, el agente olvida la memoria de experiencias y vuelve a explorar.
struct OpcionesAgente {
    std::string ruta, salida, json;
    size_t bytesBloque = 0;         // 0 = automático
    uint64_t pasos = 0;             // 0 = automático
    uint64_t semilla = 42;
    int candidatas = 3;             // codificaciones que se prueban por bloque (las de mayor Q); se envía la más pequeña
    int ocultas = 64;
    double tasa = 0.003;            // tasa de aprendizaje de Adam
    size_t memoria = 1000;          // experiencias guardadas (repetición de experiencias)
    int lote = 32;
    int actualizaciones = 8;        // pasos de gradiente por decisión
    double epsMin = 0.02, decaimiento = 0.97, epsCambio = 0.6;
    double delta = 0.02, lambda = 1.0;
    int minimoMuestras = 30;
    bool detectar = true;
};

int ejecutarAgente(const OpcionesAgente& op, const std::vector<unsigned char>& contenido);

#endif
