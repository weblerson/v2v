# Placa V2V — ESP32 DevKit v1 + DWM1000 + GY-521

Placa de 2 camadas, **70 × 60 mm**, FR4 1,6 mm, que carrega os três módulos do
protótipo. Todo o mapeamento de pinos vem de `firmware/src/config.h` — ver
`docs/PLAN_DWM1000.md` para as decisões de projeto do firmware.

## Mapeamento

| Sinal | ESP32 (pino do header) | Destino | Camada predominante |
|---|---|---|---|
| `UWB_SCK`  | GPIO18 (9)  | DWM1000 pino 20 (SPICLK)  | B.Cu |
| `UWB_MISO` | GPIO19 (10) | DWM1000 pino 19 (SPIMISO) | B.Cu |
| `UWB_MOSI` | GPIO23 (15) | DWM1000 pino 18 (SPIMOSI) | B.Cu |
| `UWB_CS`   | GPIO5 (8)   | DWM1000 pino 17 (~SPICS)  | B.Cu |
| `UWB_RST`  | GPIO27 (25) | DWM1000 pino 3 (~RST)     | B.Cu |
| `UWB_IRQ`  | GPIO26 (24) | DWM1000 pino 22 (IRQ)     | F.Cu/B.Cu |
| `I2C_SDA`  | GPIO21 (11) | GY-521 SDA                | B.Cu |
| `I2C_SCL`  | GPIO22 (14) | GY-521 SCL                | B.Cu |
| `+3V3`     | pino 3V3 (1) | DWM1000 5/6/7 + C1 + C2  | B.Cu → F.Cu |
| `+5V`      | VIN (30)    | GY-521 VCC                | F.Cu |
| `GND`      | 2 e 29      | plano nas duas faces      | — |

O **DWM1000 é 3,3 V** (máximo absoluto 4,0 V, datasheet Tabela 12). Ele sai do
regulador da própria DevKit, nunca do VIN. O GY-521 fica no 5 V porque tem LDO
próprio a bordo.

## Pontos de atenção na montagem

- **Duas áreas de keep-out de antena** não podem receber cobre, metal, bateria
  ou espaçador — nem por cima nem por baixo. A do DWM1000 está marcada
  `KEEP-OUT` no silk e encosta na borda direita (datasheet §5.1, `d ≥ 10 mm`);
  a do ESP32 fica na ponta inferior do módulo, que projeta 2,5 mm para fora da
  placa de propósito.
- **C2 (100 nF)** é o único capacitor da placa e fica o mais perto possível
  dos pinos 6/7. É o bypass que o datasheet desenha na Figura 9. Nenhum pino
  do módulo é do tipo `PD` (Power Decoupling) — o desacoplamento que o chip
  DW1000 nu exigiria já está dentro do módulo —, então este é o único externo
  necessário. Um bulk adicional foi avaliado e descartado: a DevKit já tem o
  dela na saída do regulador.
- O AMS1117 da DevKit passa a alimentar também o DWM1000 (~0,7 W dissipados).
  Se aparecer brownout, alimente o 3V3 por um regulador externo — brownout se
  parece muito com "o UWB não responde".
- 4 furos M3 (H1–H4) nos cantos + os furos próprios dos módulos.

## Fabricação

`fab/gerbers-v2v-revA.zip` está pronto para enviar. Contém os Gerbers X2 das
9 camadas, os Excellon separados (PTH/NPTH) e os mapas de furação.

- Menor trilha: 0,25 mm · menor isolação: 0,2 mm
- Menor furo: 0,3 mm (vias de sinal) · via padrão 0,6/0,3 mm
- Acabamento/cor: livre (HASL serve)

`fab/pcb-bom.csv` e `fab/pcb-pos.csv` acompanham para a montagem.

## Regerar

```sh
kicad-cli pcb drc --schematic-parity --refill-zones --save-board pcb.kicad_pcb
kicad-cli pcb export gerbers --check-zones --no-protel-ext --subtract-soldermask \
  --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
  -o fab/gerbers pcb.kicad_pcb
kicad-cli pcb export drill --format excellon --excellon-units mm \
  --excellon-separate-th --generate-map --map-format gerberx2 -o fab/gerbers pcb.kicad_pcb
```

As bibliotecas dos módulos estão versionadas em `libs/` com tabelas de projeto
(`fp-lib-table`, `sym-lib-table`), então o projeto abre em qualquer máquina.
