import json
import shutil
import subprocess
from pathlib import Path

import altair as alt
import numpy as np
import pandas as pd
import streamlit as st

RAIZ = Path(__file__).resolve().parent
EJECUTABLE = RAIZ / "codificador_6g.exe"
DATA = RAIZ / "data"
SALIDA = DATA / "salida"
VALORES_N = [8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]
METODOS = {
    "auto": "Adaptable (el más pequeño)",
    "uniforme": "Uniforme (longitud fija)",
    "huffman": "Huffman (mínima varianza)",
    "lzw": "LZW (diccionario)",
}
COLOR = "#2a78d6"

st.set_page_config(page_title="Codificador 5G/6G", layout="wide")


# ---------------------------------------------------------------- utilidades

def tam(b):
    for unidad in ["bytes", "KB", "MB", "GB"]:
        if b < 1024 or unidad == "GB":
            return f"{b:,.0f} {unidad}" if unidad == "bytes" else f"{b:,.2f} {unidad}"
        b /= 1024


def num(x, d=4):
    return "N/A" if x is None else f"{x:,.{d}f}"


def compilar_si_hace_falta():
    """Compila el núcleo C++ si no existe o si algún fuente es más reciente."""
    fuentes = list((RAIZ / "src").glob("*.cpp")) + list((RAIZ / "include").glob("*.h"))
    if EJECUTABLE.exists() and EJECUTABLE.stat().st_mtime >= max(f.stat().st_mtime for f in fuentes):
        return True
    if shutil.which("g++") is None:
        st.error("No se encontró `g++` para compilar el núcleo C++. Instálalo (MinGW en Windows) o compila manualmente:\n\n"
                 "`g++ -O2 -std=c++17 src/*.cpp -o codificador_6g.exe`")
        return False
    with st.spinner("Compilando el núcleo C++..."):
        cmd = ["g++", "-O2", "-std=c++17", *[str(f) for f in sorted((RAIZ / "src").glob("*.cpp"))], "-o", str(EJECUTABLE)]
        r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        st.error("Error al compilar el núcleo C++")
        st.code(r.stderr[-5000:])
        return False
    return True


def ejecutar(args):
    r = subprocess.run([str(EJECUTABLE), *args], capture_output=True, cwd=RAIZ)
    return r.returncode, r.stdout.decode("utf-8", "replace"), r.stderr.decode("utf-8", "replace")


def grafico_barras(df, x, y, titulo_x, titulo_y, tooltip, orden_x=None):
    base = alt.Chart(df).mark_bar(color=COLOR, cornerRadiusTopLeft=4, cornerRadiusTopRight=4)
    chart = base.encode(
        x=alt.X(f"{x}:N", title=titulo_x, sort=orden_x, axis=alt.Axis(labelAngle=0)),
        y=alt.Y(f"{y}:Q", title=titulo_y, axis=alt.Axis(grid=True, gridOpacity=0.3)),
        tooltip=tooltip,
    ).properties(height=260)
    st.altair_chart(chart, width="stretch")


def grafico_lineas(df, y, titulo_y, formato=".3f"):
    base = alt.Chart(df).encode(
        x=alt.X("n_bits:Q", title="N (bits/símbolo)", scale=alt.Scale(type="log", base=2),
                axis=alt.Axis(values=VALORES_N, labelAngle=-45)),
        y=alt.Y(f"{y}:Q", title=titulo_y, axis=alt.Axis(grid=True, gridOpacity=0.3)),
        tooltip=[alt.Tooltip("n_bits:Q", title="N"), alt.Tooltip(f"{y}:Q", title=titulo_y, format=formato)],
    )
    chart = (base.mark_line(color=COLOR, strokeWidth=2) + base.mark_point(color=COLOR, filled=True, size=64)).properties(height=220)
    st.altair_chart(chart, width="stretch")


def tabla_medidas(filas):
    """Tabla de dos columnas con la altura justa para que se vean todas las filas."""
    st.dataframe(pd.DataFrame(filas, columns=["Medida", "Valor"]), hide_index=True, height=35 * (len(filas) + 1) + 3)


def marcar_diferencias(a, b):
    return "".join("^" if x != y else " " for x, y in zip(a, b))


def boton_descarga(ruta, etiqueta, nombre, clave):
    ruta = Path(ruta) if ruta else None
    if not ruta or not ruta.exists():
        return
    if ruta.stat().st_size > 512 * 1024 * 1024:
        st.caption(f"{etiqueta}: archivo muy grande para descargar desde la página, está en `{ruta}`")
        return
    with open(ruta, "rb") as f:
        st.download_button(etiqueta, f, file_name=nombre, key=clave, width="stretch")


# ---------------------------------------------------------------- agente de IA (DQN con detección de cambios)

METODOS_AGENTE = ["Uniforme", "Huffman", "LZW"]
N_AGENTE = [8, 16, 32, 64, 128, 256, 512, 1024, 2048]


def seccion_agente(res):
    st.subheader("Agente DQN: mejor codificación y N para el archivo")
    st.caption("Elige entre Uniforme, Huffman y LZW con N de 8 a 2048 la combinación que más reduce este archivo.")
    if st.button("Ejecutar agente", type="primary"):
        ruta = DATA / res["archivo"]
        salida = SALIDA / f"agente_{Path(res['archivo']).stem}"
        salida.mkdir(parents=True, exist_ok=True)
        ruta_json = salida / "agente.json"
        with st.spinner("El agente está recorriendo el archivo..."):
            rc, out, err = ejecutar([str(ruta), "--agente", "--salida", str(salida), "--json", str(ruta_json)])
        if rc != 0 or not ruta_json.exists():
            st.error("El agente terminó con error" + (" (una reconstrucción no fue idéntica)" if rc == 4 else ""))
            st.code(err or out)
            return
        st.session_state["agente"] = {"archivo": res["archivo"], "datos": json.loads(ruta_json.read_text(encoding="utf-8"))}

    ag = st.session_state.get("agente")
    if not ag or ag["archivo"] != res["archivo"]:
        return
    d = ag["datos"]
    cfg, dec, apr, ver = d["configuracion"], d["decision"], d["aprendizaje"], d["verificacion"]
    acciones = [a["nombre"] for a in d["acciones"]]

    st.success(f"**{dec['metodo']} con N = {dec['n_bits']} bits**")
    m1, m2, m3, m4, m5 = st.columns(5)
    m1.metric("Comprimido", tam(dec["bytes_comprimido"]), delta=f"{-dec['ahorro'] * 100:+.1f}%", delta_color="inverse",
              help=f"Original: {tam(cfg['tamano'])}")
    m2.metric("Razón de compresión", f"{dec['razon']:.3f}:1")
    m3.metric("L̄ / H(X)", f"{dec['longitud_media']:.3f} / {dec['entropia']:.3f}")
    m4.metric("Varianza σ²", f"{dec['varianza']:.4f}")
    m5.metric("Reconstrucción", "idéntica" if dec["identico"] else "distinta")

    st.markdown(f"**Aprendizaje por bloques** · {cfg['pasos']:,} pasos sobre {cfg['bloques']:,} bloques de {cfg['bytes_bloque']:,} bytes · "
                f"cambios detectados: {apr['alarmas']} · bloques con la mejor codificación: {apr['acierto_bloques'] * 100:.1f}%")
    pasos_df = pd.DataFrame(d["pasos"])
    dec_df = pasos_df.assign(Codificación=pasos_df["a"].map(dict(enumerate(acciones))),
                             Método=pasos_df["a"].map(lambda a: d["acciones"][a]["metodo"]))
    usadas = [a for a in acciones if a in set(dec_df["Codificación"])]
    puntos = alt.Chart(dec_df).mark_tick(thickness=1.5, size=12).encode(
        x=alt.X("t:Q", title="Paso"), y=alt.Y("Codificación:N", sort=usadas, title=None),
        color=alt.Color("Método:N", scale=alt.Scale(domain=METODOS_AGENTE, range=["#8a8a8a", "#2a78d6", "#eb6834"]),
                        legend=alt.Legend(orient="top", title=None)),
        tooltip=[alt.Tooltip("t:Q", title="Paso"), alt.Tooltip("b:Q", title="Bloque"), "Codificación:N",
                 alt.Tooltip("rw:Q", title="Bits ahorrados", format=".3f")])
    alarmas = alt.Chart(pasos_df[pasos_df["al"]]).mark_rule(strokeDash=[4, 3], color="#52514e").encode(
        x="t:Q", tooltip=[alt.Tooltip("t:Q", title="Cambio detectado")])
    st.altair_chart((puntos + alarmas).properties(height=alt.Step(30)), width="stretch")

    curvas = pd.DataFrame({"t": pasos_df["t"], "Agente": pasos_df["rw"].rolling(20, min_periods=1).mean(),
                           "Mejor del bloque": pasos_df["rwo"].rolling(20, min_periods=1).mean()}).melt("t", var_name="Serie", value_name="Bits ahorrados")
    st.altair_chart(alt.Chart(curvas).mark_line(strokeWidth=2).encode(
        x=alt.X("t:Q", title="Paso"), y=alt.Y("Bits ahorrados:Q", axis=alt.Axis(grid=True, gridOpacity=0.3)),
        color=alt.Color("Serie:N", scale=alt.Scale(domain=["Agente", "Mejor del bloque"], range=["#2a78d6", "#1baf7a"]),
                        legend=alt.Legend(orient="top", title=None)),
        tooltip=[alt.Tooltip("t:Q", title="Paso"), "Serie:N", alt.Tooltip("Bits ahorrados:Q", format=".3f")]).properties(height=220),
        width="stretch")

    st.markdown(f"**Codificaciones medidas sobre el archivo completo** ({dec['evaluaciones_archivo']} de 27)")
    st.dataframe(pd.DataFrame([{
        "Codificación": c["nombre"], "Origen": c["origen"], "Total (bytes)": c["total"], "Cabecera": c["cabecera"], "Datos": c["carga"],
        "Razón": c["razon"], "H(X)": c["entropia"], "L̄": c["longitud_media"], "σ²": c["varianza"], "Elegida": "Sí" if c["elegida"] else ""}
        for c in sorted(d["evaluadas"], key=lambda c: c["total"])]), hide_index=True,
        column_config={c: st.column_config.NumberColumn(format="%.4f") for c in ["Razón", "H(X)", "L̄", "σ²"]})

    if ver["exhaustivo"]:
        st.markdown("**Verificación: las 27 combinaciones sobre el archivo completo** · mejor: "
                    f"{ver['mejor_real_nombre']} {'(coincide con el agente)' if ver['acierto'] else f'(el agente quedó a {ver['diferencia'] * 100:.3f}%)'}")
        todas = pd.DataFrame([{"Método": c["metodo"], "N": c["n_bits"], "Razón": c["razon"], "Total": c["total"], "Codificación": c["nombre"]}
                              for c in ver["todas"]])
        base = alt.Chart(todas).encode(
            x=alt.X("N:O", title="N (bits por símbolo)", sort=N_AGENTE, axis=alt.Axis(labelAngle=0)),
            y=alt.Y("Método:N", sort=METODOS_AGENTE, title=None),
            tooltip=["Codificación:N", alt.Tooltip("Total:Q", title="Bytes", format=","), alt.Tooltip("Razón:Q", format=".3f")])
        celdas = base.mark_rect(cornerRadius=3, stroke="#ffffff", strokeWidth=2).encode(
            color=alt.Color("Razón:Q", title="Razón", scale=alt.Scale(scheme="blues")))
        textos = base.mark_text(fontSize=11).encode(
            text=alt.Text("Razón:Q", format=".2f"),
            color=alt.condition(f"datum['Razón'] > {float(todas['Razón'].median())}", alt.value("#ffffff"), alt.value("#1f1f1f")))
        marco = alt.Chart(todas[todas["Codificación"] == dec["nombre"]]).mark_rect(fill=None, stroke="#eb6834", strokeWidth=3).encode(
            x=alt.X("N:O", sort=N_AGENTE), y=alt.Y("Método:N", sort=METODOS_AGENTE))
        st.altair_chart((celdas + textos + marco).properties(height=170), width="stretch")

    boton_descarga(dec["archivo_comprimido"], "Descargar el archivo comprimido por el agente (.ti6g)",
                   f"{Path(res['archivo']).stem}_agente.ti6g", "dl_agente")


