# Guía para explicar el programa

Esta guía recorre el programa parte por parte: qué muestra cada pantalla, qué decir en la presentación y qué archivo usar para que
el resultado se vea claro. Los números que aparecen salieron del propio programa.

## 1. Qué hace el programa (en una frase)

> Toma cualquier archivo (texto, imagen, audio, video), lo trata como una **fuente de información**, lo **codifica y comprime**,
> lo **protege con un código de control de error**, lo pasa por un **canal con ruido**, lo **reconstruye** y mide en cada paso lo
> que define la teoría de la información. Un **agente de inteligencia
> artificial** elige la mejor forma de comprimirlo y al final el archivo se **transmite bit por bit por una capa física de 5G y
> 6G**, con el código LDPC de la norma, modulación QAM, ruido y retransmisiones.

El recorrido es siempre el mismo:

```
Fuente  →  Codificación  →  Compresión  →  Control de error  →  Canal  →  Reconstrucción
```

El núcleo está escrito en C++ (por velocidad) y la interfaz en Python con Streamlit.

## 2. Cómo se usa

1. Abrir la app (`streamlit run app.py`) y entrar a `http://localhost:8501`.
2. En la **barra lateral**:
   - **Archivo**: uno de los cuatro archivos grandes de `data/` (texto, audio, imagen y video) o uno propio con *Subir archivo*.
   - **Tamaño de símbolo N**: cuántos bits forman cada símbolo (de 8 a 32 768).
   - **Método**: Uniforme, Huffman, LZW o *Adaptable* (prueba los tres y se queda con el más pequeño).
   - **Probabilidad de error p**: ruido del canal (0 = canal perfecto).
   - **Control de error**: **n y k** del código de bloque (el programa elige la matriz P) o una matriz P propia, y cuántas veces
     puede ARQ repetir un bloque.
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

**Ejemplo.** `wikipedia_enwik9_550MB.txt` con N = 8: cada símbolo ocupa 8 bits, pero H = 5.14 bits (sólo aparecen 206 de los
256 bytes posibles). Sobran casi 3 bits por símbolo.

### 2 · Codificación

**Qué muestra.** La comparación entre código uniforme (todos los códigos del mismo largo) y no uniforme (Huffman), con longitud
media L̄, varianza σ², eficiencia H/L̄ y la desigualdad de Kraft.

**Qué decir.**
- **Uniforme**: ⌈log₂ M⌉ bits para cada símbolo. Varianza 0, pero desperdicia bits.
- **Huffman**: códigos cortos para los símbolos frecuentes. Su L̄ queda muy cerca de H.
- **Varianza σ² = Σ P_k (L_k − L̄)²**: dos códigos pueden tener la misma longitud media y distinta varianza.

**Ejemplo.** El ejemplo clásico de cinco símbolos con P = {0.4, 0.2, 0.2, 0.1, 0.1} (un archivo de texto con esas frecuencias,
subido con *Subir archivo*, lo reproduce):

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

**Ejemplo.** `wikipedia_enwik9_550MB.txt` con N = 8: Huffman bajaría a 5.14 bits por símbolo (la entropía), pero LZW gana
porque guarda palabras enteras en su diccionario: el archivo pasa de 577 MB a 229 MB (60.4 % menos).

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

Al final está una **calculadora de canal discreto**: se escribe P(X) y la matriz de transición P(y|x) de cualquier tamaño, y
calcula P(x, y), P(y), P(x|y), las entropías y la información mutua de tres formas: H(X) − H(X|Y), H(Y) − H(Y|X) y
H(X) + H(Y) − H(X,Y).

**Ejemplo.** Con los valores iniciales (P(X) = {0.6, 0.4} y una matriz de 2 × 3) las tres formas dan **0.3183070 bits**: la
información mutua es simétrica, I(X;Y) = I(Y;X), y nunca es negativa.

### 6 · Control de error

