# Guía para explicar el programa

Esta guía recorre el programa parte por parte: qué muestra cada pantalla, qué decir en la presentación y qué archivo usar para que
el resultado se vea claro. Los números que aparecen salieron del propio programa.

## 1. Qué hace el programa (en una frase)

> Toma cualquier archivo (texto, imagen, audio, video), lo trata como una **fuente de información**, lo **codifica y comprime**,
> lo pasa por un **canal con ruido**, lo **reconstruye** y mide en cada paso lo que vimos en clase. Un **agente de inteligencia
> artificial** elige la mejor forma de comprimirlo y al final se ve **cuánto tardaría en llegar por 5G y 6G**.

El recorrido es siempre el mismo:

```
Fuente  →  Codificación  →  Compresión  →  Canal  →  Reconstrucción
```

El núcleo está escrito en C++ (por velocidad) y la interfaz en Python con Streamlit.

## 2. Cómo se usa

1. Abrir la app (`streamlit run app.py`) y entrar a `http://localhost:8501`.
2. En la **barra lateral**:
   - **Archivo**: uno de la carpeta `data/` (incluida `data/sin_comprimir/`) o uno propio con *Subir archivo*.
   - **Tamaño de símbolo N**: cuántos bits forman cada símbolo (de 8 a 32 768).
   - **Método**: Uniforme, Huffman, LZW o *Adaptable* (prueba los tres y se queda con el más pequeño).
   - **Probabilidad de error p**: ruido del canal (0 = canal perfecto).
3. Los resultados se recalculan solos al cambiar cualquier opción. Los archivos de más de 32 MB piden confirmar con
   *Procesar archivo*.

## 3. El resumen de arriba

Siempre visible, encima de las pestañas:

| Dato | Qué es | Cómo decirlo |
|---|---|---|
| Entropía H | Información promedio por símbolo | "Es el mínimo de bits por símbolo que puede usar un código sin perder nada" |
| Divergencia KL | N − H | "Cuánto sobra: la redundancia que se puede quitar" |
| Varianza σ² | Qué tan distintas son las longitudes del código | "A igual eficiencia, mejor la de menor varianza" |
| I(X;Y) por símbolo | Información que sobrevive al canal | "Con canal perfecto es igual a H: llega todo" |
| Comprimido | Tamaño final y porcentaje de cambio | "Cuánto se redujo el archivo" |

## 4. Pestaña por pestaña

### 1 · Fuente

**Qué muestra.** El archivo dividido en símbolos de N bits, cuántos símbolos distintos hay (M), la probabilidad de cada uno y las
medidas de Shannon.

**Qué decir.**
- Cantidad de información de un símbolo: **I(x) = −log₂ P(x)**. Un símbolo raro aporta más información que uno frecuente.
- **Entropía H(X) = −Σ P(x) log₂ P(x)**: la información promedio. Es el límite de Shannon para comprimir sin pérdida.
- **Divergencia KL = N − H**: distancia a una fuente donde todos los símbolos son igual de probables. Es la redundancia.

**Ejemplo.** `ejemplo_varianza_huffman.txt` con N = 8: cada símbolo ocupa 8 bits, pero H = 2.12 bits. Sobran casi 6 bits por
símbolo.

### 2 · Codificación

**Qué muestra.** La comparación entre código uniforme (todos los códigos del mismo largo) y no uniforme (Huffman), con longitud
media L̄, varianza σ², eficiencia H/L̄ y la desigualdad de Kraft.

**Qué decir.**
- **Uniforme**: ⌈log₂ M⌉ bits para cada símbolo. Varianza 0, pero desperdicia bits.
- **Huffman**: códigos cortos para los símbolos frecuentes. Su L̄ queda muy cerca de H.
- **Varianza σ² = Σ P_k (L_k − L̄)²**: dos códigos pueden tener la misma longitud media y distinta varianza.

**Ejemplo.** `ejemplo_varianza_huffman.txt` reproduce el ejemplo de clase, P = {0.4, 0.2, 0.2, 0.1, 0.1}:

