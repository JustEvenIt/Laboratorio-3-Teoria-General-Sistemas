# Sistema Adaptable de Codificación y Compresión 5G/6G

Este repositorio contiene un sistema desarrollado en C++ diseñado para fragmentar, analizar estadísticamente, comprimir y reconstruir grandes volúmenes de datos (audio, video, texto, imagen). El proyecto está orientado a simular las necesidades de procesamiento masivo en redes de telecomunicaciones modernas (5G y 6G), soportando un tamaño de símbolo adaptable desde 8 hasta 32768 bits.

La guía [GUIA_EXPLICACION.md](GUIA_EXPLICACION.md) explica cada parte del programa para presentarlo, con resultados de los
archivos de prueba de `data/`.

## Arquitectura del Proyecto

El código fuente está estructurado siguiendo las mejores prácticas de C++, separando las definiciones de las implementaciones:

- **`include/`**: Contiene los archivos de cabecera (`.h`) con las definiciones y contratos de las clases.
- **`src/`**: Aloja los archivos de código fuente (`.cpp`) donde reside la lógica algorítmica y matemática.
- **`data/`**: Archivos de prueba grandes (más de 500 MB cada uno): texto (`wikipedia_enwik9_550MB.txt`, el volcado enwik9 de
  Wikipedia), audio (`chopin_baladas_fantasia_allegro.wav`), imagen (`nasa_blue_marble_21600x10800.bmp`, la Blue Marble de la
  NASA) y video (`tears_of_steel_1080p.webm`). Las salidas de cada ejecución van a `data/salida/` (ignorada en git).
  Por su tamaño no están en el repositorio. Se obtienen de: enwik9 en http://mattmahoney.net/dc/enwik9.zip; grabaciones de
  Chopin de dominio público de Musopen en https://archive.org/details/musopen-chopin (convertidas a WAV); la Blue Marble de
  21600 × 10800 de NASA Visible Earth en https://visibleearth.nasa.gov (convertida a BMP); y *Tears of Steel* en
  https://download.blender.org/demo/movies/ToS/tears_of_steel_1080p.webm.
- **`visual/`**: La figura animada del botón **Visualizar** (`viaje.html`), generada con `node visual/build.mjs`.
- **`app.py`**: Interfaz web en Streamlit que muestra cada etapa del proceso con sus métricas e interpretación.

## Módulos Principales

El flujo de procesamiento sigue la cadena **Fuente → Codificación → Compresión → Control de error → Canal → Reconstrucción**:

1.  **ProcesadorFuente**: lee el archivo en modo binario y lo divide en bloques exactos de $N$ bits ($N$ múltiplo de 8, de 8 a 32768). El último bloque se completa con ceros y el tamaño original se guarda para quitarlos al reconstruir. Construye el alfabeto (símbolos distintos) con una tabla hash, calcula $P(x_k)$ y detecta el tipo de fuente (audio, video, imagen, texto) por la firma del archivo.

2.  **Compresor**: implementa tres codificaciones sobre los índices de los símbolos:
    - _Uniforme_: código de longitud fija $\lceil \log_2 M \rceil$ bits ($\sigma^2 = 0$).
    - _Huffman (no uniforme)_: código canónico de **mínima varianza** (en los empates el nodo combinado se ubica lo más arriba posible). También se calcula la variante con los empates abajo para comparar varianzas.
    - _LZW (adaptable)_: diccionario que arranca con el alfabeto de la fuente y crece al vuelo, con códigos de ancho variable.
    - En modo **adaptable** se prueban los tres y se guarda el más pequeño. El archivo `.ti6g` es autocontenido (cabecera + alfabeto + tabla + datos), y la reconstrucción se hace leyendo sólo ese archivo.

3.  **Canal**: canal binario simétrico (discreto, sin memoria) con probabilidad de error $p$. Calcula la matriz de transición $P(y_k|x_j)$, la conjunta $P(x_j,y_k)$, $P(y_k)$, $H(Y|X)$, $I(X;Y)$ y la capacidad $C = 1 - H_b(p)$, tanto en teoría como medidas en la transmisión simulada.