**Qué muestra.** El flujo comprimido protegido con un **código de bloque lineal (n, k)**: cada mensaje de k bits lleva n − k bits
de paridad. Se ven las matrices G = [P | I_k] y H = [I_(n−k) | Pᵀ], la tabla de todas las palabras del código (estructura de la
codificación), la tabla de síndromes (estructura del síndrome) y los primeros bloques que llegaron con error, con m, c = mG, el
error e, lo recibido r, el síndrome s = rHᵀ y la corrección.

Después compara tres formas de mandar el mismo archivo por el mismo canal:
- **Sin control de error**.
- **FEC**: el receptor corrige con la tabla de síndromes.
- **ARQ**: el receptor sólo detecta (s ≠ 0) y pide que se repita el bloque.

Para cada una da los bits enviados, los errores que quedan y la **información mutua entre el archivo original y el recuperado**.

**Qué decir.**
- "Control de error es agregar redundancia controlada: la compresión la quita y el código la vuelve a poner, pero ordenada."
- "Si s = 0 el bloque se acepta; si no, hay error. FEC lo corrige con el error más probable; ARQ pide que lo repitan."
- "El Hamming (7,4) tiene d_min = 3: corrige 1 error y detecta 2 por bloque. Es perfecto: sus 8 síndromes son exactamente
  'sin error' y los 7 errores de un bit."
- "Un archivo comprimido no tolera errores. Con Huffman un bit errado daña pocos símbolos, pero el decodificador puede leer
  de más o de menos y todo lo que sigue queda corrido de su posición. Con LZW, además, la entrada equivocada del diccionario
  repite el error cada vez que se usa."
- "Primero se comprime y después se protege: es el orden que respalda el teorema de separación fuente-canal de Shannon, y es
  lo que hace 5G."
- "n y k solos no definen el código: lo define P. Con n y k el programa busca la P de mayor distancia mínima. Con (23,12)
  encuentra el código de Golay, que corrige 3 errores por bloque."

**Ejemplo.** `chopin_baladas_fantasia_allegro.wav` (744 MB), N = 8, Huffman, p = 0.001, Hamming (7,4):

| | Bits enviados | Bits errados al final | Símbolos errados | I(X;Y) original ↔ recuperado |
|---|---|---|---|---|
| Sin control | 5 414 554 894 | 5 415 527 | 98.0 % | 0.019 de 7.255 bits |
| FEC | 9 475 471 068 (+75 %) | 48 749 | 97.7 % | 0.031 bits |
| ARQ | 9 542 083 712 (+76 %) | 13 | 37.4 % | 3.709 bits |

FEC quita el 99.1 % de los errores, pero con los 48 749 que quedan casi ningún símbolo queda en su posición: en Huffman un bit
errado cambia la longitud de las palabras que siguen y corre el resto. ARQ deja sólo 13 bits errados de 5 400 millones; vienen
de 9 bloques que recibieron 3 errores o más y se convirtieron en otra palabra del código, así que su síndrome dio cero (el
Hamming (7,4) tiene d_min = 3 y sólo detecta con seguridad hasta 2 errores por bloque). Con esos pocos errores el 37 % de los
símbolos queda fuera de su lugar, pero la información mutua sube a la mitad de H(X). Con **n = 5, k = 1** (repetición) o con
un código de mayor distancia mínima los errores no detectados bajan aún más, a cambio de enviar más bits.

La tabla **Teoría y medición** compara lo simulado con las fórmulas del código (por ejemplo, ARQ: 1.0070 envíos por bloque
esperados y 1.0070 medidos con el archivo de Chopin).

### 7 · Comparar N

**Qué muestra.** El mismo archivo con todos los valores de N: entropía por bit, símbolos distintos, varianza y compresión.

**Qué decir.** "Con N grande cada símbolo agrupa más bits y la entropía por bit baja. Pero aparecen muchos más símbolos distintos y
hay que guardarlos en la cabecera. Por eso existe un N óptimo para cada archivo, y no siempre es 8."