| Código | L̄ | σ² |
|---|---|---|
| Huffman de mínima varianza | 2.2 bits | **0.16** |
| Huffman con los empates abajo | 2.2 bits | 1.36 |

"Los dos son igual de eficientes, pero el de menor varianza es mejor: el flujo de bits es más parejo y un error afecta a menos
símbolos."

### 3 · Compresión

**Qué muestra.** El tamaño final con cada método, separado en **cabecera** (alfabeto y tabla del código) y **datos**, y el paso a
paso del diccionario de LZW.

**Qué decir.**
- El archivo comprimido es **autocontenido**: lleva todo lo necesario para reconstruirse.
- **LZW** guarda en un diccionario las secuencias que se repiten. Por eso gana cuando hay repeticiones, algo que Huffman no
  aprovecha porque trata cada símbolo como independiente.

**Ejemplo.** `data_prueba_secuencial.txt` con N = 16: LZW deja el archivo en un tercio.

### 4 · Reconstrucción

**Qué muestra.** El archivo reconstruido **leyendo solo el archivo comprimido**, comparado byte a byte con el original.

**Qué decir.** "La compresión es sin pérdida: lo que sale es idéntico a lo que entró."

### 5 · Canal e I(X;Y)

**Qué muestra.** Un **canal binario simétrico**: cada bit se invierte con probabilidad p. Muestra la matriz de transición P(y|x)
teórica y la medida, la información mutua y la capacidad.

**Qué decir.**
- **I(X;Y) = H(Y) − H(Y|X)**: cuánta información de lo enviado llega al receptor.
- **Capacidad C = 1 − H_b(p)**: lo máximo que el canal puede transmitir por cada bit.
- Con p = 0, I(X;Y) = H(X): llega todo. Al subir p, la información mutua baja.

**Ejemplo.** Poner p = 0.01: la capacidad baja a C = 0.919 bits por uso, y la I(X;Y) medida queda prácticamente en ese valor.

### 6 · Comparar N

**Qué muestra.** El mismo archivo con todos los valores de N: entropía por bit, símbolos distintos, varianza y compresión.

**Qué decir.** "Con N grande cada símbolo agrupa más bits y la entropía por bit baja. Pero aparecen muchos más símbolos distintos y
hay que guardarlos en la cabecera. Por eso existe un N óptimo para cada archivo, y no siempre es 8."

**Ejemplo.** `7341-minimalista.jpg`: el mejor es N = 256 con LZW.

### 7 · Agente IA

**Qué muestra.** Un agente que **elige la mejor combinación de método y N** (27 opciones: 3 métodos × 9 valores de N) para el
archivo, cómo aprendió y la comprobación contra las 27 combinaciones.

**Qué decir.**
- Es un agente de **aprendizaje por refuerzo** de tipo **DQN**, como propone el documento de referencia (que menciona
  PPO, SAC o DQN con un mecanismo adaptativo).
- Recorre el archivo por bloques:
  1. **Estado**: estadísticas del bloque (entropía, cuánto texto tiene, repeticiones).
  2. **Acción**: elige las combinaciones que su red neuronal estima mejores.
  3. **Recompensa**: los bits que ahorró. Con eso aprende.
  4. **Detección de cambios** (Page-Hinkley): si el contenido del archivo cambia, olvida lo aprendido y vuelve a explorar.
- **Por qué DQN**: las 27 opciones son discretas (para eso se diseñó DQN; SAC es para acciones continuas) y DQN reutiliza sus
  experiencias, así que aprende con pocos bloques.
- **Validación**: en 323 archivos reales eligió en el **100 %** de los casos lo mismo que probar las 27 combinaciones, y todas las
  reconstrucciones fueron idénticas.

**Qué señalar.** La decisión en verde, la gráfica de qué combinación usó en cada bloque y el mapa de las 27 combinaciones con la
elegida resaltada.

### 8 · 5G / 6G

**Qué muestra.** Cuánto tardaría en llegar el archivo **sin comprimir** y **comprimido** según los estándares:

