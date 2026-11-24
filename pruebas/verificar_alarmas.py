"""Pruebas de suscripcion, ACK y dos procesos; solo usan servidores locales."""
from contextlib import closing
import json
import os
from pathlib import Path
import select
import signal
import socket
import socketserver
import sqlite3
import subprocess
import tempfile
import threading
import time
import unittest
from http.server import ThreadingHTTPServer

from verificar_envio import Contexto, Servidor, EXE, TOKEN
from mqtt_prueba import BrokerPrueba, leer_paquete, leer_cadena

TOPICO = b'itla/planta/20250442/alarma'


def publicar(texto, topico=TOPICO):
    carga = texto.encode() if isinstance(texto, str) else texto
    datos = len(topico).to_bytes(2, 'big') + topico + carga
    longitud = len(datos)
    cabecera = bytearray([0x31])  # PUBLISH QoS 0 retenido.
    while True:
        numero = longitud % 128
        longitud //= 128
        cabecera.append(numero | (128 if longitud else 0))
        if not longitud:
            break
    return bytes(cabecera) + datos


def alarma(codigo, maquina='M1'):
    return f'ALARMA;{codigo};{maquina};{int(time.time())}\n'


class BrokerAlarmas(socketserver.BaseRequestHandler):
    def handle(self):
        ctx = self.server.contexto
        self.request.settimeout(4)
        try:
            cabecera, datos = leer_paquete(self.request)
            protocolo, p = leer_cadena(datos, 0)
            if cabecera != 0x10 or protocolo != b'MQTT' or datos[p] != 4:
                raise ValueError('CONNECT incorrecto')
            flags = datos[p + 1]
            p += 4
            _, p = leer_cadena(datos, p)
            if flags & 4:
                _, p = leer_cadena(datos, p)
                _, p = leer_cadena(datos, p)
            usuario, p = leer_cadena(datos, p) if flags & 128 else (b'', p)
            clave, p = leer_cadena(datos, p) if flags & 64 else (b'', p)
            valido = usuario == b'20250442' and clave == TOKEN.encode()
            ctx.conexiones.append((time.monotonic(), valido))
            if not valido or ctx.mqtt_modo == 'rechazar':
                self.request.sendall(b'\x20\x02\x00\x05')
                return
            self.request.sendall(b'\x20\x02\x00\x00')
            cabecera, datos = leer_paquete(self.request)
            if cabecera >> 4 == 3:
                topico, p = leer_cadena(datos, 0)
                if topico != b'itla/estudiantes/20250442/latido':
                    raise ValueError('Topico de latido incorrecto')
                campos = datos[p:].decode().strip().split(';')
                ctx.latidos.append(campos)
                if ctx.mqtt_modo == 'desde_latido':
                    with ctx.cerrojo:
                        ctx.por_emitir.append(publicar(alarma('L' + campos[1])))
                return
            if cabecera != 0x82:
                raise ValueError('Falta SUBSCRIBE')
            topico, p = leer_cadena(datos, 2)
            if topico != TOPICO or p + 1 != len(datos) or datos[p] != 0:
                raise ValueError('Topico o QoS de suscripcion incorrecto')
            self.request.sendall(b'\x90\x03' + datos[:2] + b'\x00')
            ctx.suscripciones.append(time.monotonic())
            numero = len(ctx.suscripciones)
            ctx.suscrita.set()
            paquetes = ctx.paquetes
            if numero == 1 and ctx.primera is not None:
                paquetes = ctx.primera
            salida = b''.join(paquetes)
            if ctx.fragmentar and numero == 1:
                for i in range(0, len(salida), 3):
                    self.request.sendall(salida[i:i + 3])
                    time.sleep(.002)
            else:
                self.request.sendall(salida)
            if ctx.mqtt_modo == 'cortar_primera' and numero == 1:
                return
            while not ctx.terminar.is_set():
                with ctx.cerrojo:
                    pendientes = ctx.por_emitir[:]
                    ctx.por_emitir.clear()
                if pendientes:
                    self.request.sendall(b''.join(pendientes))
                if not select.select([self.request], [], [], .05)[0]:
                    continue
                cabecera, _ = leer_paquete(self.request)
                if cabecera >> 4 == 12:
                    self.request.sendall(b'\xd0\x00')
                elif cabecera >> 4 == 14:
                    return
        except (EOFError, OSError):
            pass
        except (ValueError, IndexError) as error:
            ctx.errores.append(str(error))