**Nota.** Con los archivos de 500 MB de `data/` el barrido procesa el archivo una vez por cada N, así que tarda varios
minutos; con un archivo propio de pocos MB (*Subir archivo*) tarda segundos.

### 8 · Agente IA

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

### 9 · 5G / 6G

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

### 10 · Simulación 5G/6G

**Qué muestra.** El archivo comprimido, cifrado y transmitido bloque por bloque y bit por bit por una capa física como la de
5G NR:

```
cifrado AES-128 → bloques de transporte → CRC → código de canal → adaptación de tasa → QPSK…1024QAM → capas MIMO
→ canal con ruido → confianza de cada bit (LLR) → decodificación → CRC → HARQ → descifrado → descompresión → archivo
```

Con el mismo canal compara tres caminos: el **LDPC de 5G** (TS 38.212), el **código de bloque** de la barra lateral y **sin
código**. Se eligen el escenario (5G eMBB, 5G URLLC o 6G candidato), la relación señal a ruido, las **capas MIMO** (1, 2, 4, 8 o
«que decida el agente»), si se cifra y si se envía el archivo completo o sus primeros 4 MB; los envíos máximos por bloque son
los del control **ARQ** de la barra lateral (hasta 8). **Simular** corre en segundo plano con una barra de avance y el tiempo que
falta. Los resultados quedan en sub-pestañas:

- **Resumen**: velocidad, latencia, BLER, si el archivo llegó idéntico, la tabla de los tres códigos y los requisitos de la UIT.
- **Paso a paso**: lo que le pasa al primer bloque en cada etapa, con sus números reales (bits, CRC, partes del LDPC, símbolos
  por capa, errores, iteraciones, ACK/NACK, latencia, descifrado y descompresión). Se puede ver para cada código.
- **Capas MIMO**: qué son, la capacidad L·log₂(1 + SNR/L) para 1, 2, 4 y 8 capas y los errores de cada capa.
- **Agente**: sólo pruebas. La medición de las 12 opciones (código × capas) con los mismos bloques, la opción del agente
  marcada, y cada prueba que hizo el agente (al azar o la mejor conocida).
- **Constelación**: los símbolos recibidos sobre los puntos ideales y cómo leer la imagen.
- **Curvas BLER** y **Confiabilidad** (300 000 mensajes de 32 bytes).

El botón **Visualizar** abre una figura animada: un banco de pruebas con 11 estaciones numeradas donde cada bloque sale del
archivo, se cifra, recibe su CRC, se codifica, se modula, se reparte entre las antenas de sus capas, cruza el aire (las cuentas
rojas son símbolos que el ruido acercó a otro punto) y en el receptor se combina, se decodifica (la aguja marca las
iteraciones), se revisa el CRC (luz ACK o NACK; el aviso vuelve por el cable de atrás) y, si falla, se envía otra vez. Al
fondo, un **monitor** muestra el contenido del paquete actual: sus bytes antes y después de cifrar, su CRC y sus primeros 64 bits
en tres cuadrículas (el mensaje; cómo llegaron por el aire, en rojo los que cambiaron; y cómo quedaron después de corregir, en
rojo los que siguen mal). Al lado, un **tablero** tiene un pozo por paquete: cada paquete que termina deja una ficha por envío,
verde si llegó al primer envío, ámbar si se recuperó repitiéndolo y roja si falló; con un clic en un pozo se ve ese paquete.

**Qué decir.**
- "Esto ya no es una fórmula: cada bit del archivo pasa por la cadena de la norma y se cuenta lo que llega."
- "Primero se comprime, después se cifra y al final se protege. El cifrado es AES-128 en modo contador, el de 5G: genera un
  flujo de clave y lo suma con XOR, así que un bit errado en el canal daña sólo ese bit al descifrar."
- "Con varias antenas se mandan varias capas a la vez. La potencia se reparte: con L capas cada una llega con SNR/L. Más capas
  dan más símbolos por segundo, pero cada uno con más ruido; el agente prueba cuántas conviene."