4.  **ControlError**: código de bloque lineal sistemático (n, k): $c = [b \mid m] = mG$,
    $G = [P \mid I_k]$, $H = [I_{n-k} \mid P^T]$, $GH^T = 0$ y síndrome $s = rH^T$ con $r = c + e$. El código se define con
    **n y k** o con una $P$ cualquiera. Con n y k el programa busca la $P$ de mayor distancia mínima y dice si es la mayor
    posible: porque llega a las cotas de Singleton, Hamming y Griesmer o porque recorrió todas las matrices con más distancia sin
    encontrar ninguna. Así salen, por ejemplo, la
    repetición (5,1), Hamming (15,11), Hamming extendido (8,4) y Golay (23,12). Calcula la distancia mínima, la distribución de
    pesos y la tabla de síndromes (cada síndrome con su error de menor peso). El flujo comprimido pasa por el mismo BSC de dos
    formas:
    - _FEC_: el receptor corrige cada bloque con la tabla de síndromes.
    - _ARQ_: el receptor sólo detecta ($s \neq 0$) y pide repetir el bloque, hasta un máximo de envíos (canal de retorno ideal).

    Para cada caso mide los errores que quedan, la información mutua entre el flujo comprimido enviado y el recuperado, y la
    información mutua por símbolo entre el archivo **original** y el **recuperado**; además compara con los valores teóricos
    ($P$ de bloque mal decodificado, de error no detectado y envíos esperados) y con la capacidad del canal ($R < C$).

5.  **Sistema**: métricas de Shannon:
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

La pestaña **9 · 5G / 6G** muestra, sin tecnicismos, cuánto tardaría en llegar el archivo comprimido (el del agente si se ejecutó)
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

## Simulación 5G / 6G bit por bit

La pestaña **10 · Simulación 5G/6G** cifra el archivo comprimido y lo transmite por una capa física como la de 5G NR, bloque
por bloque y bit por bit, y compara tres caminos con el mismo canal: el **LDPC de 5G**, el **código de bloque** del control de
error y **sin código**. Un **agente** elige el código y las capas MIMO antes de enviar.

```
archivo comprimido → cifrado AES-128-CTR → bloques de transporte → CRC24A/CRC16 + segmentación + CRC24B → código de canal
→ adaptación de tasa (rv 0, 2, 3, 1) + entrelazado → QPSK…1024QAM → capas MIMO (1, 2, 4 u 8) → canal con ruido gaussiano
→ LLR → decodificación → CRC → HARQ (envíos máximos: el control ARQ, hasta 8) → descifrado → descompresión → archivo recuperado
```

Los tres caminos usan **un solo lazo de retransmisión** (`enviarTB` en `src/Simulacion.cpp`): cada código sólo dice cómo
codifica, qué bits salen en cada envío y cómo combina y decodifica (redundancia incremental en el LDPC, combinación de Chase
en el código de bloque y sin código).

La pestaña tiene sub-pestañas: **Resumen**, **Paso a paso** (el primer bloque con los números reales de cada etapa),
**Capas MIMO**, **Agente** (sólo pruebas: medición de las 12 opciones y lo que probó el agente), **Constelación** (con cómo
leerla), **Curvas BLER** y **Confiabilidad**. El botón **Visualizar** abre una figura animada donde cada bloque recorre el
transmisor, cruza el aire por sus capas y llega al receptor, con sus errores, iteraciones y ACK/NACK reales.

| Parte | Referencia | Archivo |
|---|---|---|
| CRC24A, CRC24B, CRC16, segmentación en bloques de código | TS 38.212 §5.1, §5.2.2, §7.2 | `src/NR.cpp` |
| LDPC con grafos base BG1 (46 × 68) y BG2 (42 × 52), tamaños de elevación Zc | TS 38.212 §5.3.2, Tablas 5.3.2-1 a -3 | `include/GrafosBaseNR.h`, `src/NR.cpp` |
| Adaptación de tasa: buffer circular, versiones de redundancia y entrelazado de bits | TS 38.212 §5.4.2 | `src/NR.cpp` |
| Modulación QPSK, 16QAM, 64QAM, 256QAM y 1024QAM | TS 38.211 §5.1 | `src/NR.cpp` |
| Tablas MCS 1 a 4 (incluida la de baja eficiencia para URLLC y la de 1024QAM) y TBS | TS 38.214 §5.1.3 | `src/NR.cpp`, `src/Enlace.cpp` |
| Decodificador LDPC min-sum normalizado (fila por fila), HARQ con redundancia incremental, ajuste automático de modulación y tasa | — | `src/NR.cpp`, `src/Simulacion.cpp` |
| Mapeo a capas: la capa l lleva los símbolos l, l + L, …; hasta 4 capas por palabra de código | TS 38.211 §7.3.1.3 | `src/Simulacion.cpp` |
| Cifrado AES-128 en modo contador (bloque contador de 128-NEA2) | FIPS-197, NIST SP 800-38A, TS 33.501 | `src/Cifrado.cpp` |

Escenarios, con canal AWGN: **5G eMBB** (banda n78, 100 MHz, envíos de 0.5 ms, hasta 256QAM), **5G
URLLC** (n78, envíos de 0.25 ms, modulación y tasa más robustas) y **6G candidato** (400 MHz, envíos de 62.5 µs, hasta
1024QAM). Con L capas MIMO la potencia se reparte por igual, así que cada capa llega con SNR/L; las capas llegan separadas, sin
interferencia entre ellas. La modulación y la tasa del código se eligen solas según la SNR por capa y se ajustan con cada
bloque que falla o llega bien.