# ---------------------------------------------------------------- 5G y 6G según los estándares

def _tiempo(s):
    return f"{s * 1e3:,.2f} ms" if s < 1 else f"{s:,.2f} s"


def _rango(a, b):
    return _tiempo(a) if abs(a - b) < 1e-12 else f"{_tiempo(a)} a {_tiempo(b)}"


ESTANDARES = pd.DataFrame([
    ("Velocidad garantizada a cada usuario", "100 Mbit/s", "300 a 500 Mbit/s"),
    ("Velocidad máxima", "20 Gbit/s", "50 a 200 Gbit/s"),
    ("Demora (latencia)", "4 ms (1 ms en servicios críticos)", "0.1 a 1 ms"),
    ("Confiabilidad", "99.999 %", "99.999 % a 99.99999 %"),
    ("Dispositivos conectados por km²", "1 millón", "1 a 100 millones"),
    ("Velocidad de desplazamiento soportada", "hasta 500 km/h", "500 a 1000 km/h"),
], columns=["", "5G", "6G"])


def seccion_enlace(res, d):
    st.subheader("Cómo viajaría este archivo por 5G y 6G")
    ag = st.session_state.get("agente")
    if ag and ag["archivo"] == res["archivo"]:
        ruta_comp, como = ag["datos"]["decision"]["archivo_comprimido"], f"agente de IA: {ag['datos']['decision']['nombre']}"
    else:
        ruta_comp, como = d["compresion"]["archivo"], f"{d['codificacion']['metodo_elegido']}, N = {d['fuente']['n_bits']}"

    # Red 5G real: banda de 3.5 GHz con 100 MHz, calculada con las reglas de 3GPP en buenas condiciones de señal
    ruta_json = SALIDA / f"enlace_{Path(res['archivo']).stem}.json"
    rc, out, err = ejecutar([str(DATA / res["archivo"]), "--enlace", "--comprimido", str(ruta_comp), "--json", str(ruta_json)])
    if rc != 0 or not ruta_json.exists():
        st.error("No se pudo calcular la transmisión")
        st.code(err or out)
        return
    e = json.loads(ruta_json.read_text(encoding="utf-8"))
    cfg = e["configuracion"]
    red = next(x for x in e["enlaces"] if x["banda"] == "n78")
    obj = {(o["generacion"][:2], "usuario" if "usuario" in o["nombre"] else "maxima"): o for o in e["objetivos"]}

    st.markdown(f"Archivo: **{tam(cfg['bytes_original'])}** sin comprimir · **{tam(cfg['bytes_comprimido'])}** comprimido ({como})")

    def tarjeta(col, titulo, sin, con, ayuda):
        """Tiempo de envío del archivo comprimido y cuánto cambia frente al original."""
        (s0, s1), (c0, c1) = sin, con
        col.metric(titulo, _rango(c0, c1), delta=f"{((c0 + c1) / (s0 + s1) - 1) * 100:+.1f}% frente a sin comprimir",
                   delta_color="inverse", help=f"Sin comprimir: {_rango(s0, s1)}. {ayuda}")

    def tiempos(o):
        return ((o["original"]["tiempo_min_s"], o["original"]["tiempo_max_s"]),
                (o["comprimido"]["tiempo_min_s"], o["comprimido"]["tiempo_max_s"]))

    c5, c6 = st.columns(2)
    with c5:
        st.markdown("#### 5G")
        tarjeta(st, "Con la velocidad garantizada a cada usuario (100 Mbit/s)", *tiempos(obj[("5G", "usuario")]), "Incluye la demora de 4 ms.")
        tr = red["original"]["tiempo_total_s"], red["comprimido"]["tiempo_total_s"]
        tarjeta(st, "En una red 5G real (banda de 3.5 GHz)", (tr[0], tr[0]), (tr[1], tr[1]),
                "Calculado con las normas técnicas de 5G (3GPP) para una red de 100 MHz con buena señal.")
        tarjeta(st, "Con la velocidad máxima (20 Gbit/s)", *tiempos(obj[("5G", "maxima")]), "Incluye la demora de 4 ms.")
    with c6:
        st.markdown("#### 6G")
        tarjeta(st, "Con la velocidad garantizada a cada usuario (300 a 500 Mbit/s)", *tiempos(obj[("6G", "usuario")]),
                "Incluye la demora de 0.1 a 1 ms.")
        tarjeta(st, "Con la velocidad máxima (50 a 200 Gbit/s)", *tiempos(obj[("6G", "maxima")]), "Incluye la demora de 0.1 a 1 ms.")

    # Comparación en una sola gráfica (para 6G, el valor medio de su rango)
    filas = []
    for nombre, (sin, con) in [("5G · velocidad garantizada", tiempos(obj[("5G", "usuario")])),
                               ("5G · red real (3.5 GHz)", ((tr[0], tr[0]), (tr[1], tr[1]))),
                               ("5G · velocidad máxima", tiempos(obj[("5G", "maxima")])),
                               ("6G · velocidad garantizada", tiempos(obj[("6G", "usuario")])),
                               ("6G · velocidad máxima", tiempos(obj[("6G", "maxima")]))]:
        for archivo, (a, b) in (("Sin comprimir", sin), ("Comprimido", con)):
            filas.append({"Caso": nombre, "Archivo": archivo, "Tiempo (ms)": (a + b) / 2 * 1e3, "Texto": _tiempo((a + b) / 2)})
    df = pd.DataFrame(filas)
    orden = list(dict.fromkeys(df["Caso"]))
    base = alt.Chart(df).encode(
        y=alt.Y("Caso:N", sort=orden, title=None, axis=alt.Axis(labelLimit=260)),
        yOffset=alt.YOffset("Archivo:N", sort=["Sin comprimir", "Comprimido"]),
        x=alt.X("Tiempo (ms):Q", title="Tiempo de envío (ms)", axis=alt.Axis(grid=True, gridOpacity=0.3)))
    barras = base.mark_bar(cornerRadiusEnd=4, height=12).encode(
        color=alt.Color("Archivo:N", scale=alt.Scale(domain=["Sin comprimir", "Comprimido"], range=["#8a8a8a", "#2a78d6"]),
                        legend=alt.Legend(orient="top", title=None)),
        tooltip=["Caso:N", "Archivo:N", alt.Tooltip("Texto:N", title="Tiempo")])
    textos = base.mark_text(align="left", dx=4, fontSize=11, color="#57606a").encode(text="Texto:N")
    st.altair_chart((barras + textos).properties(height=alt.Step(18)), width="stretch")

    st.markdown("**Qué pide cada estándar**")
    st.dataframe(ESTANDARES, hide_index=True)
    st.caption("Fuentes: UIT, IMT-2020 para 5G (ITU-R M.2410) e IMT-2030 para 6G (ITU-R M.2160). 6G todavía está en definición: "
               "sus valores son objetivos. Para 6G la gráfica usa el valor medio de cada rango.")


# ---------------------------------------------------------------- control de error (códigos de bloque lineales)

SUB = str.maketrans("0123456789", "₀₁₂₃₄₅₆₇₈₉")


def matriz_latex(nombre, filas, corte):
    """Matriz binaria con una línea vertical después de la columna 'corte', como G = [P | I_k]."""
    columnas = "c" * corte + "|" + "c" * (len(filas[0]) - corte)
    cuerpo = r" \\ ".join(" & ".join(fila) for fila in filas)
    return rf"{nombre} = \left[\begin{{array}}{{{columnas}}} {cuerpo} \end{{array}}\right]"


def prob(x):
    return "0" if x == 0 else (f"{x:.3e}" if x < 1e-3 else f"{x:.6f}")