| | 5G (IMT-2020) | 6G (IMT-2030) |
|---|---|---|
| Velocidad garantizada a cada usuario | 100 Mbit/s | 300 a 500 Mbit/s |
| Velocidad máxima | 20 Gbit/s | 50 a 200 Gbit/s |
| Demora (latencia) | 4 ms | 0.1 a 1 ms |

Además muestra una **red 5G real** en la banda de 3.5 GHz (la más usada), calculada con las normas técnicas de 3GPP.

**Qué decir.**
- "Comprimir es quitar redundancia: menos bits que enviar, menos tiempo."
- "6G todavía está en definición; sus valores son los objetivos de la UIT."
- "Con archivos pequeños casi no cambia nada porque domina la demora fija de la red; con archivos grandes la compresión ahorra
  mucho tiempo."

### Consola

Muestra la salida del núcleo C++ y el tiempo de cada etapa. Sirve para demostrar que el cálculo lo hace el programa.

## 5. Archivos de prueba

La carpeta `data/sin_comprimir/` tiene archivos **sin comprimir**, pensados para ver el efecto de la compresión. Todos son
sintéticos (generados para el proyecto), salvo *Don Quijote*, que es de dominio público (Project Gutenberg).

Resultados del agente y del envío por 5G y 6G (tiempo sin comprimir → comprimido, incluida la demora de la red):

| Archivo | Qué es | Tamaño | Agente elige | Comprimido | Ahorro | 5G garantizada (100 Mbit/s) | Red 5G real (3.5 GHz) | 6G garantizada |
|---|---|---|---|---|---|---|---|---|
| `don_quijote.txt` | Libro completo | 2 174 KB | LZW, N = 8 | 773 KB | 64.4 % | 182 → 67 ms | 18.0 → 9.5 ms | 48.0 → 17.4 ms |
| `sensores_trafico.csv` | Datos de sensores de una intersección | 1 472 KB | LZW, N = 16 | 204 KB | 86.2 % | 125 → 21 ms | 13.4 → 5.0 ms | 32.7 → 5.0 ms |
| `registro_servidor.log` | Registro de un servidor web | 1 465 KB | LZW, N = 16 | 157 KB | 89.3 % | 124 → 17 ms | 13.4 → 5.0 ms | 32.6 → 4.0 ms |
| `alertas_trafico.json` | Alertas de incidentes | 1 048 KB | LZW, N = 16 | 67 KB | 93.6 % | 90 → 9.5 ms | 10.5 → 4.5 ms | 23.4 → 2.0 ms |
| `mapa_interseccion.bmp` | Imagen con colores planos | 2 197 KB | LZW, N = 64 | 18 KB | **99.2 %** | 184 → 5.4 ms | 18.4 → 4.5 ms | 48.6 → 0.9 ms |
| `degradado_colores.bmp` | Imagen con degradados suaves | 2 304 KB | LZW, N = 16 | 1 733 KB | 24.8 % | 193 → 146 ms | 19.5 → 15.5 ms | 50.9 → 38.4 ms |
| `foto_con_ruido.bmp` | Imagen con ruido de cámara | 1 406 KB | Huffman, N = 8 | 1 358 KB | **3.4 %** | 119 → 115 ms | 13.0 → 13.0 ms | 31.3 → 30.2 ms |
| `sirena_ambulancia.wav` | Audio de una sirena | 689 KB | LZW, N = 128 | 219 KB | 68.2 % | 60 → 22 ms | 8.4 → 5.5 ms | 15.6 → 5.3 ms |
| `tono_puro_440hz.wav` | Audio de una nota pura | 431 KB | LZW, N = 64 | 41 KB | 90.4 % | 39 → 7.4 ms | 7.0 → 4.5 ms | 10.0 → 1.4 ms |
| `telemetria_magnetometro.bin` | Muestras de un sensor | 977 KB | LZW, N = 16 | 477 KB | 51.2 % | 84 → 43 ms | 10.5 → 7.0 ms | 21.9 → 11.0 ms |
| `Captura de pantalla 2025-03-16 121948.bmp` | Captura de pantalla | 5 590 KB | LZW, N = 8 | 2 778 KB | 50.3 % | 462 → 232 ms | 40.9 → 22.5 ms | 123 → 61 ms |

