#!/usr/bin/env python3
"""
Ponte USB serial: Flask (127.0.0.1:5000/status) -> ESP32 CYD (COM7 @115200).

OPCIONAL. O firmware ChargeGridCYD roda sozinho, simulando os dados
localmente. Esta ponte so e necessaria quando voce quiser alimentar a tela
com os numeros do servidor Flask do projeto.

Uso:
    py ponte-usb.py            # detecta a porta CH340 automaticamente
    py ponte-usb.py COM7       # forca a porta

Requisitos: pyserial  (py -m pip install pyserial)

IMPORTANTE: a ponte segura a porta COM. Encerre-a (Ctrl+C) antes de gravar
o firmware, senao o upload falha com "porta ocupada".
"""

import json
import sys
import time
import urllib.error
import urllib.request

import serial
from serial.tools import list_ports

URL      = "http://127.0.0.1:5000/status"
BAUD     = 115200
INTERVAL = 5.0     # segundos entre pacotes (o firmware espera ~5 s)


def achar_porta():
    """Procura o CH340 do CYD (VID 1A86 / PID 7523)."""
    for p in list_ports.comports():
        if (p.vid, p.pid) == (0x1A86, 0x7523):
            return p.device
    for p in list_ports.comports():          # fallback: primeira COM que existir
        return p.device
    return None


def buscar_status():
    with urllib.request.urlopen(URL, timeout=3) as r:
        return json.loads(r.read().decode("utf-8"))


def main():
    porta = sys.argv[1] if len(sys.argv) > 1 else achar_porta()
    if not porta:
        print("Nenhuma porta serial encontrada. Ligue a placa no USB.")
        return 1

    print(f"[ponte] abrindo {porta} @ {BAUD}")
    with serial.Serial(porta, BAUD, timeout=0.2) as ser:
        time.sleep(2.0)                      # espera o reset do ESP32
        ser.reset_input_buffer()

        while True:
            try:
                dados = buscar_status()
            except (urllib.error.URLError, OSError, json.JSONDecodeError) as e:
                print(f"[ponte] Flask indisponivel ({e}); aguardando...")
                time.sleep(INTERVAL)
                continue

            linha = json.dumps(dados, separators=(",", ":")) + "\n"
            ser.write(linha.encode("utf-8"))
            ser.flush()
            print(f"[ponte] -> {len(linha):3d} B  "
                  f"{dados.get('potencia_total_kw')} kW / "
                  f"{dados.get('temperatura_c')} C")

            # le a confirmacao devolvida pela placa
            fim = time.time() + 1.5
            while time.time() < fim:
                eco = ser.readline().decode("utf-8", "replace").strip()
                if eco:
                    print(f"[placa] {eco}")
                    if eco.startswith(("OK", "ERRO")):
                        break

            time.sleep(INTERVAL)


if __name__ == "__main__":
    try:
        sys.exit(main() or 0)
    except KeyboardInterrupt:
        print("\n[ponte] encerrada")