def seccion_control_error(d, res):
    ce = d.get("control_error")
    if not ce:
        st.info("El control de error está desactivado. Elige un código en la barra lateral para proteger el flujo comprimido.")
        return
    cod, teo, sin, fec, arq = ce["codigo"], ce["teoria"], ce["sin_control"], ce["fec"], ce["arq"]
    n, k, p = cod["n"], cod["k"], teo["p"]
    HX = d["fuente"]["entropia"]
    metodo = d["codificacion"]["metodo_elegido"]

    st.subheader(f"Código de bloque lineal {cod['nombre']}")
    st.markdown(f"El flujo comprimido ({metodo}) se parte en mensajes de **k = {k}** bits y a cada uno se le agregan "
                f"**n − k = {n - k}** bits de paridad. Con esa redundancia el receptor puede **corregir** el bloque (FEC) "
                "o sólo **detectar** el error y pedir que se lo envíen otra vez (ARQ).")
    c1, c2, c3, c4 = st.columns(4)
    c1.metric("Tasa R = k/n", f"{cod['tasa']:.4f}", help=f"De cada bit enviado, {cod['tasa']:.3f} son del mensaje")
    c2.metric("Distancia mínima d_min", f"{cod['d_min']}", help="Menor peso de una palabra del código distinta de cero")
    c3.metric("Corrige / detecta", f"{cod['corrige']} / {cod['detecta']} bits",
              help="Por bloque: como FEC corrige hasta t = ⌊(d_min − 1)/2⌋ errores; como ARQ detecta hasta d_min − 1. "
                   "No las dos cosas a la vez: si corrige, un error de t + 1 bits puede corregirse mal sin avisar.")
    c4.metric("Capacidad C = 1 − H_b(p)", f"{teo['capacidad']:.4f}",
              delta="R < C" if teo["tasa_menor_capacidad"] else "R ≥ C",
              delta_color="normal" if teo["tasa_menor_capacidad"] else "inverse",
              help="Teorema de codificación de canal: con R < C existen códigos con error tan pequeño como se quiera")

    nk = f"({n},{k})"
    if cod["d_min"] >= cod["d_cota"]:
        optimo = f"la mayor posible para un código {nk} (lo dicen las cotas de Singleton, Hamming y Griesmer)"
    elif cod["d_mejor_probado"]:
        optimo = (f"la mayor posible para un código {nk}: las cotas no descartan {cod['d_cota']}, pero el programa recorrió "
                  "todas las matrices P con más distancia y no existe ninguna")
    else:
        optimo = (f"la mejor que encontró la búsqueda; las cotas no descartan hasta {cod['d_cota']} y no alcanzó a revisar "
                  "todas las matrices")
    if cod["construccion"] == "busqueda":
        st.caption(f"Con n = {n} y k = {k} el programa buscó la matriz P. Su d_min = {cod['d_min']} es {optimo}.")
    elif cod["d_min"] < cod["d_mejor"]:
        st.caption(f"Esta P da d_min = {cod['d_min']}; con «n y k» en la barra lateral el programa encuentra un código {nk} "
                   f"con d_min = {cod['d_mejor']}.")

    col_a, col_b = st.columns(2)
    with col_a:
        st.markdown("**Matrices del código**")
        if n <= 15:
            st.latex(matriz_latex("G", cod["G"], n - k))
            st.latex(matriz_latex("H", cod["H"], n - k))
        else:
            st.code("G =\n" + "\n".join(cod["G"]) + "\n\nH =\n" + "\n".join(cod["H"]), language="text")
        st.caption(("G·Hᵀ = 0 se cumple" if cod["GHt_cero"] else "G·Hᵀ ≠ 0: la matriz no es válida") +
                   (". Código perfecto: cada síndrome corresponde a un error de hasta t bits." if cod["perfecto"] else "."))
    with col_b:
        st.markdown("**Codificación y decodificación** (sumas módulo 2)")
        st.latex(r"c = [\,b \mid m\,] = m\,G \qquad G = [\,P \mid I_k\,]")
        st.latex(r"H = [\,I_{n-k} \mid P^{T}\,] \qquad G\,H^{T} = 0")
        st.latex(r"r = c + e \qquad s = r\,H^{T} = e\,H^{T}")
        st.markdown("Si **s = 0** el bloque se acepta. Si no, FEC toma de la tabla de síndromes el error más probable ê "
                    "(el de menor peso) y corrige ĉ = r + ê; ARQ descarta el bloque y pide que se repita.")

    col_a, col_b = st.columns(2)
    with col_a:
        st.markdown(f"**Estructura de la codificación** ({cod['total_palabras']:,} palabras)")
        palabras = pd.DataFrame(cod["palabras"]).rename(columns={"m": "Mensaje m", "b": "Paridad b = mP",
                                                                   "c": "Palabra c = [b | m]", "peso": "Peso"})
        st.dataframe(palabras, hide_index=True, height=min(35 * (len(palabras) + 1) + 3, 400))
        pesos = ", ".join(f"A{str(x['peso']).translate(SUB)} = {x['palabras']:,}" for x in cod["distribucion_pesos"])
        st.caption((f"Se muestran las primeras {len(palabras)}. " if cod["total_palabras"] > len(palabras) else "") +
                   f"Palabras por peso: {pesos}.")
    with col_b:
        st.markdown(f"**Estructura del síndrome** ({cod['total_sindromes']:,} síndromes)")
        sindromes = pd.DataFrame(cod["sindromes"])
        sindromes["Error que corrige"] = [("ninguno" if x["peso"] == 0 else f"bit {x['posicion']}" if x["peso"] == 1
                                           else f"{x['peso']} bits") for _, x in sindromes.iterrows()]
        st.dataframe(sindromes[["s", "e", "Error que corrige"]].rename(columns={"s": "Síndrome s", "e": "Error ê (líder)"}),
                     hide_index=True, height=min(35 * (len(sindromes) + 1) + 3, 400))
        if cod["total_sindromes"] > len(sindromes):
            st.caption(f"Se muestran los primeros {len(sindromes)}.")

    st.markdown("**Bloques transmitidos con FEC** (los primeros y los primeros que llegaron con error)")
    st.dataframe(pd.DataFrame(fec["muestra"])[["bloque", "m", "c", "e", "r", "s", "e_est", "c_est", "m_est", "estado"]].rename(columns={
        "bloque": "Bloque", "c": "c = mG", "e": "Error e", "r": "Recibido r", "s": "s = rHᵀ", "e_est": "ê",
        "c_est": "ĉ = r + ê", "m_est": "m̂", "estado": "Resultado"}), hide_index=True)

    # Original → comprimido → recuperado, con y sin control de error
    st.subheader("Original, comprimido y recuperado")
    estrategias = [("Sin control de error", sin), ("FEC (corrige)", fec), ("ARQ (detecta y repite)", arq)]
    filas = []
    for nombre, x in estrategias:
        filas.append({
            "Estrategia": nombre,
            "Bits enviados": f"{x['bits_enviados']:,}",
            "Agregados": f"{x['bits_enviados'] / x['bits_mensaje'] - 1:+.1%}" if x["bits_mensaje"] else "—",
            "Errores canal": f"{x['errores_canal']:,}",
            "Errados al final": f"{x['bits_erroneos']:,}",
            "BER final": prob(x["ber_residual"]),
            "I(X;Y) bit": f"{x['IXY_bit']:.6f}",
            "I(X;Y) símbolo": f"{x['simbolos_IXY']:.4f}",
            "Símbolos errados": f"{x['tasa_error_simbolo']:.2%}",
            "Idéntico": "Sí" if x["identico"] else "No",
        })
    st.dataframe(pd.DataFrame(filas), hide_index=True)
    st.caption("I(X;Y) bit: flujo comprimido enviado ↔ flujo recuperado. I(X;Y) símbolo: símbolos del archivo "
               f"original ↔ símbolos del archivo recuperado, posición por posición; su máximo es H(X) = {HX:.4f} bits, cuando llega todo bien. "
               "Si el decodificador se corre, lo que sigue cuenta como error aunque el contenido reaparezca unas posiciones más allá.")

    graf = pd.DataFrame({"Estrategia": [e for e, _ in estrategias], "I": [x["simbolos_IXY"] for _, x in estrategias]})
    graf["Texto"] = [f"{v:.4f} bits ({v / HX:.1%} de H)" if HX > 0 else f"{v:.4f} bits" for v in graf["I"]]
    base = alt.Chart(graf).encode(
        y=alt.Y("Estrategia:N", sort=list(graf["Estrategia"]), title=None, axis=alt.Axis(labelLimit=220)),
        x=alt.X("I:Q", title="I(X;Y) original ↔ recuperado (bits/símbolo)", scale=alt.Scale(domain=[0, max(HX, 1e-9) * 1.25]),
                axis=alt.Axis(grid=True, gridOpacity=0.3, tickCount=8)),
        tooltip=["Estrategia:N", alt.Tooltip("Texto:N", title="I(X;Y)")])
    barras = base.mark_bar(color=COLOR, cornerRadiusEnd=4, height=18)
    textos = base.mark_text(align="left", dx=4, fontSize=11, color="#57606a").encode(text="Texto:N")
    regla = alt.Chart(pd.DataFrame({"H": [HX]})).mark_rule(strokeDash=[4, 3], color="#52514e").encode(
        x="H:Q", tooltip=[alt.Tooltip("H:Q", title="H(X)", format=".4f")])
    st.altair_chart((barras + textos + regla).properties(height=alt.Step(34)), width="stretch")

    if p == 0:
        st.info("Con p = 0 el canal no se equivoca y el código sólo agrega bits. Sube la probabilidad de error en la barra "
                "lateral para comparar las tres estrategias.")
    else:
        puntos = []
        if sin["bits_erroneos"]:
            reduccion = 1 - fec["bits_erroneos"] / sin["bits_erroneos"]
            puntos.append(f"FEC bajó los bits errados de {sin['bits_erroneos']:,} a {fec['bits_erroneos']:,} "
                          f"({reduccion:.1%} menos) enviando {fec['bits_enviados'] / fec['bits_mensaje'] - 1:.0%} más bits.")
        puntos.append(f"ARQ terminó con {arq['bits_erroneos']:,} bits errados después de {arq['retransmisiones']:,} "
                      f"retransmisiones; aprovechó {arq['eficiencia']:.3f} bits de mensaje por bit enviado (FEC: {fec['eficiencia']:.3f}).")
        if fec["bits_erroneos"] and metodo in ("Huffman", "LZW") and fec["tasa_error_simbolo"] > 0.01:
            if metodo == "Huffman":
                causa = ("Con Huffman los códigos tienen distinto largo: un bit errado hace leer mal unos pocos símbolos y el "
                         "decodificador se vuelve a sincronizar, pero si leyó símbolos de más o de menos, todo lo que sigue "
                         "queda corrido de su posición.")
            else:
                causa = ("Con LZW un bit errado cambia un código: esa frase sale mal, lo que sigue queda corrido de su "
                         "posición y la entrada equivocada del diccionario repite el error cada vez que se usa.")
            puntos.append(f"{causa} Por eso, comparando posición por posición, con sólo {fec['bits_erroneos']:,} bits errados "
                          f"queda mal el {fec['tasa_error_simbolo']:.1%} de los símbolos. A un archivo comprimido no le basta "
                          "con reducir los errores: tiene que llegar sin ninguno.")
        elif metodo == "Uniforme" and fec["bits_erroneos"]:
            puntos.append("Con la codificación uniforme un bit errado sólo daña su propio símbolo, así que el daño no se propaga.")
        if teo["tasa_menor_capacidad"]:
            puntos.append(f"R = {cod['tasa']:.3f} < C = {teo['capacidad']:.3f}: según el teorema de codificación de canal hay "
                          "códigos de esta tasa con error tan pequeño como se quiera, pero necesitan bloques mucho más largos "
                          f"que n = {n}.")
        else:
            puntos.append(f"R = {cod['tasa']:.3f} ≥ C = {teo['capacidad']:.3f}: ningún código de esta tasa puede dejar el error "
                          "tan pequeño como se quiera.")
        st.markdown("\n".join(f"- {x}" for x in puntos))

    st.markdown("**Teoría y medición por bloque**")
    col_a, col_b = st.columns([1, 1])
    with col_a:
        st.latex(r"P_{\mathrm{FEC}}(\text{bloque mal}) = 1 - \sum_{w} \alpha_w\, p^{w}(1-p)^{n-w}")
        st.latex(r"P(\text{error no detectado}) = \sum_{w \ge 1} A_w\, p^{w}(1-p)^{n-w}")
        st.latex(r"E[\text{envíos}] = \frac{1-q^{M}}{1-q}, \qquad q = P(s \ne 0)")
        st.caption("α_w: síndromes cuyo error ê tiene peso w. A_w: palabras del código con peso w. M: envíos máximos.")
    with col_b:
        bloques_fec, bloques_arq, envios = max(1, fec["bloques"]), max(1, arq["bloques"]), max(1, arq["transmisiones"])
        filas_t = [
            ("Sin control: bloque de k bits con error", prob(teo["p_bloque_sin_control"]), "—"),
            ("FEC: bloque mal decodificado", prob(teo["p_bloque_fec"]),
             prob((fec["bloques_mal_corregidos"] + fec["bloques_no_detectados"]) / bloques_fec)),
            ("ARQ: error no detectado por envío", prob(teo["p_no_detectado"]), prob(arq["bloques_no_detectados"] / envios)),
            ("ARQ: envíos por bloque", f"{teo['transmisiones_esperadas']:.4f}", f"{arq['transmisiones'] / bloques_arq:.4f}"),
            ("ARQ: bloque entregado con error", prob(teo["p_bloque_arq"]),
             prob((arq["bloques_no_detectados"] + arq["bloques_fallidos"]) / bloques_arq)),
            ("ARQ: eficiencia R / E[envíos]", f"{teo['eficiencia_arq']:.4f}", f"{arq['eficiencia']:.4f}"),
        ]
        st.dataframe(pd.DataFrame(filas_t, columns=["Por bloque", "Teórico", "Medido"]), hide_index=True,
                     height=35 * (len(filas_t) + 1) + 3)

    st.markdown("**ARQ: bloques que se tuvieron que repetir**")
    a1, a2, a3 = st.columns(3)
    a1.metric("Retransmisiones", f"{arq['retransmisiones']:,}", help=f"{arq['transmisiones']:,} envíos para {arq['bloques']:,} bloques")
    a2.metric("Errores no detectados", f"{arq['bloques_no_detectados']:,}", help="El error era una palabra del código: s = 0")
    a3.metric("Bloques que agotaron los envíos", f"{arq['bloques_fallidos']:,}", help=f"Máximo {teo['arq_max']} envíos por bloque")
    st.dataframe(pd.DataFrame(arq["muestra"])[["bloque", "m", "c", "e", "r", "s", "intentos", "estado"]].rename(columns={
        "bloque": "Bloque", "c": "c = mG", "e": "Error (1.er envío)", "r": "Recibido (1.er envío)", "s": "s (1.er envío)",
        "intentos": "Envíos", "estado": "Resultado"}), hide_index=True)
    st.caption("Se supone repetición selectiva ideal: los avisos del receptor (ACK si s = 0, NAK si no) llegan sin errores "
               "y no se cuenta el tiempo de espera, así que la eficiencia es la mejor que puede tener ARQ con este código.")

    b1, b2 = st.columns(2)
    for col, x, nombre, clave in ((b1, fec, "FEC", "dl_fec"), (b2, arq, "ARQ", "dl_arq")):
        with col:
            if x["identico"]:
                st.caption(f"Con {nombre} el archivo recuperado es idéntico al original.")
            else:
                boton_descarga(x["archivo"], f"Descargar recuperado con {nombre}",
                               f"{nombre.lower()}_{Path(res['archivo']).name}", clave)

    st.markdown("**En 5G y 6G**")
    st.markdown("Primero se comprime y después se protege: es el orden que respalda el teorema de separación fuente-canal "
                "de Shannon. 5G NR hace lo mismo (3GPP TS 38.212):")
    st.markdown("- Los datos van con códigos **LDPC** y la señalización con códigos **Polar**, de miles de bits por bloque.\n"
                "- Los mensajes de control cortos (3 a 11 bits) usan un código de bloque lineal (32, K) que se calcula "
                "con c = m·G, usando la tabla de 32 × 11 que fija la norma; 1 bit se repite y 2 bits llevan "
                "un bit de paridad.\n"
                "- Un **CRC** detecta los errores que quedan y **HARQ** combina corrección y retransmisión.\n\n"
                "El cálculo de la red real de la pestaña 9 ya incluye el CRC y los bloques LDPC de esa norma. Para 6G, "
                "en junio de 2026 3GPP acordó soportar un nuevo grafo base de LDPC (BG3); el resto de la codificación sigue "
                "en estudio.")


