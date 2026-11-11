import json
import shutil
import subprocess
from pathlib import Path

import altair as alt
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
    textos = base.mark_text(align="left", dx=4, fontSize=11, color="#9aa0a6").encode(text="Texto:N")
    st.altair_chart((barras + textos).properties(height=alt.Step(18)), width="stretch")

    st.markdown("**Qué pide cada estándar**")
    st.dataframe(ESTANDARES, hide_index=True)
    st.caption("Fuentes: UIT, IMT-2020 para 5G (ITU-R M.2410) e IMT-2030 para 6G (ITU-R M.2160). 6G todavía está en definición: "
               "sus valores son objetivos. Para 6G la gráfica usa el valor medio de cada rango.")


# ---------------------------------------------------------------- interfaz

LIMITE_AUTOMATICO = 32 * 1024 * 1024  # hasta este tamaño el archivo se procesa solo al cambiar algo

st.title("Codificación, compresión y reconstrucción adaptable 5G/6G")
st.caption("Fuente → Codificación → Compresión → Canal → Reconstrucción · símbolos de N bits (8 a 32768)")

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

    with st.expander("Opciones de visualización"):
        pasos = st.slider("Pasos de LZW a mostrar", 20, 1000, 200, step=20)


def procesar(ruta, parametros):
    salida = SALIDA / f"{ruta.stem}_N{n_bits}_{metodo}"
    ruta_json = salida / "resultados.json"
    salida.mkdir(parents=True, exist_ok=True)
    with st.spinner(f"Procesando {ruta.name} con N = {n_bits} bits..."):
        rc, out, err = ejecutar([str(ruta), str(n_bits), "--metodo", metodo, "--p", repr(float(p_error)),
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
parametros = (str(ruta_fuente), estado.st_size, estado.st_mtime_ns, n_bits, metodo, float(p_error), pasos)
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
                "6 · Comparar N", "7 · Agente IA", "8 · 5G / 6G", "Consola"])

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

# ---- 6. Comparar N
with tabs[5]:
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

# ---- 7. Agente de IA
with tabs[6]:
    seccion_agente(res)

# ---- 8. Enlace 5G / 6G
with tabs[7]:
    seccion_enlace(res, d)

# ---- Consola
with tabs[8]:
    st.code(res["consola"][-20000:] or "(sin salida)", language="text")
    st.caption("Tiempos (ms): " + ", ".join(f"{a} {b:,.0f}" for a, b in d["tiempos_ms"].items()))
