"""Pruebas locales de la cola: no usan el token ni el servidor del curso."""
import json
from contextlib import closing
import math
import os
from pathlib import Path
import secrets
import signal
import sqlite3
import subprocess
import tempfile
import threading
import time
import unittest
from email.utils import formatdate, parsedate_to_datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / ('estacion.exe' if os.name == 'nt' else 'estacion')
TOKEN = secrets.token_hex(16)


class Contexto:
    def __init__(self, carpeta):
        self.carpeta = carpeta
        self.modo = 'correcto'
        self.inicio = time.monotonic()
        self.eventos = []
        self.aceptadas = set()
        self.errores = []


class Servidor(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def responder(self, codigo, texto, espera=None):
        try:
            cuerpo = texto.encode()
            self.send_response(codigo)
            self.send_header('Content-Type', 'text/plain')
            self.send_header('Content-Length', str(len(cuerpo)))
            if espera is not None:
                self.send_header('rEtRy-AfTeR', str(espera))
            self.end_headers()
            self.wfile.write(cuerpo)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def autenticar(self):
        if (self.headers.get('Authorization') != 'Bearer ' + TOKEN or
                self.headers.get('User-Agent') != 'estacion-itla/1.0'):
            self.responder(401, 'error=credencial_de_prueba\n')
            return False
        return True

    def do_GET(self):
        if not self.autenticar():
            return
        if self.path == '/api/v1/config':
            self.responder(200, 'matricula=20250442\nhora=1795523281\n'
                'umbral_temp=74.23\nintervalo_s=1\nlatido_s=10\n'
                'maquinas=M1,M2,M3,M4\nlote_max=200\n')
        else:
            self.responder(404, 'error=ruta\n')

    def do_POST(self):
        if not self.autenticar():
            return
        ctx = self.server.contexto
        cuerpo = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        ahora = time.monotonic()
        ctx.eventos.append((ahora, time.time(), cuerpo))
        seq = cuerpo.get('seq')
        if self.path != '/api/v1/telemetria':
            ctx.errores.append('Ruta incorrecta')
            self.responder(404, 'error=ruta\n')
            return
        with closing(sqlite3.connect(ctx.carpeta / 'estacion.db')) as db, db:
            fila = db.execute('SELECT ts,temp,corriente,enviado,intentos '
                              'FROM telemetria WHERE seq=?', (seq,)).fetchone()
        if (set(cuerpo) != {'seq', 'ts', 'temp', 'corriente'} or fila is None or
                tuple(cuerpo[k] for k in ('ts','temp','corriente')) != fila[:3] or
                fila[3] != 0 or fila[4] < 1 or
                not 25 <= cuerpo['temp'] <= 45 or
                not 2 <= cuerpo['corriente'] <= 5):
            ctx.errores.append(('No estaba guardada antes del POST', cuerpo, fila))
            self.responder(400, 'error=medicion_no_guardada\n')
            return
        modo = ctx.modo
        numero = len(ctx.eventos)
        if modo.startswith('http_'):
            self.responder(int(modo[5:]), 'error=prueba\n')
            return
        if modo == 'sin_red':
            self.close_connection = True
            return
        if modo == 'dos_caidas':
            transcurrido = ahora - ctx.inicio
            if transcurrido < 2.4 or 5 <= transcurrido < 7.4:
                self.responder(503, 'error=mantenimiento\n', 3)
                return
        if modo in ('retry_429', 'retry_fecha', 'interina') and numero == 1:
            espera = 2
            if modo == 'retry_fecha':
                espera = formatdate(time.time() + 3, usegmt=True)
                ctx.fecha_retry = parsedate_to_datetime(espera).timestamp()
            if modo == 'interina':
                self.wfile.write(b'HTTP/1.1 100 Continue\r\n'
                                 b'Retry-After: 99\r\n\r\n')
            self.responder(429 if modo == 'retry_429' else 503,
                           'error=espera\n', espera)
            return
        if modo == 'lenta':
            time.sleep(4)
        duplicada = seq in ctx.aceptadas
        ctx.aceptadas.add(seq)
        if modo == 'respuesta_perdida' and numero == 1:
            self.close_connection = True
            return
        if modo == 'confirmacion_falsa' and numero == 1:
            self.responder(200, 'estado=duplicadax\n')
            return
        if modo == 'respuesta_grande' and numero == 1:
            self.responder(201, 'x' * 700)
            return
        self.responder(200 if duplicada else 201,
                       'estado=duplicada\n' if duplicada else 'estado=creada\n')


class EnvioITLA(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.servidor = ThreadingHTTPServer(('127.0.0.1', 4000), Servidor)
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
        self.ctx = Contexto(self.carpeta)
        self.servidor.contexto = self.ctx
        self.env = dict(os.environ, ITLA_HOST='127.0.0.1',
                        ITLA_MATRICULA='20250442', ITLA_TOKEN=TOKEN)
        r = self.ejecutar('config')
        self.assertEqual(r.returncode, 0, r.stderr)

    def ejecutar(self, *args, env=None):
        r = subprocess.run([str(EXE), *args], cwd=self.carpeta,
                           env=self.env if env is None else env,
                           capture_output=True, text=True, timeout=25)
        self.assertNotIn(TOKEN, r.stdout + r.stderr)
        return r

    def filas(self):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            return db.execute('SELECT seq,ts,temp,corriente,enviado,intentos '
                              'FROM telemetria ORDER BY seq').fetchall()

    def config(self, clave, valor):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            db.execute('UPDATE config SET valor=? WHERE clave=?', (valor, clave))

    def comprobar_correcto(self, resultado):
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        self.assertEqual(self.ctx.errores, [])

    def test_01_guardar_antes_de_enviar_y_confirmar_201(self):
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        filas = self.filas()
        self.assertEqual([f[0] for f in filas], [1,2,3,4])
        self.assertTrue(all(f[4] == 1 and f[5] == 1 for f in filas))
        self.assertEqual([e[2]['seq'] for e in self.ctx.eventos], [1,2,3,4])

    def test_02_dos_caidas_no_detienen_muestreo_y_recuperan_cola(self):
        self.ctx.modo = 'dos_caidas'
        self.ctx.inicio = time.monotonic()
        r = self.ejecutar('enviar', '--duracion', '12')
        self.comprobar_correcto(r)
        filas = self.filas()
        self.assertEqual(len(filas), 12)
        self.assertTrue(all(f[4] == 1 for f in filas))
        fallidas = 0
        for a, b in zip(self.ctx.eventos, self.ctx.eventos[1:]):
            if a[2]['seq'] == b[2]['seq']:
                self.assertGreaterEqual(b[0] - a[0], 2.98)
                fallidas += 1
        self.assertGreaterEqual(fallidas, 2)
        self.assertLessEqual(max(b[1]-a[1] for a,b in zip(filas,filas[1:])), 2)

    def test_03_respuesta_perdida_se_repite_y_200_duplicada_confirma(self):
        self.ctx.modo = 'respuesta_perdida'
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        self.assertIn('HTTP 200: seq=1 confirmada', r.stdout)
        self.assertEqual(self.filas()[0][5], 2)
        self.assertTrue(all(f[4] == 1 for f in self.filas()))
        self.assertEqual([e[2]['seq'] for e in self.ctx.eventos][:2], [1,1])

    def test_04_fallos_de_red_esperan_1_2_4_s_y_guardan_lecturas(self):
        self.ctx.modo = 'sin_red'
        r = self.ejecutar('enviar', '--duracion', '9')
        self.comprobar_correcto(r)
        filas = self.filas()
        self.assertEqual(len(filas), 9)
        self.assertTrue(all(f[4] == 0 for f in filas))
        eventos = self.ctx.eventos
        self.assertEqual(len(eventos), 4)
        for espera, a, b in zip((1,2,4), eventos, eventos[1:]):
            self.assertGreaterEqual(b[0]-a[0], espera - .02)
        self.assertEqual([e[2]['seq'] for e in eventos], [1]*4)

    def test_05_429_respeta_retry_after(self):
        self.ctx.modo = 'retry_429'
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        self.assertGreaterEqual(self.ctx.eventos[1][0]-self.ctx.eventos[0][0], 1.98)
        self.assertTrue(all(f[4] == 1 for f in self.filas()))

    def test_06_retry_after_admite_fecha_http(self):
        self.ctx.modo = 'retry_fecha'
        r = self.ejecutar('enviar', '--duracion', '5')
        self.comprobar_correcto(r)
        self.assertGreaterEqual(self.ctx.eventos[1][1], self.ctx.fecha_retry - .02)
        self.assertTrue(all(f[4] == 1 for f in self.filas()))

    def test_07_cabecera_interina_no_reemplaza_retry_final(self):
        self.ctx.modo = 'interina'
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        self.assertTrue(all(f[4] == 1 for f in self.filas()))
        self.assertGreaterEqual(self.ctx.eventos[1][0]-self.ctx.eventos[0][0], 1.98)

    def test_08_reinicio_conserva_secuencias_y_timestamps_pendientes(self):
        self.ctx.modo = 'sin_red'
        self.comprobar_correcto(self.ejecutar('enviar', '--duracion', '3'))
        anteriores = self.filas()
        self.ctx.modo = 'correcto'
        self.ctx.eventos.clear()
        self.comprobar_correcto(self.ejecutar('enviar', '--duracion', '3'))
        nuevas = self.filas()
        self.assertEqual([f[0] for f in nuevas], list(range(1,7)))
        self.assertEqual([f[:4] for f in anteriores], [f[:4] for f in nuevas[:3]])
        self.assertTrue(all(f[4] == 1 for f in nuevas))
        self.assertEqual(self.ctx.eventos[0][2]['seq'], 1)

    @unittest.skipIf(os.name == 'nt', 'SIGINT de proceso se comprueba en Linux')
    def test_09_ctrl_c_no_espera_respuesta_lenta_y_conserva_cola(self):
        self.ctx.modo = 'lenta'
        p = subprocess.Popen([str(EXE),'enviar'], cwd=self.carpeta, env=self.env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True)
        try:
            limite = time.monotonic() + 5
            while len(self.filas()) < 3 and time.monotonic() < limite:
                time.sleep(.05)
            antes = time.monotonic()
            p.send_signal(signal.SIGINT)
            out, err = p.communicate(timeout=2)
            self.assertLess(time.monotonic()-antes, .8)
            self.assertEqual(p.returncode, 0, err)
            self.assertEqual(len(self.filas()), 3)
            self.assertTrue(all(f[4] == 0 for f in self.filas()))
            self.assertNotIn(TOKEN, out + err)
            self.assertEqual(self.ctx.errores, [])
        finally:
            if p.poll() is None:
                p.kill()
                p.communicate()

    def test_10_error_persistente_no_descarta_lectura(self):
        for codigo in (400,401,403,404):
            with self.subTest(codigo=codigo):
                self.ctx.modo = 'http_' + str(codigo)
                self.ctx.eventos.clear()
                r = self.ejecutar('enviar', '--duracion', '5')
                self.assertNotEqual(r.returncode, 0)
                self.assertEqual(len(self.ctx.eventos), 1)
                self.assertTrue(all(f[4] == 0 for f in self.filas()))

    def test_11_confirmacion_falsa_no_marca_como_enviada(self):
        self.ctx.modo = 'confirmacion_falsa'
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        self.assertEqual(self.filas()[0][5], 2)
        self.assertEqual([e[2]['seq'] for e in self.ctx.eventos][:2], [1,1])

    def test_12_respuesta_excesiva_se_recupera_sin_perder_lectura(self):
        self.ctx.modo = 'respuesta_grande'
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        self.assertTrue(all(f[4] == 1 for f in self.filas()))
        self.assertEqual(self.filas()[0][5], 2)

    def test_13_cola_antigua_se_envia_en_orden_y_con_pausa(self):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            db.executemany('INSERT INTO telemetria VALUES (?,?,?,?,0,0)',
                           [(i, int(time.time())-45, 30.5, 3.2)
                            for i in range(1,21)])
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar_correcto(r)
        eventos = self.ctx.eventos
        seqs = [e[2]['seq'] for e in eventos]
        self.assertEqual(seqs, list(range(1, len(seqs)+1)))
        self.assertGreater(len(seqs), 5)
        self.assertTrue(all(b[0]-a[0] >= .249 for a,b in zip(eventos,eventos[1:])))
        self.assertTrue(all(e[1]-e[2]['ts'] >= 30 for e in eventos))

    def test_14_seq_supera_el_limite_de_32_bits(self):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            db.execute('INSERT INTO telemetria VALUES (?,?,?,?,1,1)',
                       (2147483648, int(time.time())-10, 30.0, 3.0))
        r = self.ejecutar('enviar', '--duracion', '2')
        self.comprobar_correcto(r)
        self.assertEqual([e[2]['seq'] for e in self.ctx.eventos],
                         [2147483649,2147483650])

    def test_15_configuracion_invalida_no_genera_peticiones(self):
        for valor in ('0','-1','1.5','basura','4294967296',''):
            with self.subTest(valor=valor):
                self.config('intervalo_s', valor)
                r = self.ejecutar('enviar', '--duracion', '1')
                self.assertNotEqual(r.returncode, 0)
                self.assertEqual(self.ctx.eventos, [])
                self.assertEqual(self.filas(), [])
        self.config('intervalo_s', '1')
        self.config('matricula', 'otra')
        r = self.ejecutar('enviar', '--duracion', '1')
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(self.ctx.eventos, [])

    def test_16_argumentos_invalidos_y_token_ausente_no_envian(self):
        for args in (('enviar','--duracion','0'), ('enviar','--duracion','-1'),
                     ('enviar','--duracion','2.5'), ('enviar','--duracion'),
                     ('enviar','otra'), ('enviar','--duracion','1','extra')):
            with self.subTest(args=args):
                self.assertNotEqual(self.ejecutar(*args).returncode, 0)
        env = self.env.copy()
        del env['ITLA_TOKEN']
        self.assertNotEqual(self.ejecutar('enviar','--duracion','1',env=env).returncode,0)
        self.assertEqual(self.ctx.eventos, [])
        self.assertEqual(self.filas(), [])

    def test_17_sensor_respeta_rangos_y_no_es_constante(self):
        fuente = self.carpeta / 'probar_sensor.c'
        fuente.write_text('''#include "sensor.h"
#include <math.h>
int main(void) {
    Sensor sensor; double t, a, menor=100, mayor=0; unsigned int i;
    sensor_iniciar(&sensor, 0);
    for (i=0; i<200000; i++) {
        sensor_leer(&sensor, (double)i*.31, &t, &a);
        if (!isfinite(t)||!isfinite(a)||t<25||t>45||a<2||a>5) return 1;
        if (fabs(t*100-round(t*100))>0.00001) return 2;
        if (t<menor) menor=t;
        if (t>mayor) mayor=t;
    }
    return menor<26 && mayor>44 ? 0 : 3;
}
''')
        binario = self.carpeta / ('probar_sensor.exe' if os.name == 'nt'
                                  else 'probar_sensor')
        r = subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror',
                            '-I'+str(ROOT/'src'),str(fuente),
                            str(ROOT/'src/sensor.c'),'-lm','-o',str(binario)],
                           capture_output=True, text=True, timeout=10)
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual(subprocess.run([str(binario)],timeout=5).returncode,0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