def entropia(v):
    v = np.asarray(v, dtype=float).ravel()
    v = v[v > 0]
    return float(-(v * np.log2(v)).sum())


def calculadora_canal_discreto():
    st.subheader("Canal discreto con cualquier matriz P(y|x)")
    st.caption("Escribe la distribución de entrada P(X) y la matriz de transición P(y|x) de cualquier canal discreto sin "
               "memoria; cada fila debe sumar 1.")
    c1, c2, _ = st.columns([1, 1, 2])
    J = int(c1.number_input("Entradas J", min_value=2, max_value=8, value=2, key="canal_J"))
    K = int(c2.number_input("Salidas K", min_value=2, max_value=8, value=3, key="canal_K"))
    xs = [f"x{j}".translate(SUB) for j in range(J)]
    ys = [f"y{i}".translate(SUB) for i in range(K)]
    if (J, K) == (2, 3):
        valores = [[0.6, 0.7, 0.2, 0.1], [0.4, 0.1, 0.3, 0.6]]
    else:
        valores = [[round(1 / J, 6)] + [round(1 / K, 6)] * K for _ in range(J)]
    columnas = ["P(x)"] + [f"P({y}|x)" for y in ys]
    tabla = st.data_editor(pd.DataFrame(valores, index=xs, columns=columnas), key=f"canal_{J}_{K}",
                           column_config={c: st.column_config.NumberColumn(c, min_value=0.0, max_value=1.0, step=0.0001,
                                                                           format="%.4f") for c in columnas})
    datos = tabla.to_numpy(dtype=float)
    if np.isnan(datos).any():
        st.warning("Hay celdas vacías.")
        return
    px, T = datos[:, 0], datos[:, 1:]
    problemas = []
    if abs(px.sum() - 1) > 1e-6:
        problemas.append(f"P(x) suma {px.sum():.6f}")
    problemas += [f"la fila {x} suma {T[j].sum():.6f}" for j, x in enumerate(xs) if abs(T[j].sum() - 1) > 1e-6]
    if problemas:
        st.warning("Cada distribución debe sumar 1: " + "; ".join(problemas) + ".")
        return

    conjunta = px[:, None] * T
    py = conjunta.sum(axis=0)
    HX, HY, HXY = entropia(px), entropia(py), entropia(conjunta)
    HYdX = float(sum(px[j] * entropia(T[j]) for j in range(J)))
    HXdY = HXY - HY
    col_a, col_b = st.columns([1, 1])
    with col_a:
        st.latex(r"P(x_j, y_k) = P(y_k|x_j)\,P(x_j) \qquad P(x_j|y_k) = \frac{P(x_j, y_k)}{P(y_k)}")
        st.markdown("**P(x, y)** y en la última fila **P(y)**")
        st.dataframe(pd.DataFrame(np.vstack([conjunta, py]), index=xs + ["P(y)"], columns=ys).style.format("{:.6f}"))
        st.markdown("**P(x|y)**")
        con_y = np.divide(conjunta, py, out=np.zeros_like(conjunta), where=py > 0)
        st.dataframe(pd.DataFrame(con_y, index=xs, columns=ys).style.format("{:.6f}"))
    with col_b:
        tabla_medidas([
            ("H(X)", f"{HX:.7f} bits"), ("H(Y)", f"{HY:.7f} bits"), ("H(X,Y)", f"{HXY:.7f} bits"),
            ("H(X|Y)", f"{HXdY:.7f} bits"), ("H(Y|X)", f"{HYdX:.7f} bits"),
            ("I(X;Y) = H(X) − H(X|Y)", f"{HX - HXdY:.7f} bits"),
            ("I(Y;X) = H(Y) − H(Y|X)", f"{HY - HYdX:.7f} bits"),
            ("I(X;Y) = H(X) + H(Y) − H(X,Y)", f"{HX + HY - HXY:.7f} bits"),
        ])
        st.caption("Las tres formas dan lo mismo: la información mutua es simétrica, I(X;Y) = I(Y;X), y nunca es negativa.")


# ---------------------------------------------------------------- simulación 5G / 6G (capa física con LDPC)

ESCENARIOS_SIM = {
    "embb": ("5G eMBB · banda ancha (3.5 GHz, 100 MHz)", 20.0),
    "urllc": ("5G URLLC · baja latencia y alta confiabilidad", 10.0),
    "6g": ("6G · escenario candidato (400 MHz)", 30.0),
}
CAMINOS = ["ldpc", "ldpc_igual", "bloque", "sin"]
COLORES_CAMINO = ["#2a78d6", "#1baf7a", "#eb6834", "#8a8a8a"]
MODULACION = {2: "QPSK", 4: "16QAM", 6: "64QAM", 8: "256QAM", 10: "1024QAM"}
LIMITE_SIM = 4 * 1024 * 1024  # por encima de esto se simulan sólo los primeros 4 MB del archivo comprimido


def puntos_qam(qm):
    """Constelación ideal de TS 38.211 §5.1 (misma recursión que el núcleo C++)."""
    m = qm // 2
    escala = 1 / np.sqrt(2 * (4 ** m - 1) / 3)
    niveles = []
    for p in range(2 ** m):
        a = [1 - 2 * ((p >> (m - 1 - k)) & 1) for k in range(m)]
        t = 1
        if m >= 2:
            t = 2 - a[m - 1]
            for k in range(m - 2, 0, -1):
                t = 2 ** (m - k) - a[k] * t
        niveles.append(a[0] * t * escala)
    return pd.DataFrame([(i, q) for i in niveles for q in niveles], columns=["I", "Q"])


def velocidad(b):
    return f"{b / 1e9:,.2f} Gbit/s" if b >= 1e9 else f"{b / 1e6:,.1f} Mbit/s"