class ServidorAck(Servidor):
    def do_POST(self):
        if self.path != '/api/v1/alarmas/ack':
            return super().do_POST()
        if not self.autenticar():
            return
        ctx = self.server.contexto
        cuerpo = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        codigo = cuerpo.get('codigo')
        with closing(sqlite3.connect(ctx.carpeta / 'estacion.db')) as db:
            fila = db.execute('SELECT ts_recibida,ts_ack FROM alarmas '
                              'WHERE codigo=?', (codigo,)).fetchone()
        ctx.acks.append((time.monotonic(), codigo))
        if set(cuerpo) != {'codigo'} or fila is None or fila[1] is not None:
            ctx.errores.append(('ACK sin registro pendiente', codigo, fila))
            self.responder(400, 'error=no_guardada\n')
            return
        if ctx.ack_modo == 'sin_red':
            self.close_connection = True
            return
        if ctx.ack_modo == '401':
            self.responder(401, 'error=acceso\n')
            return
        if ctx.ack_modo == 'retry' and len(ctx.acks) == 1:
            self.responder(503, 'error=espera\n', 2)
            return
        if codigo == 'NO_EXISTE':
            self.responder(404, 'error=no_existe\n')
            return
        duplicada = codigo in ctx.aceptadas_alarmas
        ctx.aceptadas_alarmas.add(codigo)
        if ctx.ack_modo == 'perdida' and len(ctx.acks) == 1:
            self.close_connection = True
            return
        if ctx.ack_modo == 'lenta':
            time.sleep(4)
        self.responder(409 if duplicada else 200, 'latencia_ms=125\n')