**Agente de transmisión.** Con «Capas MIMO: que decida el agente», antes de enviar el archivo el agente prueba las 12 opciones
(LDPC, código de bloque y sin código, cada uno con 1, 2, 4 y 8 capas) bloque por bloque con la regla ε-greedy (recompensa: bits
entregados dentro del plazo de latencia por segundo) y elige la mejor. Después se miden todas las opciones con los mismos
8 bloques y el mismo canal para comprobar la decisión.

**Archivos grandes.** La simulación guarda 8 bits en cada byte (antes usaba un byte entero por cada bit), decodifica
en paralelo con todos los núcleos y corre en segundo plano con una barra de avance. Un archivo de 600 MB necesita unos 2,5 GB de
memoria. Los resultados se comparan con los requisitos de IMT-2020 (ITU-R M.2410) y de IMT-2030 (ITU-R M.2160); la
prueba de confiabilidad envía paquetes de 32 bytes dentro del plazo y da la cota de Clopper-Pearson al 95 %. También calcula
las curvas BLER frente a SNR de los cuatro códigos con su límite de Shannon.

Verificación:
- Los grafos base se extrajeron del PDF de TS 38.212 y coinciden entrada por entrada con una copia independiente (316 y 197
  entradas).
- El codificador LDPC, la adaptación de tasa, los CRC y la modulación hasta 256QAM coinciden bit a bit con la biblioteca
  `py3gpp`. En BG2 con K ≤ 640 py3gpp da palabras que no cumplen la H de la norma; las de este programa sí la cumplen.
- La fórmula de 1024QAM y las tablas MCS se leyeron de TS 38.211 V17.6.0 y TS 38.214 V17.8.0.
- El decodificador queda a 1–2.5 dB del límite de Shannon (por ejemplo, QPSK con tasa 1/2 y K = 8448 decodifica desde
  Es/N0 ≈ 1.1 dB, con el límite en 0 dB).
- El AES da el vector de prueba de FIPS-197 (apéndice C.1) y el de modo contador de NIST SP 800-38A (F.5.1); cifrar con hilos y
  sin hilos da lo mismo, y descifrar es volver a cifrar.

## Compilación y Ejecución

```bash
python -m venv venv
# Windows: .\venv\Scripts\activate      Linux/Mac: source venv/bin/activate
pip install -r requirements.txt
streamlit run app.py
```

En la barra lateral se elige el archivo (de `data/`, incluidas sus subcarpetas, o subido), N, el método, el ruido del canal y
el código de control de error;
los resultados se recalculan solos al cambiar cualquiera de ellos (archivos de más de 32 MB piden confirmar con **Procesar
archivo**). El agente (pestaña 8) se ejecuta con su botón; la pestaña 5G/6G se calcula sola.

La interfaz compila el núcleo automáticamente con `g++` si el ejecutable no existe o si cambió el código. Para compilarlo a mano:

```bash
g++ -O3 -std=c++17 -pthread src/*.cpp -o codificador_6g.exe
```

Uso desde la terminal:

```bash
./codificador_6g.exe data/archivo.mp3 1024 --metodo auto --p 0.001 --salida data/salida/prueba --json resultados.json
./codificador_6g.exe data/archivo.txt 8 --p 0.01 --fec 7,4 --arq-max 8 --json resultados.json   # código (n,k)
./codificador_6g.exe data/archivo.txt 8 --p 0.01 --fec P:1111 --json resultados.json   # P propia: (5,1)
./codificador_6g.exe data/archivo.mp3 --barrido 8,16,32,64,128,256,512,1024,2048 --json barrido.json
./codificador_6g.exe data/archivo.mp3 --agente --salida data/salida/agente --json agente.json
./codificador_6g.exe data/archivo.mp3 --enlace --comprimido data/salida/agente/agente.ti6g --snr 20 --capas 4 --servicio embb --json enlace.json
./codificador_6g.exe data/archivo.txt --simular --comprimido data/salida/prueba/comprimido.ti6g --escenario embb --snr 20 --capas auto --fec 7,4 --json sim.json
./codificador_6g.exe data/archivo.txt --simular --comprimido data/salida/prueba/comprimido.ti6g --escenario 6g --snr 30 --capas 4 --sin-cifrado --max-bytes 4194304 --json sim.json
./codificador_6g.exe --confiabilidad --escenario urllc --snr 10 --paquetes 300000 --json confiabilidad.json
./codificador_6g.exe --curvas --escenario embb --snr 3 --fec 7,4 --json curvas.json
```


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