- "El LDPC de 5G es un código de bloque lineal como los de la pestaña 6, pero con miles de bits y una matriz H dispersa. Se
  decodifica con la confianza de cada bit y queda a 1–2 dB del límite de Shannon."
- "HARQ junta lo que llegó en cada envío; por eso un bloque que falla la primera vez casi siempre pasa la segunda."
- "La modulación y la tasa se ajustan solas: con cada bloque que falla se usa una más robusta y con cada acierto se sube un
  poco, hasta fallar alrededor del 10 % de las veces."
- "Después de decidir cada bit, este canal se comporta como el canal binario simétrico de la pestaña 5: la columna 'p del canal'
  es su p."

**Ejemplo.** `nasa_blue_marble_21600x10800.bmp`, comprimido con Huffman (N = 8) a 608 MB, cifrado, Hamming (7,4) en la barra
lateral. Con los primeros 4 MB y «que decida el agente»:

| Escenario y SNR | El agente elige | SNR por capa | LDPC de 5G | Hamming (7,4) y sin código |
|---|---|---|---|---|
| 5G eMBB, 20 dB | LDPC · 8 capas | 11.0 dB (64QAM) | 1.19 Gbit/s, latencia media 0.73 ms | no llegan |
| 5G eMBB, 0 dB | LDPC · 2 capas | −3.0 dB (QPSK) | 42.5 Mbit/s, latencia media 0.70 ms | no llegan |
| 5G URLLC, 10 dB | LDPC · 8 capas | 1.0 dB (QPSK) | 269 Mbit/s, 0.25 ms sin repeticiones | no llegan |
| 6G candidato, 30 dB | LDPC · 8 capas | 21.0 dB (256QAM) | 8.97 Gbit/s, latencia media 0.09 ms | no llegan |

En los cuatro casos la opción del agente es la mejor de la medición de las 12 opciones. A 0 dB el agente no usa 8 capas: cada
una quedaría en −9 dB, por debajo de la modulación más robusta, y ningún bloque llegaría; con 2 capas entrega 44 Mbit/s frente a
33 con 1 capa y 27 con 4.

**Archivo completo** (608 MB, eMBB a 20 dB, 8 capas): 10 126 bloques, 11 140 envíos, ninguno con error al final; 1.30 Gbit/s,
latencia media 0.70 ms y máxima 2.5 ms; el BMP descifrado y descomprimido es **idéntico** al original (comparado byte a byte).
Tardó 26 minutos y usó 2.5 GB de memoria.

En las curvas con QPSK y la misma tasa 4/7, el LDPC llega a BLER = 10 % cerca de 2 dB y el Hamming (7,4) cerca de 9 dB: unos
**7 dB de ganancia de codificación**. En URLLC a 10 dB, 300 000 paquetes de 32 bytes llegaron sin ninguna falla dentro de 1 ms:
eso asegura (al 95 %) la confiabilidad de 99.999 % que pide IMT-2020.

### Consola

Muestra la salida del núcleo C++ y el tiempo de cada etapa. Sirve para demostrar que el cálculo lo hace el programa.

## 5. Archivos de prueba

La carpeta `data/` tiene cuatro archivos reales de más de 500 MB, uno de cada tipo de fuente. Resultados con N = 8 y el método
adaptable (el programa prueba Uniforme, Huffman y LZW y guarda el más pequeño):

| Archivo | Qué es | Tamaño | Elige | Comprimido | Ahorro | Procesar (p = 0) |
|---|---|---|---|---|---|---|
| `wikipedia_enwik9_550MB.txt` | Texto de Wikipedia (volcado enwik9) | 577 MB | LZW | 229 MB | **60.4 %** | 73 s |
| `chopin_baladas_fantasia_allegro.wav` | Audio sin comprimir (PCM) | 744 MB | Huffman | 677 MB | 9.1 % | 113 s |
| `nasa_blue_marble_21600x10800.bmp` | Imagen de la Tierra de la NASA, sin comprimir | 700 MB | Huffman | 608 MB | 13.1 % | 77 s |
| `tears_of_steel_1080p.webm` | Video ya comprimido (VP9) | 571 MB | Uniforme | 571 MB | **0 %** | 88 s |

