"""Pruebas locales con datos inventados: nunca contactan el servidor del curso."""
import csv
import io
import json
import math
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import tempfile
import threading
import unittest
from decimal import Decimal, ROUND_HALF_UP
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / ("estacion.exe" if os.name == "nt" else "estacion")
CAMPOS = ("validas", "descartadas", "temp_min", "temp_max", "temp_prom",
          "sobre_umbral")
UMBRAL = 74.23
TOKEN_PRUEBA = secrets.token_hex(16)


def crear_filas():
    filas = []
    for i in range(2000):
        fila = [1783 + 7 * i, "M" + str(i % 4 + 1),
                1770897180 + (i // 4) * 60,
                round(40 + ((i * 73) % 6000) / 100, 2),
                round(2 + (i % 300) / 100, 2),
                round((i % 190) / 100, 2), "OK"]
        if i % 47 == 0:
            fila[6] = "FALLA"
        elif i % 53 == 0:
            fila[3] = -999.0
        elif i % 59 == 0:
            fila[2] = None
        elif i % 61 == 0:
            fila[4] = None
        elif i % 67 == 0:
            fila[5] = None
        elif i % 71 == 0:
            fila[6] = None
        elif i % 73 == 0:
            fila[3] = None
        filas.append(fila)
    # Bordes: cero es un valor presente; igual al umbral no lo supera.
    for indice, temp in ((12, UMBRAL), (16, UMBRAL + .01),
                         (20, UMBRAL - .01), (24, 0.0), (28, -4.25)):
        filas[indice][3] = temp
    filas[32][4] = 0.0
    filas[36][5] = 0.0
    filas[40][6] = "ok"
    filas[44][6] = " OK "
    filas[48][6] = ""
    filas[52][3] = -999.0  # Invalida aunque el estado sea OK.
    filas[56][3], filas[56][6] = 999.0, "FALLA"
    return filas


def redondear(numero):
    return float(Decimal(str(numero)).quantize(
        Decimal("0.01"), rounding=ROUND_HALF_UP))


def referencia_python(filas, umbral):
    """Oraculo independiente: listas y Decimal, sin repetir el SQL de C."""
    salida = []
    for maquina in ("M1", "M2", "M3", "M4"):
        grupo = [f for f in filas if f[1] == maquina]
        validas = [f for f in grupo if all(v is not None and v != "" for v in f)
                   and f[6] == "OK" and f[3] != -999.0]
        temperaturas = [f[3] for f in validas]
        media = (sum(Decimal(str(t)) for t in temperaturas) /
                 Decimal(len(temperaturas))) if temperaturas else None
        salida.append(dict(
            maquina=maquina, validas=len(validas),
            descartadas=len(grupo) - len(validas),
            temp_min=redondear(min(temperaturas)) if temperaturas else None,
            temp_max=redondear(max(temperaturas)) if temperaturas else None,
            temp_prom=redondear(media) if media is not None else None,
            sobre_umbral=sum(t > umbral for t in temperaturas)))
    return salida


class Servidor(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def responder(self, codigo, texto):
        datos = texto.encode()
        self.send_response(codigo)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(datos)))
        self.end_headers()
        self.wfile.write(datos)

    def autorizado(self):
        valido = (self.headers.get("Authorization") ==
                  "Bearer " + TOKEN_PRUEBA and
                  self.headers.get("User-Agent") == "estacion-itla/1.0")
        if not valido:
            self.responder(401, "error=credenciales_de_prueba\n")
        return valido

    def do_GET(self):
        if not self.autorizado():
            return
        self.server.eventos.append(("GET", self.path, None))
        url = urlsplit(self.path)
        if url.path == "/api/v1/config":
            if self.server.modo == "config_401":
                self.responder(401, "error=no_autorizado\n")
                return
            self.responder(200, (
                "matricula=20250442\nhora=1795523281\n"
                "umbral_temp=74.23\nintervalo_s=5\nlatido_s=10\n"
                "maquinas=M1,M2,M3,M4\nlote_max=200\n"))
        elif url.path == "/api/v1/lecturas":
            parametros = parse_qs(url.query)
            desde = int(parametros["desde_id"][0])
            limite = int(parametros["limite"][0])
            anteriores = [f for f in self.server.filas if f[0] <= desde][-2:]
            nuevas = [f for f in self.server.filas if f[0] > desde][:limite]
            texto = io.StringIO()
            escritor = csv.writer(texto, lineterminator="\r\n")
            escritor.writerow(("id", "maquina", "ts", "temp", "corriente",
                               "vibracion", "estado"))
            escritor.writerows(anteriores + nuevas)
            self.responder(200, texto.getvalue())
        else:
            self.responder(404, "error=ruta_de_prueba_desconocida\n")

    def do_POST(self):
        if not self.autorizado():
            return
        texto = self.rfile.read(int(self.headers["Content-Length"]))
        cuerpo = json.loads(texto)
        self.server.eventos.append(("POST", self.path, cuerpo))
        if self.path == "/api/v1/respuestas/descarga":
            esperado = dict(filas_unicas=len(self.server.filas),
                            suma_ids=sum(f[0] for f in self.server.filas))
            self.responder(200, "correcto=" + str(int(cuerpo == esperado)) + "\n")
            return
        if self.path != "/api/v1/respuestas/analisis":
            self.responder(404, "error=ruta_de_prueba_desconocida\n")
            return
        if self.server.modo == "transporte":
            self.close_connection = True
            return
        if self.server.modo == "http_401":
            self.responder(401, "error=no_autorizado\n")
            return
        esperado = referencia_python(self.server.filas, self.server.umbral)
        valido = set(cuerpo) == {"maquinas"} and len(cuerpo["maquinas"]) == 4
        if valido:
            for recibido, referencia in zip(cuerpo["maquinas"], esperado):
                if set(recibido) != set(referencia):
                    valido = False
                    break
                for clave, valor in referencia.items():
                    if clave in ("temp_min", "temp_max", "temp_prom"):
                        if (not isinstance(recibido[clave], (int, float)) or
                            not math.isclose(recibido[clave], valor,
                                             abs_tol=.010001, rel_tol=0)):
                            valido = False
                    elif recibido[clave] != valor:
                        valido = False
        if not valido:
            self.server.errores.append(cuerpo)
            self.responder(400, "error=JSON_no_coincide_con_oraculo\n")
            return
        lineas = [f"M{i}.{campo}=ok" for i in range(1, 5) for campo in CAMPOS]
        modo = self.server.modo
        if modo == "campo_mal":
            lineas[4] = "M1.temp_prom=mal"
        elif modo == "incompleta":
            lineas.pop()
        elif modo == "duplicada":
            lineas.append(lineas[0])
        elif modo == "sufijo":
            lineas[4] = "M1.temp_prom=okay"
        lineas.append("intentos_restantes=9")
        self.responder(200, "\r\n".join(lineas) + "\r\n")


