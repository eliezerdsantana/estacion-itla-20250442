"""Ejecuta rutas del cliente C bajo Valgrind, solo con servidores locales."""
from pathlib import Path
import shlex
import shutil
import sys
import tempfile
import time
import unittest

import verificar_analisis as analisis
import verificar_envio as envio
import verificar_alarmas as alarmas


class ServidorMemoria(envio.Servidor):
    def do_POST(self):
        # La instrumentacion puede demorar el arranque del proceso. Las caidas
        # del simulador se cuentan desde su primer POST, no desde su lanzamiento.
        ctx = self.server.contexto
        if not ctx.eventos:
            ctx.inicio = time.monotonic()
        super().do_POST()


def main():
    if sys.platform != 'linux' or shutil.which('valgrind') is None:
        print('Esta comprobacion requiere Linux y Valgrind.', file=sys.stderr)
        return 2
    ejecutable = envio.EXE.resolve()
    if not ejecutable.is_file():
        print('Primero compila el programa con make.', file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix='itla-memoria-') as temporal:
        carpeta = Path(temporal)
        lanzador = carpeta / 'cliente-valgrind'
        comando = ['valgrind', '--tool=memcheck', '--leak-check=full',
                   '--show-leak-kinds=definite,indirect',
                   '--errors-for-leak-kinds=definite,indirect',
                   '--error-exitcode=99',
                   '--log-file=' + str(carpeta / 'valgrind-%p.log'),
                   str(ejecutable)]
        lanzador.write_text('#!/bin/sh\nexec ' + shlex.join(comando) + ' "$@"\n')
        lanzador.chmod(0o700)
        for modulo in (analisis, envio, alarmas):
            modulo.EXE = lanzador
        envio.Servidor = ServidorMemoria

        casos = [
            analisis.AnalisisITLA('test_regresion_descarga_y_analisis_juntos'),
            envio.EnvioITLA('test_02_dos_caidas_no_detienen_muestreo_y_recuperan_cola'),
            envio.EnvioITLA('test_03_respuesta_perdida_se_repite_y_200_duplicada_confirma'),
            alarmas.AlarmasITLA('test_03_reconexion_y_retenida_no_duplican_ack'),
            alarmas.AlarmasITLA('test_04_respuesta_perdida_y_409_confirman_la_misma_alarma'),
        ]
        resultado = unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(casos))
        informes = sorted(carpeta.glob('valgrind-*.log'))
        informes_correctos = bool(informes)
        print('\nProcesos C analizados por Valgrind:', len(informes))
        for informe in informes:
            texto = informe.read_text(errors='replace')
            correcto = 'ERROR SUMMARY: 0 errors' in texto
            informes_correctos = informes_correctos and correcto
            print('\n' + informe.name)
            if not correcto:
                print(texto)
                continue
            for linea in texto.splitlines():
                if any(campo in linea for campo in (
                    'in use at exit:', 'total heap usage:', 'definitely lost:',
                    'indirectly lost:', 'possibly lost:', 'still reachable:',
                    'All heap blocks were freed', 'ERROR SUMMARY:')):
                    print(linea)
        return 0 if resultado.wasSuccessful() and informes_correctos else 1


if __name__ == '__main__':
    sys.exit(main())
