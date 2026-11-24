"""Broker MQTT minimo para pruebas locales, sin dependencias externas."""
import socket
import socketserver
import time


def leer_exacto(conexion, n):
    salida = bytearray()
    while len(salida) < n:
        fragmento = conexion.recv(n - len(salida))
        if not fragmento:
            raise EOFError
        salida.extend(fragmento)
    return bytes(salida)


def leer_paquete(conexion):
    cabecera = leer_exacto(conexion, 1)[0]
    longitud = 0
    multiplicador = 1
    for _ in range(4):
        numero = leer_exacto(conexion, 1)[0]
        longitud += (numero & 127) * multiplicador
        if longitud > 4096:
            raise ValueError('Paquete excesivo')
        if not numero & 128:
            return cabecera, leer_exacto(conexion, longitud)
        multiplicador *= 128
    raise ValueError('Longitud MQTT invalida')


def leer_cadena(datos, posicion):
    if posicion + 2 > len(datos):
        raise ValueError('Falta longitud de cadena')
    longitud = int.from_bytes(datos[posicion:posicion + 2], 'big')
    inicio = posicion + 2
    fin = inicio + longitud
    if fin > len(datos):
        raise ValueError('Cadena incompleta')
    return datos[inicio:fin], fin


class PublicadorPrueba(socketserver.BaseRequestHandler):
    def handle(self):
        ctx = self.server.contexto
        self.request.settimeout(8)
        try:
            cabecera, datos = leer_paquete(self.request)
            if cabecera != 0x10:
                raise ValueError('El primer paquete no es CONNECT')
            protocolo, posicion = leer_cadena(datos, 0)
            if protocolo != b'MQTT' or posicion + 4 > len(datos):
                raise ValueError('Protocolo MQTT incorrecto')
            nivel, flags = datos[posicion:posicion + 2]
            posicion += 4
            _, posicion = leer_cadena(datos, posicion)  # Identificador de cliente.
            if flags & 4:
                _, posicion = leer_cadena(datos, posicion)
                _, posicion = leer_cadena(datos, posicion)
            usuario = clave = b''
            if flags & 128:
                usuario, posicion = leer_cadena(datos, posicion)
            if flags & 64:
                clave, posicion = leer_cadena(datos, posicion)
            valido = (nivel == 4 and usuario == b'20250442' and
                      clave == self.server.token.encode())
            # Guardamos el resultado de la autenticacion, nunca la clave.
            ctx.mqtt_conexiones.append((time.monotonic(), valido))
            numero = len(ctx.mqtt_conexiones)
            modo = ctx.mqtt_modo
            if modo == 'cortar_primera' and numero == 1:
                return
            if modo in ('demorar_primera', 'demorar_larga') and numero == 1:
                time.sleep(6 if modo == 'demorar_primera' else 10)
            if modo == 'rechazar' or not valido:
                self.request.sendall(b'\x20\x02\x00\x05')
                return
            self.request.sendall(b'\x20\x02\x00\x00')
            while True:
                cabecera, datos = leer_paquete(self.request)
                tipo = cabecera >> 4
                if tipo == 3:
                    topico, posicion = leer_cadena(datos, 0)
                    ctx.mqtt_mensajes.append(dict(
                        mono=time.monotonic(), fecha=time.time(),
                        topico=topico.decode(), carga=datos[posicion:],
                        qos=(cabecera >> 1) & 3, retain=bool(cabecera & 1)))
                elif tipo == 14:
                    return
                elif tipo == 12:
                    self.request.sendall(b'\xd0\x00')
                else:
                    raise ValueError('Paquete inesperado en publicacion')
        except (EOFError, ConnectionError, socket.timeout):
            pass
        except ValueError as error:
            ctx.mqtt_errores.append(str(error))


class BrokerPrueba(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