**Lo que muestran juntos:**
- **El texto** se comprime mucho: tiene palabras y estructuras que se repiten, y LZW las guarda en su diccionario.
- **El audio y la imagen sin comprimir** bajan poco con N = 8: así sólo se aprovecha que unos bytes son más comunes que otros, y
  su entropía ya es alta (7.25 bits por byte en el audio y 6.92 en la imagen, de un máximo de 8). Huffman llega casi a ese
  límite: 9.1 % y 13.1 % de ahorro. Los parecidos entre bytes vecinos (una muestra de audio se parece a la anterior) no se
  aprovechan con símbolos sueltos.
- **El video ya comprimido** no baja nada: su entropía es 7.9999 bits por byte, prácticamente la máxima, porque el formato VP9
  ya quitó la redundancia. El programa guarda el código uniforme y el archivo queda igual (más 299 bytes de cabecera).
- Con archivos de este tamaño cada etapa tarda: procesar uno toma entre 1 y 2 minutos, y la simulación 5G/6G del archivo completo
  unos 26 minutos (por eso, sin marcar «Archivo completo», se envían los primeros 4 MB).

## 6. Orden sugerido para la demostración (unos 20 minutos)

Antes de empezar, procesar los archivos que se van a mostrar (cada uno tarda entre 1 y 2 minutos; los resultados quedan
guardados mientras la página siga abierta). La simulación del archivo completo de nasa_blue ya queda guardada y se muestra al
abrir la pestaña 10 sin repetirla.

1. **Introducción** (1 min): la frase de la sección 1 y el recorrido Fuente → … → Reconstrucción.
2. **Fuente** con `wikipedia_enwik9_550MB.txt` (2 min): H = 5.14 frente a 8 bits.
3. **Codificación** (2 min): Uniforme frente a Huffman, longitud media y varianza.
4. **Compresión y reconstrucción** con el mismo archivo (1.5 min): LZW lo deja en 229 MB y la reconstrucción es idéntica.
5. **Canal** con p = 0.01 (1.5 min): matriz de transición, I(X;Y) y capacidad. Mostrar al final la calculadora del canal discreto.
6. **Control de error** con `chopin_baladas_fantasia_allegro.wav` y p = 0.001 (2 min): matrices G y H, tabla de síndromes, un
   bloque corregido y la comparación sin control / FEC / ARQ. Volver p a 0.
7. **Contraste** entre el texto (60 % de ahorro) y el video ya comprimido (0 %) (1 min).
8. **Agente IA** (2 min): decisión, gráfica por bloques, mapa de las combinaciones.
9. **5G / 6G** (1 min): cuánto tarda el archivo comprimido frente al original.
10. **Simulación 5G/6G** con `nasa_blue_marble_21600x10800.bmp` (4 min): el resultado guardado del archivo completo (idéntico,
    1.30 Gbit/s), el **Paso a paso**, las **Capas MIMO**, el **Agente** (sus pruebas), la **Constelación** y el botón
    **Visualizar**. Después, una simulación de 4 MB a 0 dB para ver que el agente cambia a 2 capas.
