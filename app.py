import streamlit as st
import subprocess
import os
import re

st.set_page_config(page_title="Codificador 5G/6G - LZW", layout="centered")

st.title("📡 Sistema de Codificación 5G/6G")
st.markdown("Sube un archivo para analizar su entropía, comprimirlo usando LZW y verificar su reconstrucción.")

# 1. Controles de la Interfaz
archivo_subido = st.file_uploader("Selecciona un archivo multimedia o de texto")
n_bits = st.selectbox("Tamaño de bloque (N bits)", [8, 64, 128, 512, 1024, 2048, 4096, 8192])

if st.button("Procesar Archivo"):
    if archivo_subido is not None:
        # Asegurar que exista la carpeta data/
        os.makedirs("data", exist_ok=True)
        ruta_guardado = os.path.join("data", archivo_subido.name)
        
        with open(ruta_guardado, "wb") as f:
            f.write(archivo_subido.getbuffer())
        
        st.info(f"Procesando {archivo_subido.name} con bloques de {n_bits} bits...")

        # 2. Conectar la Interfaz con el código C++ de forma segura
        ruta_ejecutable = os.path.join(os.getcwd(), "codificador_6g.exe")

        # Validar si el archivo ejecutable realmente existe antes de correrlo
        if not os.path.exists(ruta_ejecutable):
            st.error(f"No se encontró el archivo ejecutable en: {ruta_ejecutable}. Asegúrate de compilarlo primero en la terminal.")
        else:
            comando = [ruta_ejecutable, ruta_guardado, str(n_bits)]
            
            # Ejecutamos con text=False para capturar los bytes en crudo
            resultado = subprocess.run(comando, capture_output=True, text=False)

            # Decodificamos de forma segura ignorando caracteres binarios conflictivos
            stdout_texto = resultado.stdout.decode('utf-8', errors='ignore') if resultado.stdout else ""
            stderr_texto = resultado.stderr.decode('utf-8', errors='ignore') if resultado.stderr else ""

            # 3. Mostrar los resultados de forma estructurada y limpia
            if resultado.returncode == 0:
                st.success("¡Procesamiento y compresión exitosos!")
                
                # Organizar la salida visualmente en pestañas
                tab1, tab2 = st.tabs(["📊 Métricas y Resumen", "🔍 Traza Paso a Paso (LZW)"])
                
                with tab1:
                    st.subheader("📊 Métricas Principales del Sistema")
                    
                    # Extraer valores numéricos de la salida de C++ mediante expresiones regulares
                    match_h = re.search(r"Entropia \(H\):\s*([0-9.]+)", stdout_texto)
                    match_kl = re.search(r"Divergencia KL:\s*([0-9.]+)", stdout_texto)
                    match_var = re.search(r"Varianza de Codificacion \(sigma\^2\):\s*([0-9.e+]+)", stdout_texto)
                    
                    val_h = match_h.group(1) if match_h else "N/A"
                    val_kl = match_kl.group(1) if match_kl else "N/A"
                    
                    # Formato compacto para evitar que el número largo se corte en la tarjeta
                    if match_var:
                        try:
                            val_var_float = float(match_var.group(1))
                            if val_var_float >= 1e6:
                                val_var_fmt = f"{val_var_float/1e6:.2f} M"
                            elif val_var_float >= 1e3:
                                val_var_fmt = f"{val_var_float/1e3:.1f} k"
                            else:
                                val_var_fmt = f"{val_var_float:.1f}"
                        except:
                            val_var_fmt = match_var.group(1)
                    else:
                        val_var_fmt = "N/A"

                    # Mostrar métricas en columnas con tarjetas visuales de Streamlit
                    col_m1, col_m2, col_m3 = st.columns(3)
                    col_m1.metric("Entropía (H)", f"{val_h} bits")
                    col_m2.metric("Divergencia KL", f"{val_kl}")
                    col_m3.metric("Varianza (σ²)", val_var_fmt, "bits²")

                    st.markdown("---")
                    
                    # Mostrar resumen de compresión
                    if os.path.exists("data/comprimido.bin"):
                        tam_comp = os.path.getsize("data/comprimido.bin")
                        tam_orig = len(archivo_subido.getvalue())
                        
                        col_c1, col_c2 = st.columns(2)
                        col_c1.metric("Tamaño Original", f"{tam_orig} bytes")
                        
                        porcentaje_reduccion = f"-{((1 - tam_comp/tam_orig)*100):.1f}%" if tam_orig > 0 else None
                        col_c2.metric("Tamaño Comprimido", f"{tam_comp} bytes", 
                                      delta=porcentaje_reduccion,
                                      delta_color="inverse")

                    # Consola completa oculta en un desplegable para mantener la interfaz limpia
                    with st.expander("Ver salida completa del núcleo C++"):
                        st.text_area("Consola", stdout_texto, height=200, label_visibility="collapsed")
                
                with tab2:
                    st.subheader("Proceso iterativo del diccionario LZW")
                    st.markdown("Registro paso a paso de cómo el compresor detectó patrones y generó códigos dinámicos:")
                    
                    if stdout_texto:
                        lineas_consola = stdout_texto.split("\n")
                        pasos_lzw = [linea for linea in lineas_consola if "[LZW Paso]" in linea]
                        
                        if pasos_lzw:
                            st.code("\n".join(pasos_lzw), language="text")
                        else:
                            st.info("No se encontraron registros '[LZW Paso]'. Asegúrate de haber vuelto a compilar tu código en C++.")
                    else:
                        st.warning("El núcleo de C++ no devolvió ninguna salida estándar.")
                
                # 4. Botones de descarga de archivos
                st.markdown("---")
                st.subheader("Archivos de Salida")
                col1, col2 = st.columns(2)
                
                if os.path.exists("data/comprimido.bin"):
                    with open("data/comprimido.bin", "rb") as file_comp:
                        col1.download_button("📥 Descargar Comprimido (.bin)", file_comp, file_name="comprimido.bin")
                
                if os.path.exists("data/reconstruido.bin"):
                    with open("data/reconstruido.bin", "rb") as file_rec:
                        col2.download_button("🔄 Descargar Reconstruido", file_rec, file_name=f"reconstruido_{archivo_subido.name}")
            else:
                st.error("Ocurrió un error en el núcleo de C++.")
                st.error(stderr_texto if stderr_texto else "Error desconocido sin mensaje de error estándar.")
    else:
        st.warning("Por favor, sube un archivo primero.")