class AlarmasITLA(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.http = ThreadingHTTPServer(('127.0.0.1', 4000), ServidorAck)
        cls.broker = BrokerPrueba(('127.0.0.1', 1883), BrokerAlarmas)
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
        self.ctx.ack_modo = 'correcto'
        self.ctx.paquetes = [publicar(alarma('A1'))]
        self.ctx.primera = None
        self.ctx.fragmentar = False
        self.ctx.conexiones = []
        self.ctx.suscripciones = []
        self.ctx.acks = []
        self.ctx.aceptadas_alarmas = set()
        self.ctx.latidos = []
        self.ctx.por_emitir = []
        self.ctx.cerrojo = threading.Lock()
        self.ctx.suscrita = threading.Event()
        self.ctx.terminar = threading.Event()
        self.addCleanup(self.ctx.terminar.set)
        self.http.contexto = self.broker.contexto = self.ctx
        self.env = dict(os.environ, ITLA_HOST='127.0.0.1',
                        ITLA_MATRICULA='20250442', ITLA_TOKEN=TOKEN)
        r = self.ejecutar('config')
        self.assertEqual(r.returncode, 0, r.stderr)

    def ejecutar(self, *args):
        r = subprocess.run([str(EXE), *args], cwd=self.carpeta, env=self.env,
                           capture_output=True, encoding='utf-8', timeout=20)
        self.assertNotIn(TOKEN, r.stdout + r.stderr)
        return r

    def iniciar(self, *args):
        p = subprocess.Popen([str(EXE), *args], cwd=self.carpeta, env=self.env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             encoding='utf-8')
        def cerrar():
            if p.poll() is None:
                p.kill()
            p.communicate(timeout=5)
        self.addCleanup(cerrar)
        return p

    def consulta(self, sql):
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db:
            return db.execute(sql).fetchall()

    def filas(self):
        return self.consulta('SELECT codigo,maquina,ts_servidor,ts_recibida,ts_ack '
                             'FROM alarmas ORDER BY codigo')

    def comprobar(self, r, cantidad):
        self.assertEqual(r.returncode, 0, r.stderr)
        filas = self.filas()
        self.assertEqual(len(filas), cantidad)
        self.assertTrue(all(f[4] is not None for f in filas))
        self.assertEqual(self.ctx.errores, [])
        self.assertTrue(all(c[1] for c in self.ctx.conexiones))

    def test_01_guardar_antes_del_ack_e_ignorar_ninguna(self):
        self.ctx.paquetes.insert(0, publicar('NINGUNA;123\n'))
        r = self.ejecutar('escuchar', '--duracion', '2')
        self.comprobar(r, 1)
        self.assertEqual([a[1] for a in self.ctx.acks], ['A1'])
        self.assertIn('latencia_ms=125', r.stdout)
        self.assertLess(self.ctx.acks[0][0] - self.ctx.suscripciones[0], .8)

    def test_02_fragmentos_y_varios_mensajes_consecutivos(self):
        self.ctx.fragmentar = True
        self.ctx.paquetes = [publicar(alarma('A' + str(i))) for i in range(4)]
        self.comprobar(self.ejecutar('escuchar', '--duracion', '3'), 4)
        self.assertEqual(len(self.ctx.acks), 4)

    def test_03_reconexion_y_retenida_no_duplican_ack(self):
        self.ctx.mqtt_modo = 'cortar_primera'
        self.ctx.paquetes *= 3
        r = self.ejecutar('escuchar', '--duracion', '4')
        self.comprobar(r, 1)
        self.assertGreaterEqual(len(self.ctx.suscripciones), 2)
        self.assertEqual(len(self.ctx.acks), 1)
        self.assertIn('Alarma repetida:', r.stdout)

    def test_04_respuesta_perdida_y_409_confirman_la_misma_alarma(self):
        self.ctx.ack_modo = 'perdida'
        r = self.ejecutar('escuchar', '--duracion', '3')
        self.comprobar(r, 1)
        self.assertEqual([a[1] for a in self.ctx.acks], ['A1', 'A1'])
        self.assertIn('HTTP 409:', r.stdout)

    def test_05_retry_after_http_no_bloquea_recepcion(self):
        self.ctx.ack_modo = 'retry'
        self.ctx.paquetes += [publicar(alarma('A2'))]
        r = self.ejecutar('escuchar', '--duracion', '4')
        self.comprobar(r, 2)
        self.assertGreaterEqual(self.ctx.acks[1][0] - self.ctx.acks[0][0], 1.98)
        self.assertLess(self.filas()[1][3] - self.filas()[0][3], 2)

    def test_06_reinicio_conserva_alarma_pendiente(self):
        self.ctx.ack_modo = 'sin_red'
        r = self.ejecutar('escuchar', '--duracion', '1')
        self.assertEqual(r.returncode, 0, r.stderr)
        anterior = self.filas()[0]
        self.assertIsNone(anterior[4])
        self.ctx.ack_modo = 'correcto'
        self.ctx.paquetes = [publicar('NINGUNA;123\n')]
        self.comprobar(self.ejecutar('escuchar', '--duracion', '2'), 1)
        self.assertEqual(self.filas()[0][:4], anterior[:4])

    def test_07_404_no_inventa_ack_ni_bloquea_otras_alarmas(self):
        self.ctx.paquetes = [publicar(alarma('NO_EXISTE')), publicar(alarma('Z1'))]
        r = self.ejecutar('escuchar', '--duracion', '2')
        self.assertEqual(r.returncode, 0, r.stderr)
        filas = self.filas()
        self.assertIsNone(filas[0][4])
        self.assertIsNotNone(filas[1][4])
        self.assertEqual(self.consulta('SELECT codigo FROM alarmas_no_vigentes'),
                         [('NO_EXISTE',)])
        self.assertEqual(len(self.ctx.acks), 2)
        self.assertEqual(self.ctx.errores, [])

    def test_08_codigo_se_escapa_en_json_y_se_guarda_literal(self):
        codigo = 'A"\\ñ\t\x01'
        self.ctx.paquetes = [publicar(alarma(codigo))]
        self.comprobar(self.ejecutar('escuchar', '--duracion', '2'), 1)
        self.assertEqual(self.filas()[0][0], codigo)
        self.assertEqual(self.ctx.acks[0][1], codigo)

    def test_09_formatos_invalidos_y_topico_ajeno_se_ignoran(self):
        textos = ['ALARMA;;M1;123\n', 'ALARMA;B;M5;123\n',
                  'ALARMA;B;M1;-1\n', 'ALARMA;B;M1;123;extra\n',
                  'ALARMA;B;M1;9223372036854775808\n', 'BASURA\n']
        self.ctx.paquetes = [publicar(t) for t in textos]
        self.ctx.paquetes += [publicar(alarma('AJENA'), b'otro/topico'),
                              publicar(alarma('A1'))]
        self.comprobar(self.ejecutar('escuchar', '--duracion', '2'), 1)
        self.assertEqual([a[1] for a in self.ctx.acks], ['A1'])

    def test_10_trama_excesiva_o_nul_reconecta_y_recupera(self):
        for invalida in (b'x' * 100 + b'\n', b'ALARMA;N\x00;M1;123\n'):
            with self.subTest(invalida=invalida[:12]):
                self.ctx.suscripciones.clear()
                self.ctx.primera = [publicar(invalida)]
                self.ctx.paquetes = [publicar(alarma('A1'))]
                self.comprobar(self.ejecutar('escuchar', '--duracion', '3'), 1)
                self.assertGreaterEqual(len(self.ctx.suscripciones), 2)

    def test_11_rafaga_mayor_de_4096_bytes_no_pierde_la_ultima(self):
        self.ctx.paquetes = [publicar('NINGUNA;1234567890\n')] * 130
        self.ctx.paquetes += [publicar(alarma('A1'))]
        self.assertGreater(len(b''.join(self.ctx.paquetes)), 4096)
        self.comprobar(self.ejecutar('escuchar', '--duracion', '3'), 1)

    def test_12_enviar_y_escuchar_comparten_sqlite_con_latidos(self):
        self.ctx.mqtt_modo = 'desde_latido'
        self.ctx.paquetes = []
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            db.execute("UPDATE config SET valor='1' WHERE clave='latido_s'")
        receptor = self.iniciar('escuchar', '--duracion', '7')
        self.assertTrue(self.ctx.suscrita.wait(3), 'No se suscribio')
        r = self.ejecutar('enviar', '--duracion', '5')
        salida, errores = receptor.communicate(timeout=12)
        self.assertEqual(receptor.returncode, 0, errores)
        self.comprobar(r, 5)
        self.assertEqual(len(self.ctx.latidos), 5)
        self.assertEqual(self.consulta('SELECT COUNT(*) FROM telemetria '
                                       'WHERE enviado=1'), [(5,)])
        self.assertNotIn(TOKEN, salida + errores)

    @unittest.skipIf(os.name == 'nt', 'SIGINT de consola se comprueba manualmente')
    def test_13_ctrl_c_sale_sin_esperar_la_suscripcion(self):
        self.ctx.paquetes = []
        p = self.iniciar('escuchar')
        self.assertTrue(self.ctx.suscrita.wait(3))
        inicio = time.monotonic()
        p.send_signal(signal.SIGINT)
        salida, errores = p.communicate(timeout=2)
        self.assertEqual(p.returncode, 0, errores)
        self.assertLess(time.monotonic() - inicio, 1)
        self.assertIn('Escucha detenida;', salida)

    def test_14_broker_rechazado_respeta_espera_creciente(self):
        self.ctx.mqtt_modo = 'rechazar'
        r = self.ejecutar('escuchar', '--duracion', '4')
        self.assertEqual(r.returncode, 0, r.stderr)
        eventos = self.ctx.conexiones
        self.assertEqual(len(eventos), 3)
        for a, b, espera in zip(eventos, eventos[1:], (1, 2)):
            self.assertGreaterEqual(b[0] - a[0], espera - .02)
        self.assertEqual(self.filas(), [])

    def test_15_acceso_rechazado_no_se_marca_como_confirmado(self):
        self.ctx.ack_modo = '401'
        r = self.ejecutar('escuchar', '--duracion', '2')
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(len(self.ctx.acks), 1)
        self.assertIsNone(self.filas()[0][4])
        self.assertIn('ACK rechazado', r.stderr)

    def test_16_duracion_e_identidad_invalidas_no_inician_mqtt(self):
        for valor in ('0', '-1', '1.2', 'basura', '4294967296'):
            with self.subTest(valor=valor):
                r = self.ejecutar('escuchar', '--duracion', valor)
                self.assertNotEqual(r.returncode, 0)
                self.assertIn('Uso:', r.stderr)
        with closing(sqlite3.connect(self.carpeta / 'estacion.db')) as db, db:
            db.execute("UPDATE config SET valor='otra' WHERE clave='matricula'")
        r = self.ejecutar('escuchar', '--duracion', '1')
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(self.ctx.conexiones, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