def seccion_simulacion(res, d):
    st.subheader("Simulación del envío por 5G y 6G")
    st.markdown("El archivo comprimido se envía **bit por bit** como lo haría una red 5G: se parte en bloques, a cada bloque se "
                "le agrega un **CRC** para detectar errores y un **código de control de error**, los bits se convierten en "
                "**símbolos** (modulación), el canal les suma **ruido**, el receptor corrige lo que puede y pide repetir lo que "
                "llegó mal (**ARQ**). Al final se descomprime y se compara con el original.")
    st.markdown("Con el mismo ruido se comparan tres formas de proteger los bits: el **LDPC**, el código que usa 5G; el **código "
                "de bloque** de la barra lateral (por ejemplo Hamming (7,4)); y **sin código** (sólo CRC).")

    ag = st.session_state.get("agente")
    if ag and ag["archivo"] == res["archivo"]:
        ruta_comp, como = Path(ag["datos"]["decision"]["archivo_comprimido"]), f"agente de IA: {ag['datos']['decision']['nombre']}"
    else:
        ruta_comp, como = Path(d["compresion"]["archivo"]), f"{d['codificacion']['metodo_elegido']}, N = {d['fuente']['n_bits']}"
    bytes_comp = ruta_comp.stat().st_size if ruta_comp.exists() else 0

    c1, c2, c3 = st.columns(3)
    escenario = c1.selectbox("Escenario", list(ESCENARIOS_SIM), format_func=lambda k: ESCENARIOS_SIM[k][0], key="sim_esc")
    snr = c2.number_input("Relación señal a ruido, SNR (dB)", min_value=-10.0, max_value=50.0, value=ESCENARIOS_SIM[escenario][1],
                          step=1.0, key=f"sim_snr_{escenario}",
                          help="Cuánto más fuerte es la señal que el ruido. Con más SNR el canal se equivoca menos y se pueden "
                               "mandar más bits por símbolo.")
    harq = c3.number_input("Envíos máximos por bloque (ARQ)", min_value=1, max_value=8, value=4, key="sim_harq",
                           help="Si un bloque llega mal (el CRC no cuadra), se pide otra vez, hasta este número de envíos.")
    c4, c5 = st.columns(2)
    probar = c4.checkbox("Probar la confiabilidad con 300 000 mensajes de 32 bytes (unos 40 s)", value=False,
                         key="sim_conf", disabled=escenario == "embb",
                         help="5G URLLC y 6G exigen que un mensaje corto llegue bien en menos de 1 ms el 99.999 % de las veces. "
                              "Con 300 000 mensajes sin fallas ya se puede afirmar.")
    completo = c5.checkbox("Archivo completo", value=bytes_comp <= LIMITE_SIM, key="sim_completo",
                           help="Con archivos grandes la simulación tarda; sin esta opción se envían los primeros 4 MB.")
    st.caption(f"Se envía el archivo comprimido: **{tam(bytes_comp)}** ({como}). Código de bloque: el de la barra lateral "
               f"({especificacion_fec}).")

    if st.button("Simular", type="primary", key="sim_boton"):
        salida = SALIDA / f"simulacion_{Path(res['archivo']).stem}"
        salida.mkdir(parents=True, exist_ok=True)
        ruta_json = salida / "simulacion.json"
        args = [str(DATA / res["archivo"]), "--simular", "--comprimido", str(ruta_comp), "--escenario", escenario,
                "--snr", repr(float(snr)), "--harq", str(int(harq)), "--fec", especificacion_fec,
                "--paquetes", str(300000 if probar and escenario != "embb" else 0),
                "--salida", str(salida), "--json", str(ruta_json)]
        if not completo:
            args += ["--max-bytes", str(LIMITE_SIM)]
        with st.spinner("Enviando el archivo bit por bit..."):
            rc, out, err = ejecutar(args)
        if rc != 0 or not ruta_json.exists():
            st.error("La simulación terminó con error")
            st.code(err or out)
            return
        st.session_state["simulacion"] = {"archivo": res["archivo"], "datos": json.loads(ruta_json.read_text(encoding="utf-8"))}
        st.session_state.pop("curvas_sim", None)

    sim = st.session_state.get("simulacion")
    if not sim or sim["archivo"] != res["archivo"]:
        st.info("Elige el escenario y pulsa **Simular**.")
        return
    s = sim["datos"]
    e, m, k, conf = s["escenario"], s["mcs"], s["canal"], s["confiabilidad"]
    caminos = s["caminos"]
    L = next(c for c in caminos if c["clave"] == "ldpc")

    st.success(f"**{e['nombre']}** · SNR {k['snr_db']:.1f} dB · cada envío dura {e['unidad_s'] * 1e3:.3f} ms · modulación "
               f"inicial **{MODULACION[m['qm']]}** ({m['qm']} bits por símbolo) con tasa del código **R = {m['tasa']:.3f}**, "
               "elegidas según la SNR")
    if not s["archivo"]["completo"]:
        st.warning(f"Se enviaron los primeros {tam(s['archivo']['bytes_simulados'])} de {tam(s['archivo']['bytes_comprimido'])}; "
                   "sin el archivo completo no se puede descomprimir, así que no hay medida contra el original.")
    m1, m2, m3, m4 = st.columns(4)
    m1.metric("Velocidad con LDPC", velocidad(L["goodput"]), help="Bits que llegaron bien por segundo")
    m2.metric("Latencia media por bloque", f"{L['latencia_media'] * 1e3:.3f} ms",
              help="Lo que tarda un bloque en llegar bien, contando los envíos repetidos")
    m3.metric("Bloques que fallaron al primer envío", f"{L['bler_primera']:.1%}",
              help="BLER: fracción de bloques con el CRC mal en el primer envío. Luego ARQ los repite.")
    m4.metric("Archivo recuperado", "idéntico" if L["identico"] else ("se pudo descomprimir" if L["descomprimido"] else "dañado"))

    st.markdown("**Las tres formas de proteger los bits, con el mismo ruido**")
    filas = []
    for c in caminos:
        filas.append({
            "Código": c["nombre"], "Tasa": f"{c['tasa']:.3f}", "Bloques": f"{c['bloques_transporte']:,}",
            "Envíos": f"{c['transmisiones']:,}", "Fallan al 1.er envío": f"{c['bler_primera']:.1%}",
            "Fallan al final": f"{c['bler_final']:.1%}", "p del canal": prob(c["ber_bruta"]),
            "Bits errados al final": prob(c["ber_final"]), "Velocidad": velocidad(c["goodput"]),
            "Latencia": f"{c['latencia_media'] * 1e3:.3f} ms", "I(X;Y) byte": f"{c['IXY_byte']:.4f}",
            "Idéntico": "Sí" if c["identico"] else "No"})
    st.dataframe(pd.DataFrame(filas), hide_index=True)
    st.caption(f"**p del canal**: fracción de bits que llegan errados antes de corregir. Es la p del canal binario simétrico de "
               f"la pestaña 5: después de decidir cada bit, este canal se comporta como un BSC con esa p. **Bits errados al "
               f"final**: después de corregir y repetir. **I(X;Y) byte**: información mutua entre el archivo original y el "
               f"recuperado, de un máximo de H(X) = {L['H_byte']:.4f} bits.")

    graf = pd.DataFrame([{"Código": c["nombre"], "clave": c["clave"], "Mbit/s": c["goodput"] / 1e6,
                          "I": c["IXY_byte"], "Texto": velocidad(c["goodput"]),
                          "TextoI": f"{c['IXY_byte']:.3f} bits"} for c in caminos])
    orden = list(graf["Código"])
    colores = alt.Scale(domain=CAMINOS, range=COLORES_CAMINO)
    g1, g2 = st.columns(2)
    with g1:
        base = alt.Chart(graf).encode(y=alt.Y("Código:N", sort=orden, title=None, axis=alt.Axis(labelLimit=200)),
                                      x=alt.X("Mbit/s:Q", title="Velocidad (Mbit/s)", axis=alt.Axis(grid=True, gridOpacity=0.3)),
                                      tooltip=["Código:N", alt.Tooltip("Texto:N", title="Velocidad")])
        st.altair_chart((base.mark_bar(cornerRadiusEnd=4, height=16).encode(color=alt.Color("clave:N", scale=colores, legend=None))
                         + base.mark_text(align="left", dx=4, fontSize=11, color="#57606a").encode(text="Texto:N"))
                        .properties(height=alt.Step(30)), width="stretch")
    with g2:
        base = alt.Chart(graf).encode(y=alt.Y("Código:N", sort=orden, title=None, axis=alt.Axis(labelLimit=200)),
                                      x=alt.X("I:Q", title="I(X;Y) original ↔ recuperado (bits por byte)",
                                              scale=alt.Scale(domain=[0, max(L["H_byte"], 1e-9) * 1.3]),
                                              axis=alt.Axis(grid=True, gridOpacity=0.3)),
                                      tooltip=["Código:N", alt.Tooltip("TextoI:N", title="I(X;Y)")])
        regla = alt.Chart(pd.DataFrame({"H": [L["H_byte"]]})).mark_rule(strokeDash=[4, 3], color="#52514e").encode(
            x="H:Q", tooltip=[alt.Tooltip("H:Q", title="H(X) por byte", format=".4f")])
        st.altair_chart((base.mark_bar(cornerRadiusEnd=4, height=16).encode(color=alt.Color("clave:N", scale=colores, legend=None))
                         + base.mark_text(align="left", dx=4, fontSize=11, color="#57606a").encode(text="TextoI:N") + regla)
                        .properties(height=alt.Step(30)), width="stretch")

    # Requisitos de la UIT frente a lo simulado con LDPC
    st.markdown(f"**Lo que pide {e['generacion']} frente a lo simulado con LDPC**")
    req = []
    if e["tasa_min"] > 0:
        meta = velocidad(e["tasa_min"]) if e["tasa_min"] == e["tasa_max"] else f"{velocidad(e['tasa_min'])} a {velocidad(e['tasa_max'])}"
        req.append(("Velocidad para cada usuario", f"≥ {meta}", velocidad(L["goodput"]), "Sí" if L["goodput"] >= e["tasa_min"] else "No"))
    meta_lat = f"≤ {e['lat_max'] * 1e3:g} ms" if e["lat_min"] == e["lat_max"] else f"{e['lat_min'] * 1e3:g} a {e['lat_max'] * 1e3:g} ms"
    req.append(("Latencia (el bloque más lento)", meta_lat, f"{L['latencia_max'] * 1e3:.3f} ms",
                "Sí" if L["latencia_max"] <= e["lat_max"] + 1e-12 else "No"))
    if conf.get("hecha"):
        exito, garantia, N = conf["exito"], conf["confiabilidad_garantizada"], conf["paquetes"]
        if garantia >= conf["requisito_min"]:
            veredicto = "Sí"
        elif exito < conf["requisito_min"]:
            veredicto = "No"
        else:
            veredicto = f"No se puede afirmar con {N:,} mensajes (hacen falta {conf['paquetes_necesarios']:,.0f})"
        meta_f = (f"≥ {conf['requisito_min']:.5%}" if conf["requisito_min"] == conf["requisito_max"]
                  else f"{conf['requisito_min']:.5%} a {conf['requisito_max']:.7%}")
        req.append((f"Confiabilidad: 32 bytes en ≤ {conf['plazo_s'] * 1e3:g} ms", meta_f,
                    f"{conf['fallas']:,} fallas en {N:,}; se puede asegurar {garantia:.5%}", veredicto))
    elif e["fiab_min"] > 0:
        req.append(("Confiabilidad: 32 bytes en ≤ 1 ms", f"≥ {e['fiab_min']:.5%}", "No se probó", "—"))
    st.dataframe(pd.DataFrame(req, columns=["Requisito", "Meta", "Simulado", "Cumple"]), hide_index=True)
    st.caption("Las metas son de la UIT (IMT-2020 para 5G e IMT-2030 para 6G) y valen para toda la red; aquí se comparan con un "
               "solo enlace. Con 0 fallas en N mensajes sólo se puede asegurar (al 95 %) que fallan menos de 3 de cada N.")

    # Constelación e información mutua
    st.markdown("**Los símbolos que llegan y la información mutua**")
    k1, k2 = st.columns([1, 1])
    with k1:
        rx = pd.DataFrame(s["constelacion"], columns=["I", "Q"])
        ideal = puntos_qam(m["qm"])
        lim = float(max(np.abs(ideal.values).max() * 1.6, 0.5))
        escala = alt.Scale(domain=[-lim, lim])
        pts = alt.Chart(rx).mark_circle(size=9, opacity=0.35, color=COLORES_CAMINO[0]).encode(
            x=alt.X("I:Q", scale=escala, title="Componente I"), y=alt.Y("Q:Q", scale=escala, title="Componente Q"))
        ref = alt.Chart(ideal).mark_point(shape="cross", size=30, color="#1f2328", filled=True).encode(x="I:Q", y="Q:Q")
        st.altair_chart((pts + ref).properties(height=330), width="stretch")
        st.caption(f"Cada cruz es un símbolo de {MODULACION[m['qm']]} ({m['qm']} bits). Los puntos son {len(rx):,} símbolos "
                   "recibidos: el ruido los mueve, y si uno cae más cerca de otra cruz, sus bits llegan errados.")
    with k2:
        b = s["bicm"]
        tabla_medidas([
            ("Información mutua por bit enviado", f"{b['info_mutua_bit']:.4f} bits"),
            ("Información mutua por símbolo", f"{b['info_mutua_simbolo']:.3f} bits"),
            ("Bits de mensaje por símbolo (R × bits por símbolo)", f"{b['tasa_codigo_simbolo']:.3f} bits"),
            ("Capacidad del canal, log₂(1 + SNR)", f"{k['capacidad_shannon']:.3f} bits"),
        ])
        st.caption("La información mutua entre cada bit enviado y lo que recibe el receptor dice cuánta información deja pasar el "
                   "canal. Si el código manda menos bits de mensaje por símbolo que esa información, puede corregir los "
                   "errores; si manda más, el bloque falla y hay que repetirlo. Nada puede pasar de la capacidad.")
        if L.get("mcs_usados"):
            st.markdown("**Modulación y tasa que usó el ajuste automático**")
            st.dataframe(pd.DataFrame([{"Modulación": MODULACION[x["qm"]], "Bits por símbolo": x["qm"], "Tasa R": f"{x['tasa']:.3f}",
                                        "Bloques": x["bloques"]} for x in L["mcs_usados"]]), hide_index=True)
            st.caption("Si un bloque falla en el primer envío, los siguientes usan una tasa más baja o menos bits por símbolo; "
                       "si llegan bien, se vuelve a subir poco a poco.")

    if L.get("ldpc"):
        x = L["ldpc"]
        with st.expander("Detalles del LDPC de la norma (primer bloque)"):
            st.dataframe(pd.DataFrame([{"Matriz base": f"BG{x['grafo_base']}", "Tamaño de cada submatriz (Zc)": x["zc"],
                                        "Partes del bloque": x["bloques_codigo"], "Bits de mensaje por parte": x["k_prima"],
                                        "Bits codificados por parte": x["n"], "CRC del bloque": x["crc_tb"],
                                        "Iteraciones medias": f"{L['iteraciones_medias']:.1f}"}]), hide_index=True)
            st.caption("El LDPC es un código de bloque lineal como el Hamming: sus palabras cumplen c·Hᵀ = 0. La diferencia es "
                       "que H es enorme y casi toda de ceros, y que el receptor corrige con la confianza de cada bit, repasando "
                       "las ecuaciones de paridad varias veces (iteraciones) hasta que el síndrome da cero.")

    # Curvas BLER frente a SNR
    st.markdown("**Bloques con error según la SNR**")
    st.caption("Un bloque de 4096 bits, un solo envío y la modulación de esta simulación: el LDPC con su tasa, el LDPC con la "
               "misma tasa que el código de bloque, el código de bloque y sin código. Las líneas verticales marcan el límite de "
               "Shannon de cada tasa: ningún código puede funcionar a la izquierda de su línea.")
    if st.button("Calcular curvas", key="sim_curvas"):
        ruta_json = SALIDA / f"curvas_{Path(res['archivo']).stem}.json"
        with st.spinner("Enviando bloques con cada SNR..."):
            rc, out, err = ejecutar(["--curvas", "--escenario", e["clave"], "--snr", repr(float(k["snr_db"])),
                                     "--fec", especificacion_fec, "--json", str(ruta_json)])
        if rc != 0 or not ruta_json.exists():
            st.error("No se pudieron calcular las curvas")
            st.code(err or out)
        else:
            st.session_state["curvas_sim"] = json.loads(ruta_json.read_text(encoding="utf-8"))
    cv = st.session_state.get("curvas_sim")
    if cv:
        nombres = {c["clave"]: c["nombre"] for c in cv["curvas"]}
        piso = 0.5 / cv["bloques_max"]  # los puntos sin errores se dibujan en el piso del eje
        pts = pd.DataFrame([p for p in cv["puntos"] if p["simulado"]])
        pts["Curva"] = pts["curva"].map(nombres)
        pts["y"] = pts["bler"].clip(lower=piso)
        pts["Lectura"] = [f"{b:.3f}" if b > 0 else f"0 (menos de 1 en {n})" for b, n in zip(pts["bler"], pts["bloques"])]
        dominio = [nombres[c] for c in CAMINOS if c in nombres]
        rango = [COLORES_CAMINO[i] for i, c in enumerate(CAMINOS) if c in nombres]
        color = alt.Color("Curva:N", scale=alt.Scale(domain=dominio, range=rango), legend=alt.Legend(orient="top", title=None))
        forma = alt.Shape("Curva:N", scale=alt.Scale(domain=dominio, range=["circle", "diamond", "square", "triangle-up"][:len(dominio)]),
                          legend=None)
        lineas = alt.Chart(pts).mark_line(strokeWidth=2).encode(
            x=alt.X("snr_db:Q", title="SNR (dB)", axis=alt.Axis(grid=True, gridOpacity=0.3)),
            y=alt.Y("y:Q", title="Fracción de bloques con error (BLER)", scale=alt.Scale(type="log", domain=[piso, 1], nice=False),
                    axis=alt.Axis(grid=True, gridOpacity=0.3, values=[v for v in (1, 0.1, 0.01, 0.001) if v >= piso],
                                  format="~g")),
            color=color, strokeDash=alt.StrokeDash("Curva:N", legend=None,
                                                   scale=alt.Scale(domain=dominio, range=[[1, 0], [6, 3], [1, 0], [2, 2]][:len(dominio)])))
        aviso = ["Curva:N", alt.Tooltip("snr_db:Q", title="SNR (dB)"), alt.Tooltip("Lectura:N", title="BLER"),
                 alt.Tooltip("bloques:Q", title="Bloques enviados")]
        marcas = (alt.Chart(pts[pts["bler"] > 0]).mark_point(size=50, filled=True).encode(
                      x="snr_db:Q", y="y:Q", color=color, shape=forma, tooltip=aviso)
                  + alt.Chart(pts[pts["bler"] == 0]).mark_point(size=50, filled=False, strokeWidth=1.5).encode(
                      x="snr_db:Q", y="y:Q", color=color, shape=forma, tooltip=aviso))
        lim = pd.DataFrame([{"Curva": c["nombre"], "x": c["limite_shannon_db"]} for c in cv["curvas"]])
        reglas = alt.Chart(lim).mark_rule(strokeDash=[2, 3], opacity=0.7).encode(
            x="x:Q", color=color, tooltip=["Curva:N", alt.Tooltip("x:Q", title="Límite de Shannon (dB)", format=".2f")])
        st.altair_chart((lineas + marcas + reglas).properties(height=300), width="stretch")
        st.caption(f"{MODULACION[cv['qm']]}, hasta {cv['bloques_max']} bloques por punto. Los puntos huecos no tuvieron errores. "
                   "La distancia horizontal entre dos curvas a la misma altura es cuánta SNR ahorra un código frente al otro "
                   "(ganancia de codificación).")

    b1, b2, b3 = st.columns(3)
    for col, c in zip((b1, b2, b3), caminos):
        with col:
            if c["archivo"]:
                boton_descarga(c["archivo"], f"Descargar recuperado ({c['nombre']})", f"sim_{c['clave']}_{Path(res['archivo']).name}",
                               f"dl_sim_{c['clave']}")
    st.caption("Supuestos: canal con ruido gaussiano (AWGN) y una sola antena; el receptor no tiene que estimar el canal; sin "
               "cabeceras de protocolo; la red reparte el tiempo entre bajada y subida (TDD) y repite un bloque 4 envíos después. "
               "El escenario 6G usa valores candidatos porque 6G todavía no tiene norma.")


