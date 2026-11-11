# Sistema Adaptable de Codificación y Compresión 5G/6G

Este repositorio contiene un sistema desarrollado en C++ diseñado para fragmentar, analizar estadísticamente, comprimir y reconstruir grandes volúmenes de datos (audio, video, texto, imagen). El proyecto está orientado a simular las necesidades de procesamiento masivo en redes de telecomunicaciones modernas (5G y 6G), soportando un tamaño de símbolo adaptable desde 8 hasta 32768 bits.

## Arquitectura del Proyecto

El código fuente está estructurado siguiendo las mejores prácticas de C++, separando las definiciones de las implementaciones:

- **`include/`**: Contiene los archivos de cabecera (`.h`) con las definiciones y contratos de las clases.
- **`src/`**: Aloja los archivos de código fuente (`.cpp`) donde reside la lógica algorítmica y matemática.
- **`data/`**: Archivos de prueba. Las salidas de cada ejecución van a `data/salida/` (ignorada en git).
- **`app.py`**: Interfaz web en Streamlit que muestra cada etapa del proceso con sus métricas e interpretación.

## Módulos Principales

El flujo de procesamiento sigue la cadena **Fuente → Codificación → Compresión → Canal → Reconstrucción**:

1.  **ProcesadorFuente**: lee el archivo en modo binario y lo divide en bloques exactos de $N$ bits ($N$ múltiplo de 8, de 8 a 32768). El último bloque se completa con ceros y el tamaño original se guarda para quitarlos al reconstruir. Construye el alfabeto (símbolos distintos) con una tabla hash, calcula $P(x_k)$ y detecta el tipo de fuente (audio, video, imagen, texto) por la firma del archivo.

2.  **Compresor**: implementa tres codificaciones sobre los índices de los símbolos:
    - _Uniforme_: código de longitud fija $\lceil \log_2 M \rceil$ bits ($\sigma^2 = 0$).
    - _Huffman (no uniforme)_: código canónico de **mínima varianza** (en los empates el nodo combinado se ubica lo más arriba posible). También se calcula la variante con los empates abajo para comparar varianzas.
    - _LZW (adaptable)_: diccionario que arranca con el alfabeto de la fuente y crece al vuelo, con códigos de ancho variable.
    - En modo **adaptable** se prueban los tres y se guarda el más pequeño. El archivo `.ti6g` es autocontenido (cabecera + alfabeto + tabla + datos), y la reconstrucción se hace leyendo sólo ese archivo.

3.  **Canal**: canal binario simétrico (discreto, sin memoria) con probabilidad de error $p$. Calcula la matriz de transición $P(y_k|x_j)$, la conjunta $P(x_j,y_k)$, $P(y_k)$, $H(Y|X)$, $I(X;Y)$ y la capacidad $C = 1 - H_b(p)$, tanto en teoría como medidas en la transmisión simulada.

4.  **Sistema**: métricas de Shannon:
    - **Cantidad de información** $I(x_k) = -\log_2 P(x_k)$ y **entropía** $H(X)$.
    - **Divergencia KL** $D(P\|U) = N - H$ respecto a la fuente uniforme.
    - **Varianza de la codificación** $\sigma^2 = \sum_k P_k (L_k - \bar L)^2$, longitud media $\bar L$ y eficiencia $H/\bar L$.
    - **Información mutua** $I(X;Y) = H(X) + H(Y) - H(X,Y)$ entre la secuencia fuente y la reconstruida tras el canal.

## Agente de IA: DQN con detección de cambios

Decide **qué codificación (Uniforme, Huffman o LZW) y qué N (8 … 2048) usar para un archivo**, siguiendo el esquema del documento
(*agente RL/DRL — PPO / SAC / DQN + mecanismo adaptativo*, con *Change Detection → sin cambio: política actual / cambio: adaptación RL*).

1. El archivo se recorre como un flujo de bloques. En cada bloque el agente observa el estado (9 estadísticas: entropía con
   N = 8 y N = 16, bytes distintos, texto, ceros, repeticiones, etc.), propone las 3 codificaciones de mayor Q, comprime el bloque
   con ellas y aprende de la recompensa (fracción de bits ahorrados).
2. Red Q de una capa oculta (64 ReLU, Adam), repetición de experiencias (1000), ε-greedy, γ = 0. Page-Hinkley sobre el error de
   predicción detecta si el contenido del archivo cambia; entonces vacía la memoria y vuelve a explorar.
3. Para el archivo completo, en cada método parte del N con mayor Q y de N = 8 y prueba N/4 … 4N sobre el archivo completo hasta que
   deja de mejorar. Gana el menor tamaño; si otra queda a menos del 1%, la de menor varianza σ².
4. Comprime el archivo con la decisión y lo reconstruye sólo desde el comprimido.

Validación con 323 archivos reales (código, texto, imágenes, binarios, datos, audio): en el 100% eligió la misma codificación y N
que la búsqueda exhaustiva de las 27 combinaciones, con 12.5 compresiones del archivo completo en promedio (en vez de 27) y todas
las reconstrucciones idénticas.