Para 6G se usa el valor medio de su rango.

**Lo que muestran juntos:**
- **Texto y datos** (libro, CSV, registro, JSON) se comprimen mucho: tienen palabras y estructuras que se repiten.
- **Colores planos** (`mapa_interseccion.bmp`) se reducen casi por completo; **ruido** (`foto_con_ruido.bmp`) casi no se reduce:
  el ruido es información que no se repite, su entropía es casi la máxima.
- **El mismo contenido comprimido y sin comprimir**: la captura de pantalla en PNG (en `data/`) no baja nada porque el formato PNG
  ya está comprimido; la misma captura en BMP baja a la mitad. Lo mismo pasa con MP3, JPG y ZIP.

## 6. Orden sugerido para la demostración (unos 12 minutos)

1. **Introducción** (1 min): la frase de la sección 1 y el recorrido Fuente → … → Reconstrucción.
2. **Fuente** con `ejemplo_varianza_huffman.txt` (2 min): H = 2.12 frente a 8 bits.
3. **Codificación** con el mismo archivo (2 min): varianza 0.16 frente a 1.36.
4. **Compresión y reconstrucción** con `data_prueba_secuencial.txt`, N = 16 (1.5 min): LZW y reconstrucción idéntica.
5. **Canal** con p = 0.01 (1.5 min): matriz de transición, I(X;Y) y capacidad. Volver p a 0.
6. **Contraste** entre `mapa_interseccion.bmp` (99 %) y `foto_con_ruido.bmp` (3 %) (1 min).
7. **Agente IA** con `sensores_trafico.csv` (2 min): decisión, gráfica por bloques, mapa de las 27 combinaciones.
8. **5G / 6G** con la captura en BMP (1 min): la red 5G real pasa de 41 ms a 22.5 ms.
9. **Cierre** (30 s): entropía = límite de la compresión, varianza = criterio entre códigos igual de eficientes, información mutua =
   lo que sobrevive al canal, agente = decide solo, 5G/6G = menos bits, menos tiempo.

## 7. Preguntas frecuentes

| Pregunta | Respuesta corta |
|---|---|
| ¿Por qué un MP3 o un PNG no se comprimen? | Ya vienen comprimidos: su entropía es casi 8 bits por byte y no queda redundancia. |
| ¿Por qué no siempre N = 8? | Con N mayor se aprovechan bytes que van juntos, pero la cabecera crece; cada archivo tiene su punto óptimo. |
| ¿Por qué la regla de la menor varianza? | A igual eficiencia, longitudes más parejas dan un flujo de bits más estable y un error afecta a menos símbolos. |
| ¿El agente puede equivocarse? | Se comprobó en 323 archivos: siempre eligió lo mismo que probar las 27 combinaciones. Además reconstruye el archivo para verificarlo. |
| ¿Por qué DQN y no PPO o SAC? | Las opciones son discretas (27), que es el caso de DQN; SAC es para acciones continuas. DQN reutiliza sus experiencias y aprende con pocos datos. |
| ¿Los tiempos de 5G son reales? | Los de "velocidad garantizada" y "máxima" usan los valores de la UIT. La "red 5G real" se calcula con las normas de 3GPP para una red de 100 MHz con buena señal. |
| ¿6G ya existe? | No como estándar terminado: se usan los objetivos que fijó la UIT (IMT-2030). |
| ¿Por qué C++? | Por velocidad: un archivo de 200 MB se procesa en unos 20 segundos. |

## 8. Supuestos y límites

- El canal de la pestaña 5 es un **canal binario simétrico simulado** (errores independientes).
- La **red 5G real** supone buena señal (20 dB), 4 antenas y la banda de 3.5 GHz con 100 MHz; no incluye las cabeceras de los
  protocolos de red.
- Los valores de **6G** son objetivos de investigación de la UIT, no una norma técnica terminada.
- Los tiempos incluyen la demora fija de la red (4 ms en 5G; 0.1 a 1 ms en 6G).