# ---------------------------------------------------------------- interfaz

LIMITE_AUTOMATICO = 32 * 1024 * 1024  # hasta este tamaño el archivo se procesa solo al cambiar algo

st.title("Codificación, compresión y reconstrucción adaptable 5G/6G")
st.caption("Fuente → Codificación → Compresión → Control de error → Canal → Reconstrucción · símbolos de N bits (8 a 32768)")

if not compilar_si_hace_falta():
    st.stop()

DATA.mkdir(exist_ok=True)
with st.sidebar:
    st.header("Archivo")
    origen = st.radio("Origen del archivo", ["Archivos de data/", "Subir archivo"], horizontal=True, label_visibility="collapsed")
    ruta_fuente = None
    if origen == "Subir archivo":
        subido = st.file_uploader("Audio, video, texto, imagen o cualquier archivo")
        if subido is not None:
            ruta_fuente = DATA / Path(subido.name).name
            if not ruta_fuente.exists() or ruta_fuente.stat().st_size != subido.size:
                ruta_fuente.write_bytes(subido.getbuffer())
    else:
        disponibles = sorted(p for p in DATA.rglob("*") if p.is_file() and p.suffix != ".ti6g" and SALIDA not in p.parents)
        if disponibles:
            ruta_fuente = st.selectbox("Archivo", disponibles,
                                       format_func=lambda p: f"{p.relative_to(DATA).as_posix()}  ({tam(p.stat().st_size)})")
        else:
            st.info("No hay archivos en data/")

    st.header("Codificación")
    n_bits = st.selectbox("Tamaño de símbolo N (bits)", VALORES_N, index=0,
                          help="El archivo se divide en bloques de N bits; cada bloque distinto es un símbolo de la fuente.")
    metodo = st.selectbox("Método", list(METODOS), format_func=METODOS.get,
                          help="Adaptable prueba Uniforme, Huffman y LZW y guarda el que deja el archivo más pequeño.")

    st.header("Canal")
    p_error = st.number_input("Probabilidad de error de bit p", min_value=0.0, max_value=0.5, value=0.0, step=0.0001,
                              format="%.4f", help="Canal binario simétrico: cada bit se invierte con probabilidad p. 0 = canal ideal.")

    st.header("Control de error")
    forma_fec = st.radio("Código de bloque", ["n y k", "Matriz P", "Ninguno"], horizontal=True,
                         help="Se agregan n − k bits de paridad a cada mensaje de k bits del flujo comprimido. "
                              "Ninguno omite esta etapa.")
    if forma_fec == "n y k":
        st.session_state.setdefault("fec_n", 7)
        st.session_state.setdefault("fec_k", 4)
        col_n, col_k = st.columns(2)
        n_fec = int(col_n.number_input("n", min_value=2, max_value=24, step=1, key="fec_n",
                                       help="Bits de cada palabra codificada (2 a 24)"))
        k_min, k_max = max(1, n_fec - 16), min(20, n_fec - 1)
        st.session_state["fec_k"] = min(max(int(st.session_state["fec_k"]), k_min), k_max)
        k_fec = int(col_k.number_input("k", min_value=k_min, max_value=k_max, step=1, key="fec_k",
                                       help=f"Bits de mensaje por palabra ({k_min} a {k_max} con n = {n_fec})"))
        especificacion_fec = f"{n_fec},{k_fec}"
        r_fec = n_fec - k_fec
        st.caption(f"{r_fec} bit{'s' if r_fec != 1 else ''} de paridad · tasa R = {k_fec / n_fec:.3f}. El programa elige la matriz P de "
                   "mayor distancia mínima.")
    elif forma_fec == "Matriz P":
        filas_p = st.text_input("Filas de P", "110,101,011,111",
                                help="k filas de n − k bits separadas por coma. G = [P | I_k] y H = [I_(n−k) | Pᵀ]. "
                                     "El valor inicial es un código de Hamming (7,4).")
        especificacion_fec = "P:" + filas_p.replace(" ", "")
    else:
        especificacion_fec = "ninguno"
    arq_max = st.number_input("ARQ: envíos máximos por bloque", min_value=1, max_value=64, value=8,
                              help="Si después de estos envíos el síndrome sigue sin ser cero, el bloque se entrega como llegó.")

    with st.expander("Opciones de visualización"):
        pasos = st.slider("Pasos de LZW a mostrar", 20, 1000, 200, step=20)


