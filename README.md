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

## Compilación y Ejecución

```bash
python -m venv venv
# Windows: .\venv\Scripts\activate      Linux/Mac: source venv/bin/activate
pip install -r requirements.txt
streamlit run app.py
```

La interfaz compila el núcleo automáticamente con `g++` si el ejecutable no existe o si cambió el código. Para compilarlo a mano:

```bash
g++ -O2 -std=c++17 src/*.cpp -o codificador_6g.exe
```

Uso desde la terminal:

```bash
./codificador_6g.exe data/archivo.mp3 1024 --metodo auto --p 0.001 --salida data/salida/prueba --json resultados.json
./codificador_6g.exe data/archivo.mp3 --barrido 8,16,32,64,128,256,512,1024,2048 --json barrido.json
./codificador_6g.exe data/archivo.mp3 --agente --salida data/salida/agente --json agente.json
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
