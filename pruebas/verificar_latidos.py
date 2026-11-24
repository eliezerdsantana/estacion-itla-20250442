"""Pruebas de MQTT junto con HTTP: no contactan el servidor del curso."""
from contextlib import closing
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import threading
import time
import unittest
from http.server import ThreadingHTTPServer

from verificar_envio import Contexto, Servidor, EXE, TOKEN
from mqtt_prueba import BrokerPrueba, PublicadorPrueba


class LatidosITLA(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http = ThreadingHTTPServer(('127.0.0.1', 4000), Servidor)
        cls.broker = BrokerPrueba(('127.0.0.1', 1883), PublicadorPrueba)
        cls.broker.token = TOKEN
        cls.hilos = []
        for servidor in (cls.http, cls.broker):
            hilo = threading.Thread(target=servidor.serve_forever, daemon=True)
            hilo.start()
            cls.hilos.append(hilo)

    @classmethod
    def tearDownClass(cls):
        for servidor in (cls.http, cls.broker):
            servidor.shutdown()
            servidor.server_close()
        for hilo in cls.hilos:
            hilo.join(timeout=5)

    def setUp(self):
        self.temporal = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporal.cleanup)
        self.carpeta = Path(self.temporal.name)
        self.ctx = Contexto(self.carpeta)
        self.ctx.mqtt_modo = 'correcto'
        self.ctx.mqtt_conexiones = []
        self.ctx.mqtt_mensajes = []
        self.ctx.mqtt_errores = []
        self.http.contexto = self.broker.contexto = self.ctx
        self.env = dict(os.environ, ITLA_HOST='127.0.0.1',
                        ITLA_MATRICULA='20250442', ITLA_TOKEN=TOKEN)
        r = self.ejecutar('config')
        self.assertEqual(r.returncode, 0, r.stderr)
        self.config('latido_s', '1')

    def config(self, clave, valor):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            db.execute('UPDATE config SET valor=? WHERE clave=?', (valor, clave))

    def filas(self):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db:
            return db.execute('SELECT seq,ts,temp,corriente,enviado,intentos '
                              'FROM telemetria ORDER BY seq').fetchall()

    def ejecutar(self, *args):
        r = subprocess.run([str(EXE), *args], cwd=self.carpeta, env=self.env,
                           capture_output=True, text=True, timeout=20)
        self.assertNotIn(TOKEN, r.stdout + r.stderr)
        return r

    def comprobar(self, resultado, cantidad):
        self.assertEqual(resultado.returncode, 0, resultado.stderr)
        self.assertEqual(len(self.filas()), cantidad)
        self.assertEqual(self.ctx.errores, [])
        self.assertEqual(self.ctx.mqtt_errores, [])

    def cargas(self):
        salida = []
        for mensaje in self.ctx.mqtt_mensajes:
            self.assertEqual(mensaje['topico'],
                             'itla/estudiantes/20250442/latido')
            self.assertEqual(mensaje['qos'], 0)
            self.assertFalse(mensaje['retain'])
            carga = mensaje['carga']
            self.assertLess(len(carga), 100)
            self.assertTrue(carga.endswith(b'\n'))
            campos = carga.decode().strip().split(';')
            self.assertEqual(campos[0], 'LATIDO')
            self.assertEqual(len(campos), 4)
            seq, pendientes, ts = map(int, campos[1:])
            self.assertGreater(seq, 0)
            self.assertGreaterEqual(pendientes, 0)
            self.assertLessEqual(abs(mensaje['fecha'] - ts), 3)
            salida.append((seq, pendientes, ts))
        return salida

    def test_01_topico_credenciales_formato_y_qos0(self):
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar(r, 4)
        cargas = self.cargas()
        self.assertEqual(len(cargas), 4)
        self.assertEqual([c[0] for c in cargas], [1,2,3,4])
        self.assertTrue(all(c[1] for c in self.ctx.mqtt_conexiones))
        self.assertTrue(all(f[4] == 1 for f in self.filas()))
        self.assertEqual(r.stdout.count('Latido MQTT enviado:'), 4)

    def test_02_usa_el_periodo_de_configuracion(self):
        self.config('latido_s', '2')
        r = self.ejecutar('enviar', '--duracion', '6')
        self.comprobar(r, 6)
        cargas = self.cargas()
        self.assertEqual([c[0] for c in cargas], [1,3,5])
        mensajes = self.ctx.mqtt_mensajes
        for a, b in zip(mensajes, mensajes[1:]):
            self.assertGreaterEqual(b['mono'] - a['mono'], 1.7)
            self.assertLess(b['mono'] - a['mono'], 2.5)

    def test_03_los_latidos_siguen_durante_retry_after_http(self):
        self.ctx.modo = 'dos_caidas'
        self.ctx.inicio = time.monotonic()
        r = self.ejecutar('enviar', '--duracion', '10')
        self.comprobar(r, 10)
        cargas = self.cargas()
        self.assertEqual(len(cargas), 10)
        self.assertEqual([c[1] for c in cargas[:3]], [1,2,3])
        self.assertGreaterEqual(sum(c[1] > 0 for c in cargas), 6)
        for a, b in zip(self.ctx.mqtt_mensajes, self.ctx.mqtt_mensajes[1:]):
            self.assertLess(b['mono'] - a['mono'], 1.6)

    def test_04_http_lento_no_bloquea_muestreo_ni_latidos(self):
        self.ctx.modo = 'lenta'
        r = self.ejecutar('enviar', '--duracion', '6')
        self.comprobar(r, 6)
        cargas = self.cargas()
        self.assertEqual(len(cargas), 6)
        self.assertGreaterEqual(cargas[3][1], 3)
        self.assertTrue(any(f[4] == 0 for f in self.filas()))

    def test_05_rechazo_mqtt_no_detiene_la_cola_http(self):
        self.ctx.mqtt_modo = 'rechazar'
        r = self.ejecutar('enviar', '--duracion', '3')
        self.comprobar(r, 3)
        self.assertEqual(self.ctx.mqtt_mensajes, [])
        self.assertTrue(all(f[4] == 1 for f in self.filas()))
        self.assertIn('Latido MQTT descartado:', r.stdout)

    def test_06_latido_fallido_no_se_encola_ni_se_repite(self):
        self.ctx.mqtt_modo = 'demorar_primera'
        r = self.ejecutar('enviar', '--duracion', '8')
        self.comprobar(r, 8)
        cargas = self.cargas()
        self.assertGreaterEqual(len(cargas), 2)
        self.assertTrue(all(c[0] > 1 for c in cargas))
        self.assertEqual(len({c[0] for c in cargas}), len(cargas))
        self.assertTrue(all(f[4] == 1 for f in self.filas()))
        self.assertIn('Latido MQTT descartado:', r.stdout)

    def test_07_broker_corta_y_siguiente_latido_lleva_datos_nuevos(self):
        self.ctx.mqtt_modo = 'cortar_primera'
        r = self.ejecutar('enviar', '--duracion', '4')
        self.comprobar(r, 4)
        self.assertEqual([c[0] for c in self.cargas()], [2,3,4])
        self.assertTrue(all(f[4] == 1 for f in self.filas()))

    def test_08_reinicio_del_programa_conserva_seq_del_latido(self):
        self.comprobar(self.ejecutar('enviar', '--duracion', '2'), 2)
        self.comprobar(self.ejecutar('enviar', '--duracion', '2'), 4)
        self.assertEqual([c[0] for c in self.cargas()], [1,2,3,4])

    def test_09_periodo_invalido_no_inicia_red(self):
        for valor in ('0','-1','1.5','basura','4294967296',''):
            with self.subTest(valor=valor):
                self.config('latido_s', valor)
                r = self.ejecutar('enviar', '--duracion', '1')
                self.assertNotEqual(r.returncode, 0)
                self.assertEqual(self.filas(), [])
                self.assertEqual(self.ctx.eventos, [])
                self.assertEqual(self.ctx.mqtt_conexiones, [])

    def test_10_matricula_no_puede_cambiar_el_topico(self):
        self.config('matricula', '2025/0442')
        self.env['ITLA_MATRICULA'] = '2025/0442'
        r = self.ejecutar('enviar', '--duracion', '1')
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(self.filas(), [])
        self.assertEqual(self.ctx.eventos, [])
        self.assertEqual(self.ctx.mqtt_conexiones, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