def procesar(ruta, parametros):
    salida = SALIDA / f"{ruta.stem}_N{n_bits}_{metodo}"
    ruta_json = salida / "resultados.json"
    salida.mkdir(parents=True, exist_ok=True)
    with st.spinner(f"Procesando {ruta.name} con N = {n_bits} bits..."):
        rc, out, err = ejecutar([str(ruta), str(n_bits), "--metodo", metodo, "--p", repr(float(p_error)),
                                 "--fec", especificacion_fec, "--arq-max", str(int(arq_max)),
                                 "--traza", str(pasos), "--salida", str(salida), "--json", str(ruta_json)])
    if rc not in (0, 3) or not ruta_json.exists():
        st.session_state["resultado"] = {"parametros": parametros, "error": err or out or "Sin mensaje de error"}
    else:
        st.session_state["resultado"] = {"parametros": parametros, "datos": json.loads(ruta_json.read_text(encoding="utf-8")),
                                         "consola": out + err, "archivo": ruta.relative_to(DATA).as_posix()}


if ruta_fuente is None:
    st.info("Elige un archivo en la barra lateral para empezar.")
    st.stop()

estado = ruta_fuente.stat()
if estado.st_size == 0:
    st.error(f"**{ruta_fuente.name}** está vacío: no hay símbolos que codificar.")
    st.stop()

# Los resultados se recalculan cuando cambia el archivo o cualquier parámetro, así lo que se ve siempre
# corresponde a lo elegido en la barra lateral
parametros = (str(ruta_fuente), estado.st_size, estado.st_mtime_ns, n_bits, metodo, float(p_error), especificacion_fec,
              int(arq_max), pasos)
res = st.session_state.get("resultado")
if not res or res["parametros"] != parametros:
    if estado.st_size <= LIMITE_AUTOMATICO or st.sidebar.button("Procesar archivo", type="primary", width="stretch"):
        procesar(ruta_fuente, parametros)
        res = st.session_state["resultado"]
    else:
        st.warning(f"**{ruta_fuente.name}** pesa {tam(estado.st_size)}. Pulsa **Procesar archivo** en la barra lateral.")
        st.stop()
if "error" in res:
    st.error("Ocurrió un error en el núcleo C++")
    st.code(res["error"])
    st.stop()

d = res["datos"]
f, c, comp, rec, k = d["fuente"], d["codificacion"], d["compresion"], d["reconstruccion"], d["canal"]
metodo_elegido = c["metodo_elegido"]

st.success(f"**{res['archivo']}** · {f['tipo']} ({f['formato']}) · N = {f['n_bits']} bits · codificación **{metodo_elegido}**"
           f"{' (adaptable)' if c['adaptable'] else ''} · reconstrucción "
           f"{'idéntica' if rec['identico'] else 'DIFERENTE'}")

m1, m2, m3, m4, m5 = st.columns(5)
m1.metric("Entropía H", f"{f['entropia']:.4f} bits", help="bits/símbolo")
m2.metric("Divergencia KL", f"{f['kl_uniforme_n']:.4f} bits", help="D(P‖U) respecto a la uniforme sobre 2^N símbolos")
var_elegida = c[metodo_elegido.lower()]["varianza"]
m3.metric("Varianza σ²", f"{var_elegida:.4f} bits²", help=f"Varianza de la codificación {metodo_elegido}")
m4.metric("I(X;Y) por símbolo", f"{k['simbolos_IXY']:.4f} bits", help="Información mutua fuente ↔ reconstrucción tras el canal")
m5.metric("Comprimido", tam(comp["bytes"]), delta=f"{-100 * comp['ahorro']:+.1f}%", delta_color="inverse",
          help=f"Original: {tam(f['tamano_original'])}")

tabs = st.tabs(["1 · Fuente", "2 · Codificación", "3 · Compresión", "4 · Reconstrucción", "5 · Canal e I(X;Y)",
                "6 · Control de error", "7 · Comparar N", "8 · Agente IA", "9 · 5G / 6G", "10 · Simulación 5G/6G", "Consola"])

# ---- 1. Fuente
with tabs[0]:
    col_a, col_b = st.columns([1, 1])
    with col_a:
        st.subheader("Modelo estadístico de la fuente")
        tabla_medidas([
            ("Tipo de fuente", f"{f['tipo']} ({f['formato']})"),
            ("Tamaño", tam(f["tamano_original"])),
            ("N (bits/símbolo)", f"{f['n_bits']} ({f['bytes_por_simbolo']} bytes)"),
            ("Símbolos en la secuencia (n)", f"{f['simbolos']:,}"),
            ("Símbolos distintos (M)", f"{f['alfabeto']:,}"),
            ("Bits de relleno del último bloque", f"{f['relleno_bits']:,}"),
            ("Entropía H(X)", f"{f['entropia']:.6f} bits/símbolo"),
            ("Entropía máxima log₂(2^N)", f"{f['entropia_maxima']:.0f} bits"),
            ("Entropía por bit H/N", f"{f['entropia_por_bit']:.6f}"),
            ("Redundancia 1 − H/N", f"{100 * f['redundancia']:.4f}%"),
            ("D_KL(P‖U uniforme sobre 2^N)", f"{f['kl_uniforme_n']:.6f} bits"),
            ("D_KL(P‖U uniforme sobre M)", f"{f['kl_uniforme_m']:.6f} bits"),
            ("Información total n·H", f"{f['info_total_bits']:,.0f} bits"),
        ])
    with col_b:
        st.subheader("Fórmulas")
        st.latex(r"I(x_k) = -\log_2 P(x_k)")
        st.latex(r"H(X) = -\sum_k P(x_k)\log_2 P(x_k)")
        st.latex(r"D_{KL}(P\|U) = \sum_k P(x_k)\log_2\frac{P(x_k)}{U(x_k)} = N - H(X)")

    st.subheader("Símbolos más frecuentes y su cantidad de información")
    top = pd.DataFrame(d["top_simbolos"])
    if not top.empty:
        top["símbolo"] = top["hex"]
        grafico_barras(top, "símbolo", "p", "Símbolo (hex)", "P(x)",
                       [alt.Tooltip("hex:N", title="Símbolo"), alt.Tooltip("ascii:N", title="ASCII"),
                        alt.Tooltip("p:Q", format=".6f", title="P(x)"), alt.Tooltip("informacion:Q", format=".4f", title="I(x) bits")],
                       orden_x="-y")
        st.dataframe(top[["hex", "ascii", "frecuencia", "p", "informacion", "long_huffman", "codigo_huffman"]].rename(columns={
            "hex": "Símbolo (hex)", "ascii": "ASCII", "frecuencia": "Frecuencia", "p": "P(x)", "informacion": "I(x) [bits]",
            "long_huffman": "L_k Huffman", "codigo_huffman": "Código Huffman"}), hide_index=True)

# ---- 2. Codificación
with tabs[1]:
    st.subheader("Codificación uniforme vs no uniforme vs adaptable")
    filas = [("Sin codificar (N bits)", c["sin_codificar"]["longitud_media"], 0.0, c["sin_codificar"]["eficiencia"], None, None, None)]
    for clave, nombre in [("uniforme", "Uniforme"), ("huffman", "Huffman (mínima varianza)"), ("lzw", "LZW (adaptable)")]:
        x = c[clave]
        filas.append((nombre, x["longitud_media"], x["varianza"], x["eficiencia"], x.get("kraft"),
                      x.get("longitud_min"), x.get("longitud_max")))
    filas.insert(3, ("Huffman (empates abajo)", c["huffman"]["longitud_media"], c["huffman"]["varianza_empates_abajo"],
                     c["huffman"]["eficiencia"], 1.0, c["huffman"]["longitud_min"], c["huffman"]["longitud_max_empates_abajo"]))
    tabla_codigos = pd.DataFrame(filas, columns=["Código", "L̄ [bits/símbolo]", "σ² [bits²]", "Eficiencia H/L̄", "Kraft Σ2^-L",
                                                 "L mín", "L máx"])
    formatos = {"L̄ [bits/símbolo]": "{:.4f}", "σ² [bits²]": "{:.4f}", "Eficiencia H/L̄": "{:.2%}", "Kraft Σ2^-L": "{:.4f}",
                "L mín": "{:.0f}", "L máx": "{:.0f}"}
    for col, fmt in formatos.items():  # "—" donde la medida no aplica; σ² = 0 sin ruido de punto flotante
        tabla_codigos[col] = [("—" if pd.isna(v) else fmt.format(abs(v) if abs(v) < 1e-12 else v)) for v in tabla_codigos[col]]
    st.dataframe(tabla_codigos, hide_index=True)

    col_a, col_b = st.columns([1, 1])
    with col_a:
        st.markdown("**Varianza en la codificación**")
        st.latex(r"\bar{L} = \sum_{k=0}^{K-1} P_k L_k \qquad \sigma^2 = \sum_{k=0}^{K-1} P_k\,(L_k - \bar{L})^2")
        st.caption("Para LZW, L_k es la cantidad de bits que le corresponde a cada símbolo de la fuente "
                   "(bits del código emitido ÷ símbolos que cubre la frase).")
    with col_b:
        st.markdown("**Distribución de longitudes del código de Huffman**")
        lh = pd.DataFrame(d["longitudes_huffman"])
        if not lh.empty:
            grafico_barras(lh, "longitud", "probabilidad", "Longitud L_k (bits)", "Σ P_k de esa longitud",
                           [alt.Tooltip("longitud:Q", title="L_k"), alt.Tooltip("simbolos:Q", title="Símbolos"),
                            alt.Tooltip("probabilidad:Q", format=".4f", title="Σ P_k")])
            st.dataframe(lh.rename(columns={"longitud": "L_k", "simbolos": "Símbolos", "probabilidad": "Σ P_k"}), hide_index=True)

    st.subheader("Secuencia codificada (primeros símbolos)")
    muestra = pd.DataFrame(d["muestra"])
    if not muestra.empty:
        st.dataframe(muestra[["i", "hex", "ascii", "p", "informacion", "uniforme", "huffman"]].rename(columns={
            "i": "#", "hex": "Símbolo (hex)", "ascii": "ASCII", "p": "P(x)", "informacion": "I(x)",
            "uniforme": "Código uniforme", "huffman": "Código Huffman"}), hide_index=True)
        st.code("Uniforme: " + " ".join(muestra["uniforme"]) + "\nHuffman:  " + " ".join(muestra["huffman"]), language="text",
                wrap_lines=True)

