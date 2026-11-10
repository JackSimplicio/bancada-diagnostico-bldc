# Firmware – ESP32

- **IDE:** Arduino IDE 2.x · **Placa:** ESP32 Dev Module (pacote "esp32" da Espressif)
- **Bibliotecas:** Adafruit ADS1X15 · INA226 (Rob Tillaart) · Adafruit GFX · Adafruit SH110X
- **Antes de gravar:** troque `AP_SENHA` (linha 64) por uma senha própria com no mínimo 8 caracteres.

## Pinos

| Função | GPIO |
|---|---|
| I²C SDA / SCL | 21 / 22 |
| Relés IN1–IN4 (ativos em nível baixo) | 16, 17, 18, 19 |
| Gate do MOSFET (100 Ω série, 100 kΩ pull-down) | 4 |
| LED de estado (330 Ω) | 26 |
| Botão de liberação (pull-up interno) | 27 |

Endereços I²C: ADS1115 `0x48` (ganho ±0,256 V) · INA226 `0x40` · OLED SH1106 `0x3C`.

## Uso

1. Grave o firmware e conecte-se à rede Wi-Fi **Bancada-PFC**.
2. Abra `http://192.168.4.1` no celular.
3. Configuração A: **Medir os 3 pares** → APROVADO libera o ensaio sob carga.
4. Configuração B: **Iniciar ensaio sob carga**; dados exportáveis em CSV.

Os mesmos comandos estão disponíveis no Monitor Serial (115200 baud). Procedimento completo em `../docs/procedimento_da_bancada_v2.pdf`.