11. **Cierre** (30 s): entropía = límite de la compresión, varianza = criterio entre códigos igual de eficientes, información
    mutua = lo que sobrevive al canal, control de error = redundancia que se agrega a propósito, cifrado = privacidad sin
    agregar bits, capas MIMO = más símbolos a la vez repartiendo la potencia, agente = decide solo y con pruebas, y un buen
    código de canal llega cerca del límite de Shannon.

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
| ¿Comprimir y luego agregar bits no es contradictorio? | No: la compresión quita la redundancia que no sirve y el código agrega poca redundancia, ordenada para encontrar errores. |
| ¿FEC o ARQ? | FEC no necesita canal de retorno ni espera, pero gasta bits siempre. ARQ sólo repite lo que llegó mal, pero necesita el aviso del receptor y agrega demora. 5G combina las dos (HARQ). |
| ¿Por qué FEC no salvó el archivo comprimido? | El (7,4) corrige un error por bloque; los bloques con dos o más se decodifican mal. Con Huffman o LZW basta un bit errado para que lo que sigue quede corrido de posición, y con LZW el error se repite. |
| ¿Por qué no basta con dar n y k? | Hay muchas matrices P para el mismo (n, k) y no todas corrigen lo mismo. El programa busca la de mayor distancia mínima y dice si es la mayor posible, por las cotas de Singleton, Hamming y Griesmer o porque revisó todas las matrices con más distancia. |
| ¿Qué códigos usa 5G? | LDPC para los datos y Polar para la señalización (3GPP TS 38.212), con bloques de miles de bits. Para mensajes de control de 3 a 11 bits usa un código de bloque (32, K) que se calcula con c = m·G. |
| ¿Y 6G? | En junio de 2026 3GPP acordó soportar un nuevo grafo base de LDPC (BG3); el resto de la codificación sigue en estudio. |
| ¿La simulación usa el LDPC de verdad? | Sí: los grafos base salen de la norma (TS 38.212) y el codificador, la adaptación de tasa, los CRC y la modulación dan los mismos bits que una implementación independiente (py3gpp). |
| ¿Por qué el Hamming (7,4) no llega si corrige errores? | Corrige 1 error de cada 7 bits con decisiones duras. Con 256QAM a 20 dB el canal deja 6.6 % de bits errados, muchos más de los que puede corregir. El LDPC usa información blanda y bloques de miles de bits. |
| ¿Qué es la información mutua del demodulador? | Cuánta información sobre cada bit deja pasar el canal junto con la modulación. Si la tasa del código por símbolo es menor, el decodificador puede corregir; si es mayor, hacen falta retransmisiones. |
| ¿Por qué 100 000 paquetes no bastan para 99.999 %? | Con 0 fallas en N paquetes sólo se puede asegurar (al 95 %) una probabilidad de falla menor que 3/N; para 10⁻⁵ hacen falta 300 000. |

## 8. Supuestos y límites

- El canal de la pestaña 5 es un **canal binario simétrico simulado** (errores independientes). La pestaña 6 usa el mismo canal,
  con la misma p y la misma semilla.
- En **ARQ** se supone repetición selectiva ideal: los avisos del receptor (ACK/NAK) llegan sin errores y no se cuenta la espera.
  Si un bloque agota los envíos, se entrega como llegó.
- La **información mutua por símbolo** y los **símbolos errados** se calculan posición por posición: miden si el archivo se
  reconstruye tal cual, no si el contenido sigue siendo legible.
- La **red 5G real** supone buena señal (20 dB), 4 antenas y la banda de 3.5 GHz con 100 MHz; no incluye las cabeceras de los
  protocolos de red.
- Los valores de **6G** son objetivos de investigación de la UIT, no una norma técnica terminada.
- Los tiempos incluyen la demora fija de la red (4 ms en 5G; 0.1 a 1 ms en 6G).
- En la **simulación 5G/6G** el canal sólo suma ruido gaussiano (AWGN); las capas MIMO llegan separadas, sin interferencia entre
  ellas, y se reparten la potencia por igual (cada una con SNR/L); el receptor no tiene que estimar el
  canal, no hay cabeceras de protocolo y un bloque se repite 4 envíos después. La latencia cuenta el tiempo de los envíos y las
  esperas entre repeticiones.
- El **escenario 6G** usa valores candidatos (400 MHz, envíos de 62.5 µs, hasta 1024QAM) con el LDPC de 5G: no hay norma de 6G.