class AnalisisITLA(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.servidor = ThreadingHTTPServer(("127.0.0.1", 4000), Servidor)
        cls.hilo = threading.Thread(target=cls.servidor.serve_forever, daemon=True)
        cls.hilo.start()

    @classmethod
    def tearDownClass(cls):
        cls.servidor.shutdown()
        cls.servidor.server_close()
        cls.hilo.join(timeout=5)

    def setUp(self):
        self.temporal = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporal.cleanup)
        self.carpeta = Path(self.temporal.name)
        self.filas = crear_filas()
        self.env = dict(os.environ, ITLA_HOST="127.0.0.1",
                        ITLA_MATRICULA="20250442", ITLA_TOKEN=TOKEN_PRUEBA)
        s = self.servidor
        s.filas, s.umbral = self.filas, UMBRAL
        s.eventos, s.errores, s.modo = [], [], "correcto"
        resultado = self.ejecutar("config")
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        self.guardar_filas(self.filas)
        s.eventos.clear()

    def ejecutar(self, *argumentos, env=None):
        return subprocess.run([str(EXE), *argumentos], cwd=self.carpeta,
                              env=self.env if env is None else env,
                              capture_output=True, text=True, timeout=12)

    def guardar_filas(self, filas):
        with sqlite3.connect(self.carpeta / "estacion.db") as db:
            db.execute("DELETE FROM lecturas_planta")
            db.executemany("INSERT INTO lecturas_planta VALUES (?,?,?,?,?,?,?)",
                           filas)

    def config(self, clave, valor):
        with sqlite3.connect(self.carpeta / "estacion.db") as db:
            db.execute("UPDATE config SET valor=? WHERE clave=?", (valor, clave))

    def comprobar_tabla(self, resultado, filas=None, umbral=UMBRAL):
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        lineas = [l for l in resultado.stdout.splitlines()
                  if l.startswith(("M1,", "M2,", "M3,", "M4,"))]
        recibidas = list(csv.DictReader(
            ["maquina," + ",".join(CAMPOS)] + lineas))
        esperado = referencia_python(self.filas if filas is None else filas,
                                     umbral)
        self.assertEqual(len(recibidas), 4)
        for recibida, referencia in zip(recibidas, esperado):
            self.assertEqual(recibida["maquina"], referencia["maquina"])
            for campo in CAMPOS:
                if referencia[campo] is None:
                    self.assertEqual(recibida[campo], "NULL")
                elif campo.startswith("temp_"):
                    self.assertAlmostEqual(float(recibida[campo]),
                                           referencia[campo], delta=.010001)
                else:
                    self.assertEqual(int(recibida[campo]), referencia[campo])

    def no_enviada(self, resultado):
        self.assertNotEqual(resultado.returncode, 0)
        self.assertEqual(self.servidor.eventos, [])

    def test_local_sin_token_y_sin_peticiones(self):
        env = {k: v for k, v in os.environ.items() if not k.startswith("ITLA_")}
        for _ in range(3):
            resultado = self.ejecutar("analizar", env=env)
            self.comprobar_tabla(resultado)
        self.assertEqual(self.servidor.eventos, [])

    def test_filtro_de_todos_los_campos_y_ceros_validos(self):
        self.comprobar_tabla(self.ejecutar("analizar"))
        referencia = referencia_python(self.filas, UMBRAL)[0]
        self.assertEqual(referencia["temp_min"], -4.25)
        self.assertLess(referencia["temp_max"], 999.0)

    def test_umbral_variable_y_comparacion_estricta(self):
        self.config("umbral_temp", "50.00")
        self.servidor.umbral = 50.0
        self.comprobar_tabla(self.ejecutar("analizar"), umbral=50.0)
        resultado = self.ejecutar("analizar", "--enviar")
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        self.assertEqual(len(self.servidor.eventos), 1)

    def test_post_de_cuatro_maquinas_y_24_confirmaciones(self):
        resultado = self.ejecutar("analizar", "--enviar")
        self.comprobar_tabla(resultado)
        self.assertIn("24 de 24", resultado.stdout)
        self.assertEqual(len(self.servidor.eventos), 1)
        self.assertEqual(self.servidor.errores, [])

    def test_descarga_parcial_solo_admite_informe_local(self):
        parciales = self.filas[:100]
        self.guardar_filas(parciales)
        self.comprobar_tabla(self.ejecutar("analizar"), filas=parciales)
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_maquina_sin_validas_conserva_null_y_no_envia(self):
        for fila in self.filas:
            if fila[1] == "M4":
                fila[6] = "FALLA"
        self.guardar_filas(self.filas)
        self.comprobar_tabla(self.ejecutar("analizar"))
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_maquina_ausente_conserva_cero_y_null(self):
        filas = [f for f in self.filas if f[1] != "M4"]
        self.guardar_filas(filas)
        self.comprobar_tabla(self.ejecutar("analizar"), filas=filas)
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_maquina_desconocida_no_se_ignora_en_silencio(self):
        self.filas[1][1] = "M5"
        self.guardar_filas(self.filas)
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_maquina_nula_no_se_inventa(self):
        self.filas[1][1] = None
        self.guardar_filas(self.filas)
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_umbral_malformado_no_gasta_intentos(self):
        for valor in ("74.23basura", "nan", "inf", "1e9999", ""):
            with self.subTest(valor=valor):
                self.config("umbral_temp", valor)
                self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_matricula_distinta_no_envia(self):
        self.config("matricula", "otra")
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_config_sin_maquinas_esperadas_no_envia(self):
        self.config("maquinas", "M1,M2")
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_base_sin_lecturas_no_envia(self):
        self.guardar_filas([])
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_error_sql_no_envia(self):
        with sqlite3.connect(self.carpeta / "estacion.db") as db:
            db.execute("ALTER TABLE lecturas_planta "
                       "RENAME COLUMN vibracion TO ausente")
        self.no_enviada(self.ejecutar("analizar", "--enviar"))

    def test_respuestas_rechazadas_no_se_confunden_con_exito(self):
        for modo in ("campo_mal", "http_401", "incompleta", "duplicada", "sufijo"):
            with self.subTest(modo=modo):
                self.servidor.modo = modo
                self.servidor.eventos.clear()
                resultado = self.ejecutar("analizar", "--enviar")
                self.assertNotEqual(resultado.returncode, 0)
                self.assertEqual(len(self.servidor.eventos), 1)

    def test_error_de_transporte_no_reintenta_la_evaluacion(self):
        self.servidor.modo = "transporte"
        resultado = self.ejecutar("analizar", "--enviar")
        self.assertNotEqual(resultado.returncode, 0)
        self.assertEqual(len(self.servidor.eventos), 1)

    def test_regresion_descarga_y_analisis_juntos(self):
        self.guardar_filas([])
        resultado = self.ejecutar("descargar")
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        self.assertIn("correcto=1", resultado.stdout)
        with sqlite3.connect(self.carpeta / "estacion.db") as db:
            filas = db.execute(
                "SELECT COUNT(*),SUM(id) FROM lecturas_planta").fetchone()
        self.assertEqual(filas, (2000, sum(f[0] for f in self.filas)))
        self.servidor.eventos.clear()
        self.comprobar_tabla(self.ejecutar("analizar"))
        self.assertEqual(self.servidor.eventos, [])

    def test_regresion_config_401_conserva_datos(self):
        self.servidor.modo = "config_401"
        resultado = self.ejecutar("config")
        self.assertNotEqual(resultado.returncode, 0)
        self.servidor.eventos.clear()
        self.comprobar_tabla(self.ejecutar("analizar"))

    def test_argumentos_incorrectos_no_hacen_peticiones(self):
        for args in ((), ("otra",), ("config", "--enviar"),
                     ("analizar", "--otro"), ("analizar", "--enviar", "extra")):
            with self.subTest(args=args):
                self.no_enviada(self.ejecutar(*args))


if __name__ == "__main__":
    unittest.main(verbosity=2)