## Transmisión por 5G NR y requisitos de 5G y 6G

La pestaña **8 · 5G / 6G** muestra, sin tecnicismos, cuánto tardaría en llegar el archivo comprimido (el del agente si se ejecutó)
frente al original: con las velocidades y la demora que exigen los estándares de 5G (IMT-2020) y 6G (IMT-2030), y en una red 5G
real de 3.5 GHz con buena señal, además de una tabla con lo que pide cada estándar. Por debajo, el núcleo C++ calcula la red real
con las normas de 3GPP (el detalle completo se obtiene con `--enlace` desde la terminal):

| Qué se calcula | Referencia |
|---|---|
| Recursos por ancho de banda: n28 (700 MHz, 20 MHz, 15 kHz, 106 RB, FDD), n78 (3.5 GHz, 100 MHz, 30 kHz, 273 RB, TDD), n257 (28 GHz, 400 MHz, 120 kHz, 264 RB, TDD) | TS 38.101-1/-2, TS 38.211 |
| Tablas MCS (hasta 64QAM y hasta 256QAM) y tamaño del bloque de transporte (TBS) | TS 38.214 §5.1.3 |
| CRC, grafo base del LDPC, segmentación en bloques de código, tamaño de elevación Zc y relleno | TS 38.212 §5.2.2, §7.2 |
| Tasa pico | TS 38.306 §4.1.2 |
| Velocidad, latencia y confiabilidad de 5G | ITU-R M.2410 (IMT-2020) |
| Valores de 6G | ITU-R M.2160 (IMT-2030) |

Supuestos que no fija el estándar: el MCS se elige como el más alto cuya eficiencia espectral no supera log₂(1 + SNR/Γ)
(Γ = 1.5 dB en eMBB y 4 dB en URLLC); PDSCH de 13 símbolos (eMBB) o mini-slot de 7 (URLLC); DMRS de 12 RE por PRB; TDD con
patrón DDDSU; BLER objetivo 10% (eMBB) o 10⁻⁵ (URLLC); sin cabeceras de PDCP, RLC ni MAC. 6G todavía no tiene especificación
técnica de 3GPP, por eso se compara con los valores de IMT-2030.

El cálculo del TBS y de la segmentación se verificó contra una segunda implementación independiente con aritmética exacta
(16 848 casos, 0 diferencias); la tasa pico de n78 con 4 capas y 256QAM da 2.34 Gbit/s, el valor publicado.

## Compilación y Ejecución

```bash
python -m venv venv
# Windows: .\venv\Scripts\activate      Linux/Mac: source venv/bin/activate
pip install -r requirements.txt
streamlit run app.py
```

En la barra lateral se elige el archivo (de `data/`, incluidas sus subcarpetas, o subido), N, el método y el ruido del canal;
los resultados se recalculan solos al cambiar cualquiera de ellos (archivos de más de 32 MB piden confirmar con **Procesar
archivo**). El agente (pestaña 7) se ejecuta con su botón; la pestaña 5G/6G se calcula sola.

La interfaz compila el núcleo automáticamente con `g++` si el ejecutable no existe o si cambió el código. Para compilarlo a mano:

```bash
g++ -O2 -std=c++17 src/*.cpp -o codificador_6g.exe
```

Uso desde la terminal:

```bash
./codificador_6g.exe data/archivo.mp3 1024 --metodo auto --p 0.001 --salida data/salida/prueba --json resultados.json
./codificador_6g.exe data/archivo.mp3 --barrido 8,16,32,64,128,256,512,1024,2048 --json barrido.json
./codificador_6g.exe data/archivo.mp3 --agente --salida data/salida/agente --json agente.json
./codificador_6g.exe data/archivo.mp3 --enlace --comprimido data/salida/agente/agente.ti6g --snr 20 --capas 4 --servicio embb --json enlace.json
```

`data/ejemplo_varianza_huffman.txt` reproduce el ejemplo clásico $P = \{0.4, 0.2, 0.2, 0.1, 0.1\}$. Las dos variantes de Huffman tienen el mismo $\bar L = 2.2$, pero $\sigma^2 = 0.16$ frente a $1.36$.

# Enunciado codificación, decodificación, compresión adaptable 5G y 6G

- Diseñe un sistema adaptable para N bits (8,16,32,64,128,256,512,1024,2048) (bits/simbolos) </br> </br>
  Tipo fuente: (audio, video, texto, imagen), grandes volumnes de datos </br> </br>
  Secuencias (Codificadas/ decodificadas/ comprimidas/ descomprimidas) -> Mostrar todo el proceso </br> </br>
  Codificación -> Uniforme/ No uniforme/ Se puede usar otro </br> </br>
  Identificar y eliminar redundancias -> Controlada
  </br> </br>
  Reconsturcción -> Información </br> </br>
  Fuente - Codificación - Compresión - Reconstrucción (Decodifica, descomprime, reconstruye)
  </br> </br>
  Medir e interpretar: Cantidad información (I), Entropia (H), Informacón mutua I(x,y), divergencia KL,

- N bits (4096, 8192, 16384, 32768) (bits simbolos)
