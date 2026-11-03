#ifndef SISTEMA_H
#define SISTEMA_H

#include <cstdint>
#include <string>
#include <vector>
#include "ProcesadorFuente.h"

// Métricas de la fuente (Shannon)
struct MetricasFuente {
    uint64_t n = 0, M = 0;
    double entropia = 0;            // H(X) = -sum p log2 p   [bits/símbolo]
    double entropiaMaxima = 0;      // log2(2^N) = N
    double entropiaMaxAlfabeto = 0; // log2(M)
    double klUniformeN = 0;         // D(P || U_{2^N}) = N - H
    double klUniformeM = 0;         // D(P || U_M) = log2 M - H
    double redundancia = 0;         // 1 - H/N
    double infoMin = 0, infoMax = 0;// I(x) = -log2 p(x) del símbolo más y menos probable
    double infoTotalBits = 0;       // n * H
};

// Métricas de un código: longitud media, varianza sigma^2 = sum p_k (L_k - L)^2, eficiencia H/L
struct MetricasCodigo {
    std::string nombre;
    double longitudMedia = 0;
    double varianza = 0;
    double desviacion = 0;
    double eficiencia = 0;
    double kraft = 0;
    int longitudMin = 0, longitudMax = 0;
};

// Información mutua entre la secuencia fuente X y la secuencia reconstruida Y
struct MetricasMutua {
    double HX = 0, HY = 0, HXY = 0, IXY = 0;
    double HXdadoY = 0;   // equivocación
    uint64_t errores = 0;
    double tasaErrorSimbolo = 0;
};

class Sistema {
public:
    static MetricasFuente analizarFuente(const Fuente& f, const std::vector<double>& P);
    static MetricasCodigo metricasCodigo(const std::string& nombre, const std::vector<double>& P,
                                         const std::vector<uint8_t>& longitudes, double entropia);
    // Y puede contener el símbolo de borrado M (bloques perdidos por errores del canal)
    static MetricasMutua informacionMutua(const std::vector<uint32_t>& X, const std::vector<uint32_t>& Y,
                                          const std::vector<uint64_t>& frecuenciasX, double HX);
};

#endif