# ---- 3. Compresión
with tabs[2]:
    st.subheader("Eliminación de redundancia")
    st.dataframe(pd.DataFrame([
        (nombre, tam(c[clave]["bytes_cabecera"]), tam(c[clave]["bytes_carga"]), tam(c[clave]["bytes_total"]), c[clave]["razon"],
         "Sí" if nombre == metodo_elegido else "")
        for clave, nombre in [("uniforme", "Uniforme"), ("huffman", "Huffman"), ("lzw", "LZW")]
    ], columns=["Método", "Cabecera", "Datos", "Total", "Razón original/comprimido", "Elegido"]), hide_index=True,
        column_config={"Razón original/comprimido": st.column_config.NumberColumn(format="%.4f")})

    st.markdown(f"**Flujo comprimido ({metodo_elegido}), primeros 256 bits**")
    st.code(comp["flujo_bits"] or "(vacío)", language="text", wrap_lines=True)

    st.subheader("Proceso iterativo del diccionario LZW")
    st.caption(f"Primeros {len(d['traza_lzw'])} pasos de {c['lzw']['codigos_emitidos']:,} códigos emitidos. "
               f"El diccionario empieza con los M = {f['alfabeto']:,} símbolos de la fuente y crece hasta "
               f"{c['lzw']['diccionario_max']:,} entradas; el ancho de cada código crece con él.")
    traza = pd.DataFrame(d["traza_lzw"])
    if not traza.empty:
        st.dataframe(traza[["paso", "posicion", "patron", "patron_ascii", "largo", "siguiente", "codigo", "ancho", "nueva_entrada"]]
                     .rename(columns={"paso": "Paso", "posicion": "Posición", "patron": "Patrón (hex)", "patron_ascii": "Patrón (ASCII)",
                                      "largo": "Símbolos", "siguiente": "Siguiente", "codigo": "Código emitido",
                                      "ancho": "Bits", "nueva_entrada": "Nueva entrada"}),
                     hide_index=True, height=420)

# ---- 4. Reconstrucción
with tabs[3]:
    st.subheader("Decodificación, descompresión y reconstrucción")
    st.markdown("El archivo se reconstruye **leyendo solamente `comprimido.ti6g`** (cabecera + alfabeto + tabla + datos) "
                "y luego se compara byte a byte con el original.")
    r1, r2, r3 = st.columns(3)
    r1.metric("Bytes reconstruidos", f"{rec['bytes']:,}")
    r2.metric("Bytes originales", f"{f['tamano_original']:,}")
    r3.metric("Resultado", "Idéntico" if rec["identico"] else "Diferente")
    if not muestra.empty:
        st.dataframe(muestra[["i", "hex", "reconstruido"]].assign(igual=lambda m: m["hex"] == m["reconstruido"]).rename(columns={
            "i": "#", "hex": "Fuente (hex)", "reconstruido": "Reconstruido (hex)", "igual": "Igual"}), hide_index=True)
    st.markdown("**Archivos de salida**")
    b1, b2 = st.columns(2)
    with b1:
        boton_descarga(comp["archivo"], "Descargar comprimido (.ti6g)", f"{Path(res['archivo']).stem}.ti6g", "dl_comp")
    with b2:
        boton_descarga(rec["archivo"], "Descargar reconstruido", f"reconstruido_{Path(res['archivo']).name}", "dl_rec")

# ---- 5. Canal
with tabs[4]:
    st.subheader("Canal discreto sin memoria: canal binario simétrico (BSC)")
    k1, k2, k3, k4 = st.columns(4)
    k1.metric("Probabilidad de error p", f"{k['p']:g}")
    k2.metric("Errores de bit medidos", f"{k['errores_0a1'] + k['errores_1a0']:,}", help=f"Tasa medida: {k['tasa_error_bit']:.3e}")
    k3.metric("I(X;Y) por bit", f"{k['IXY_medida']:.4f} bits", help=f"Teórica: {k['IXY']:.6f} bits")
    k4.metric("Capacidad C = 1 − H_b(p)", f"{k['capacidad']:.4f} bits/uso")
    if k["p"] == 0:
        st.caption("Con p = 0 el canal es ideal. Cambia la probabilidad de error en la barra lateral para ver el ruido.")
    col_a, col_b = st.columns([1, 1])
    with col_a:
        st.latex(r"P = \begin{bmatrix} P(y_0|x_0) & P(y_1|x_0) \\ P(y_0|x_1) & P(y_1|x_1) \end{bmatrix}"
                 r"= \begin{bmatrix} 1-p & p \\ p & 1-p \end{bmatrix}")
        st.latex(r"P(x_j, y_k) = P(y_k|x_j)\,P(x_j) \qquad P(y_k) = \sum_j P(y_k|x_j)\,P(x_j)")
        st.latex(r"I(X;Y) = H(Y) - H(Y|X) \qquad C = 1 - H_b(p)")
    with col_b:
        etiquetas = ["x₀ = 0", "x₁ = 1"]
        st.markdown("**Matriz de transición teórica P(y|x)**")
        st.dataframe(pd.DataFrame(k["transicion"], index=etiquetas, columns=["y₀ = 0", "y₁ = 1"]))
        st.markdown("**Matriz de transición medida**")
        st.dataframe(pd.DataFrame(k["transicion_medida"], index=etiquetas, columns=["y₀ = 0", "y₁ = 1"]).style.format("{:.6f}"))
        st.markdown("**Distribución conjunta P(x, y)**")
        st.dataframe(pd.DataFrame(k["conjunta"], index=etiquetas, columns=["y₀ = 0", "y₁ = 1"]).style.format("{:.6f}"))

    st.markdown("**Medidas por bit del flujo codificado**")
    tabla_medidas([
        ("p (probabilidad de error)", f"{k['p']:g}"),
        ("Bits transmitidos", f"{k['bits']:,}"),
        ("P(x₀), P(x₁)", f"{k['px0']:.6f}, {k['px1']:.6f}"),
        ("P(y₀), P(y₁)", f"{k['py0']:.6f}, {k['py1']:.6f}"),
        ("Errores 0→1 / 1→0", f"{k['errores_0a1']:,} / {k['errores_1a0']:,}"),
        ("Tasa de error de bit medida", f"{k['tasa_error_bit']:.3e}"),
        ("H(X)", f"{k['HX']:.6f} bits"), ("H(Y)", f"{k['HY']:.6f} bits"),
        ("H(Y|X) = H_b(p)", f"{k['HY_dado_X']:.6f} bits"), ("H(X|Y) (equivocación)", f"{k['HX_dado_Y']:.6f} bits"),
        ("I(X;Y) teórica", f"{k['IXY']:.6f} bits"), ("I(X;Y) medida", f"{k['IXY_medida']:.6f} bits"),
        ("Capacidad C", f"{k['capacidad']:.6f} bits/uso"),
    ])

    st.markdown("**Medidas por símbolo: fuente X ↔ reconstrucción Y**")
    st.latex(r"I(X;Y) = H(X) + H(Y) - H(X,Y)")
    s1, s2, s3, s4 = st.columns(4)
    s1.metric("H(X)", f"{k['simbolos_HX']:.4f}")
    s2.metric("H(Y)", f"{k['simbolos_HY']:.4f}")
    s3.metric("I(X;Y)", f"{k['simbolos_IXY']:.4f}")
    s4.metric("Símbolos errados", f"{100 * k['tasa_error_simbolo']:.2f}%")
    if k["p"] > 0:
        st.markdown("**Flujo enviado vs recibido** (los `^` marcan bits invertidos por el canal)")
        st.code(f"X: {comp['flujo_bits']}\nY: {k['flujo_recibido']}\n   {marcar_diferencias(comp['flujo_bits'], k['flujo_recibido'])}",
                language="text")
        if not muestra.empty and "canal" in muestra:
            st.dataframe(muestra[["i", "hex", "canal"]].rename(columns={"i": "#", "hex": "Enviado (hex)", "canal": "Recibido (hex)"}),
                         hide_index=True)
        boton_descarga(k.get("archivo"), "Descargar archivo recibido con ruido", f"canal_{Path(res['archivo']).name}", "dl_canal")

    st.divider()
    calculadora_canal_discreto()

# ---- 6. Control de error
with tabs[5]:
    seccion_control_error(d, res)

# ---- 7. Comparar N
with tabs[6]:
    st.subheader("Adaptabilidad: el mismo archivo con distintos N")
    seleccion = st.multiselect("Valores de N", VALORES_N, default=VALORES_N)
    if st.button("Comparar", type="primary", disabled=not seleccion):
        ruta_json = SALIDA / f"barrido_{Path(res['archivo']).stem}.json"
        SALIDA.mkdir(parents=True, exist_ok=True)
        with st.spinner("Analizando todos los N (puede tardar con archivos grandes)..."):
            rc, out, err = ejecutar([str(DATA / res["archivo"]) if (DATA / res["archivo"]).exists() else f["ruta"],
                                     "--barrido", ",".join(map(str, sorted(seleccion))), "--json", str(ruta_json)])
        if rc != 0:
            st.error(err or out)
        else:
            st.session_state["barrido"] = {"archivo": res["archivo"],
                                           "filas": json.loads(ruta_json.read_text(encoding="utf-8"))["filas"]}
    barrido = st.session_state.get("barrido")
    if barrido and barrido["archivo"] == res["archivo"]:
        df = pd.DataFrame(barrido["filas"])
        mejor = df.loc[df["razon_mejor"].idxmax()]
        st.success(f"Mejor N: **{int(mejor['n_bits'])} bits** con **{mejor['mejor']}** (razón {mejor['razon_mejor']:.4f}:1)")
        g1, g2 = st.columns(2)
        with g1:
            st.markdown("**Entropía por bit H/N**")
            grafico_lineas(df, "entropia_por_bit", "H/N")
            st.markdown("**Varianza de Huffman σ²**")
            grafico_lineas(df, "huffman_varianza", "σ² (bits²)")
        with g2:
            st.markdown("**Símbolos distintos M**")
            grafico_lineas(df, "alfabeto", "M", formato=",.0f")
            st.markdown("**Razón de compresión del mejor método**")
            grafico_lineas(df, "razon_mejor", "original / comprimido")
        st.dataframe(df[["n_bits", "simbolos", "alfabeto", "entropia", "entropia_por_bit", "kl_uniforme_n", "huffman_longitud_media",
                         "huffman_varianza", "huffman_varianza_max", "huffman_eficiencia", "lzw_bits_por_simbolo",
                         "total_uniforme", "total_huffman", "total_lzw", "mejor", "razon_mejor", "tiempo_ms"]].rename(columns={
            "n_bits": "N", "simbolos": "n", "alfabeto": "M", "entropia": "H", "entropia_por_bit": "H/N", "kl_uniforme_n": "D_KL",
            "huffman_longitud_media": "L̄ Huffman", "huffman_varianza": "σ² Huffman", "huffman_varianza_max": "σ² empates abajo",
            "huffman_eficiencia": "η Huffman", "lzw_bits_por_simbolo": "LZW bits/símbolo", "total_uniforme": "Bytes uniforme",
            "total_huffman": "Bytes Huffman", "total_lzw": "Bytes LZW", "mejor": "Mejor", "razon_mejor": "Razón",
            "tiempo_ms": "Tiempo (ms)"}), hide_index=True)

# ---- 8. Agente de IA
with tabs[7]:
    seccion_agente(res)

# ---- 9. Enlace 5G / 6G
with tabs[8]:
    seccion_enlace(res, d)

# ---- 10. Simulación 5G / 6G
with tabs[9]:
    seccion_simulacion(res, d)

# ---- Consola
with tabs[10]:
    st.code(res["consola"][-20000:] or "(sin salida)", language="text")
    st.caption("Tiempos (ms): " + ", ".join(f"{a} {b:,.0f}" for a, b in d["tiempos_ms"].items()))
