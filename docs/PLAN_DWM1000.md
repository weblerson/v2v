# Plano de implementação — suporte definitivo ao DWM1000

Documento de projeto e registro de decisões. Base: leitura integral de
`firmware/src/*`, `platformio.ini`, `PLAN_DISTANCE.md`, `METRICS.md`,
`monitor/*`, `pcb/pcb.kicad_sch` e do datasheet DWM1000 (Decawave, v1.7, 2016).

**O software está implementado** (ver §9). O que falta é bancada: nada do
caminho de ranging pode ser verificado sem os dois módulos ligados.

**Decisões já tomadas pela equipe** (fixadas ao longo do documento):

- **Não haverá fusão.** O DWM1000 **substitui** o GPS como fonte de
  posicionamento relativo.
- **Dois módulos DWM1000 disponíveis**, um por dispositivo.
- As correções de firmware apontadas na §9 entram **nesta mesma entrega**.

---

## 1. Estado atual do firmware — o que ajuda e o que atrapalha

### Já está pronto para receber UWB

| Ativo | Por que importa |
|---|---|
| `PositionHandler` (`position.h`) | Interface certa: o resto do sistema só pergunta "qual a distância até o peer X?". Trocar GPS→UWB não exige reescrever `main.cpp`. |
| Contrato de frescor (`POSITION_MAX_AGE_MS`) | Já documentado e implementado; o handler UWB só precisa respeitá-lo (com prazo próprio — ver D9). |
| Tabela de peers via ESP-NOW (`peers.cpp`) | **Maior ativo do projeto.** Já resolve descoberta de vizinhos, identidade (MAC) e timeout. O TWR não precisa inventar protocolo de descoberta — basta rangear contra quem já apareceu no ESP-NOW. |
| `MAX_PEERS = 8`, `portMUX` | Estrutura de concorrência já existe e o padrão está estabelecido. |
| Separação `comms` / `peers` / `position` | O UWB entra como backend, sem tocar no rádio ESP-NOW nem na classificação de movimento. |

### O que atrapalha

1. **`update()` tem contrato de não-bloqueio** (`position.h:20`), mas uma troca
   TWR completa leva ~8 ms. Chamar TWR dentro do `loop()` violaria o contrato e
   estouraria o `LOOP_INTERVAL_MS = 100`. → resolvido em **D3**.
2. **O DWM1000 não mede ângulo.** Só distância. AoA exige PDoA com duas
   antenas, recurso do DW3000. Com o GPS fora, `bearingTo()` deixa de existir
   como informação real. → resolvido em **D8**.
3. **`protocol.h:19-23` previa exatamente este cenário** e está correto: com o
   GPS fora do caminho ativo, `PositionPacket` sai do fluxo de produção.
4. **PlatformIO está quebrado nesta máquina**: `pio` aponta para um venv pipx
   sem interpretador (`bad interpreter: .../platformio/bin/python`).
   `pipx reinstall platformio` antes de qualquer compilação.

---

## 2. O que o datasheet impõe

### Elétrico — os itens que matam o módulo

| Regra | Fonte | Consequência de violar |
|---|---|---|
| `VDD3V3`/`VDDAON` máx. absoluto **4,0 V** | p.16, Tab.12 | 5 V no VCC destrói o módulo instantaneamente. |
| Alimentação nominal **2,8–3,6 V** | p.12, Tab.5 | — |
| Máx. **3,6 V** em qualquer pino digital | p.12, Tab.5 | ESP32 (3,3 V) é compatível. |
| **`RSTn` NUNCA pode ser levado a nível alto por fonte externa** | p.5 e p.17, Fig.9 (em vermelho) | Amarrar RSTn ao 3V3 é violação direta do datasheet. Só se pode puxar para LOW (dreno aberto) ou deixar em alta impedância. |
| ESD 2 kV HBM | p.16 | Pulseira antiestática; não tocar a antena. |
| **TX 140 mA / RX 160 mA** (picos) | p.12, Tab.6 | Ver orçamento de energia abaixo. |
| **Religar só com `VDDAON` > 2,3 V ou < 100 mV** | p.19, §5.2.3 | Religar com VDDAON entre 100 mV e 2,3 V faz o módulo subir **em estado desconhecido**, recuperável só com descarga total. |
| *Keep-out* da antena, ≥10 mm sem metal | p.17, §5.1, Fig.8 | Padrão de radiação degradado; alcance e precisão caem. |

> **Sobre o módulo queimado na etapa anterior:** o artigo atribui a provável
> inversão de polaridade. O datasheet abre outras hipóteses igualmente
> plausíveis, que valem verificar para não repetir o erro: (a) 5 V no VDD3V3,
> (b) RSTn amarrado ao 3V3, (c) *brownout* por corrente insuficiente, e
> (d) a hipótese de que ele **nem estivesse danificado**, apenas travado no
> estado desconhecido de §5.2.3. Com os dois módulos novos, o checklist da §7 é
> obrigatório antes de energizar.

### Orçamento de energia (bancada, alimentação USB)

```
ESP32 (WiFi TX, pico)   ~240 mA
DWM1000 (RX contínuo)    160 mA
MPU6050                   ~4 mA
                        ─────────
pico agregado           ~404 mA   ← USB 2.0 fornece 500 mA
```

Com o NEO-6M fora (~45 mA a menos), a margem melhora, mas continua apertada nos
picos de WiFi. O AMS1117 do DevKit V1 aguenta a corrente, porém dissipa ~0,5 W
(5→3,3 V a 300 mA). **Usar hub USB alimentado ou fonte externa ≥1 A nos
ensaios.** Um *brownout* durante ranging vai parecer "o DWM1000 falhou".

### SPI

| Parâmetro | Valor | Fonte |
|---|---|---|
| Modo | 0 por padrão (GPIO5/6 com pull-down interno) | p.18, Tab.13 |
| Clock máx. **antes** do CLKPLL travar | **3 MHz** | p.6, Tab.2 |
| Clock máx. depois do CLKPLL travar | 20 MHz | p.6, Tab.2 |
| MSB first, CS ativo baixo | — | p.6, §1.3 |
| `SPICSn` com pull-up interno (~60 kΩ) | p.18, Fig.10 | Relevante para a escolha do pino CS (D6). |
| `IRQ` ativo alto; recomenda-se pull-down externo | p.10, p.17 | Evita interrupções espúrias. |

A inicialização começa em ≤3 MHz (leitura do `DEV_ID`) e só depois sobe para
8–16 MHz. É item de verificação no bring-up, não de implementação.

### Calibração — o que separa "funciona" de "10 cm"

- **Antenna delay** (p.8, §2.1.3): obrigatório. A resolução do timestamp do
  DW1000 é 1/(499,2 MHz × 128) ≈ **15,65 ps ≈ 4,69 mm por LSB**. O valor típico
  16436 ticks equivale a ~77 m de "distância aparente" — ou seja, **um erro de
  213 ticks no antenna delay = 1 m de erro constante**. Sem calibrar, não há
  como atingir a meta de RMSE ≤ 1 m do `METRICS.md` §2.
- Procedimento: medir a uma distância conhecida com dois módulos e ajustar o
  delay até bater. Armazenamento em OTP exige elevar VDD3V3 a 3,8 V → ver **D7**.
- **Transmitter calibration** (p.8, §2.1.2): ajuste de potência ao limite
  regulatório (−41,3 dBm/MHz). Item de conformidade, não de funcionamento; fora
  do escopo do protótipo.

---

## 3. Decisões de arquitetura

### D1 — Biblioteca: **DW1000Ng, vendorizada**

Opções reais, verificadas no GitHub:

| Opção | ★ | Licença | Último push | Avaliação |
|---|---|---|---|---|
| `thotro/arduino-dw1000` | 574 | Apache-2.0 | jan/2024 | Canônica, mas o `DW1000Ranging` embutido impõe **descoberta própria anchor/tag com blink frames**, que colide com a nossa descoberta por ESP-NOW. A classe baixo-nível faz SPI dentro da ISR. |
| `F-Army/arduino-dw1000-ng` | 133 | MIT | nov/2023, **arquivado** | Reescrita mais limpa, com exatamente o que precisamos. |
| `Makerfabs-ESP32-UWB` | 278 | **sem licença** | out/2023 | Fork do thotro para ESP32. Sem licença = todos os direitos reservados; inadequado para trabalho acadêmico publicável. |

**Escolha: DW1000Ng**, copiada para `firmware/lib/DW1000Ng/`. Motivos
confirmados no código-fonte dela:

1. `initializeNoInterrupt(ss, rst)` — **modo polled, sem ISR**. Elimina o
   problema clássico de SPI-dentro-de-ISR no ESP32 sem gambiarra.
2. `isTransmitDone()` / `isReceiveDone()` / `clearTransmitStatus()` — API de
   polling que casa com uma task FreeRTOS.
3. `DW1000NgRanging::computeRangeAsymmetric()` + `correctRange()` — a matemática
   do DS-TWR assimétrico e a correção de viés por potência já prontas.
4. Exemplos `SimpleAntennaCalibration` / `SaveAntennaDelay` — atacam diretamente
   a exigência do §2.1.3 do datasheet.
5. `initialize(ss, irq, rst, SPIClass&)` aceita SPI customizado (VSPI).

Vendorizada (em vez de `lib_deps`) porque o repositório está **arquivado**: não
haverá correções upstream, precisaremos de pelo menos um patch (D6), e a build
fica reprodutível e auditável — o que também serve ao artigo. Licença MIT,
atribuição preservada em `firmware/lib/DW1000Ng/LICENSE`.

### D2 — Esquema TWR: **DS-TWR assimétrico, 4 quadros**

| Esquema | Quadros | Erro dominante | Veredito |
|---|---|---|---|
| SS-TWR simples | 2 | Offset de clock: 20 ppm × 400 µs de reply ⇒ **~2,4 m** | Inviável |
| SS-TWR + correção por *carrier integrator* | 2 | ~10–30 cm | Viável, exige correção manual bem sintonizada |
| **DS-TWR assimétrico** | **4** | Cancela o offset de clock em 1ª ordem ⇒ ~10 cm | **Escolhido** |

`computeRangeAsymmetric()` já implementa o cálculo, o airtime extra é
irrelevante na nossa carga, e a meta de RMSE ≤ 1 m fica com margem confortável
em vez de depender de correção de clock.

**Orçamento de tempo por troca** (CH5, 850 kbps, PRF 16 MHz, preâmbulo 256):

```
airtime/quadro ≈ 254 µs (preâmbulo) + 8 µs (SFD) + 21 µs (PHR) + ~190 µs (dados) ≈ 0,5 ms
4 quadros                                                                         ≈ 2,0 ms
2 reply delays (3000 µs — folgado para absorver jitter do WiFi)                   ≈ 6,0 ms
                                                                                 ─────────
troca completa                                                                    ≈ 8 ms
```

Com **dois carros a 10 Hz: 80 ms/s = 8 % de ocupação do canal.** Folga enorme.
`UWB_REPLY_DELAY_US` fica ajustável; dá para baixar a ~1500 µs depois,
monitorando `HPDWARN` (aviso de "tarde demais para o TX agendado").

> O instante exato do TX atrasado é gerado **pelo próprio chip**, não pelo MCU.
> Jitter de interrupção do WiFi não corrompe a medida — no máximo faz perder a
> janela e abortar a troca. Por isso o reply delay começa folgado.

### D3 — Concorrência: **task FreeRTOS dedicada**

```
core 0: WiFi / ESP-NOW (stack do IDF)
core 1: loopTask (Arduino, prio 1) + uwbRangingTask (prio 2, 4 kB)
```

- `uwbRangingTask` roda a máquina de estados TWR em estilo bloqueante — que é
  como o TWR se escreve naturalmente — com timeout duro por troca.
- Resultados vão para `UwbRange[MAX_PEERS]`, protegida por `portMUX`, no mesmo
  padrão de `peers.cpp` e `position_gps.cpp`.
- `PositionDWMHandler::update()` fica **quase vazio** → o contrato de
  não-bloqueio de `position.h:20` passa a ser respeitado por construção.
- `distanceTo()` só lê a tabela e aplica o prazo de frescor.

### D4 — Endereçamento: derivar do MAC WiFi, sem novo pacote ESP-NOW

O DW1000 usa endereços IEEE 802.15.4 (16/64 bits); o resto do sistema usa MAC
WiFi de 6 bytes.

**Escolha:** `addr16 = (mac[4] << 8) | mac[5]`, PAN ID `0xDECA`, e o MAC completo
de 6 bytes viaja **dentro do payload do quadro POLL** para vinculação e
verificação.

- Sem pacote novo no ESP-NOW ⇒ `comms.cpp` não muda por causa do UWB.
- Colisão nos 2 últimos bytes: ~1/65536 por par, e detectável porque o MAC
  completo está no POLL. Com dois dispositivos, é conferível na bancada.

**Formato dos quadros** (payload do DW1000; o CRC é automático):

```
POLL          [0x21][srcA:2][dstA:2][srcMac:6]                                 = 11 B
POLL_ACK      [0x10][srcA:2][dstA:2]                                           =  5 B
RANGE         [0x23][srcA:2][dstA:2][tPollSent:5][tPollAckRx:5][tRangeSent:5]  = 20 B
RANGE_REPORT  [0x2A][srcA:2][dstA:2][range_mm:4][fpPower_dBm_q8:2]             = 11 B
```

### D5 — Escalonamento: **regra determinística de iniciador por par**

Para cada par (A, B), **quem tem o `addr16` menor é o iniciador**; o outro é
respondedor. Como o DS-TWR termina com `RANGE_REPORT`, **os dois lados ficam
sabendo a distância** — não se perde nada.

Elimina colisão de iniciação simultânea por construção e corta o airtime pela
metade. Custo: se o UWB do nó de endereço menor falhar, aquele par não mede.
Cada nó faz *round-robin* apenas sobre os peers em que é iniciador, e fica em RX
quando ocioso. Com dois dispositivos a regra é trivial: um sempre inicia, o
outro sempre responde. Colisões residuais entre pares diferentes (cenário
futuro, >2 nós) são tratadas por timeout + retry.

### D6 — Pinagem

Pinos ocupados hoje: 2 (LED), 21/22 (I²C do MPU6050), 1/3 (USB).
**GPIO 16/17 são liberados** com a saída do NEO-6M.

| Sinal DWM1000 | Pino ESP32 | Observação |
|---|---|---|
| SPICLK (20) | GPIO 18 | VSPI padrão |
| SPIMISO (19) | GPIO 19 | VSPI padrão |
| SPIMOSI (18) | GPIO 23 | VSPI padrão |
| SPICSn (17) | GPIO 5 | GPIO5 é *strapping* (precisa estar HIGH no boot); o pull-up interno de ~60 kΩ do `SPICSn` (p.18) garante isso. |
| RSTn (3) | GPIO 27 | **Dreno aberto por software**: `pinMode(INPUT)` normalmente; para resetar, `pinMode(OUTPUT)` + `LOW` por ~1 ms e volta a `INPUT`. **Nunca `HIGH`.** |
| IRQ (22) | GPIO 26 | `INPUT_PULLDOWN`. Não usado no modo polled; reservado para evolução. |
| VDD3V3 (6,7) + VDDAON (5) | 3V3 | **Nunca 5 V.** 10 µF + 100 nF junto ao módulo. |
| GND (8,16,21,23,24) | GND | Todos. |
| EXTON, WAKEUP, GPIO0–7 | não conectar | WAKEUP pode ir a GND se não usado (p.9). |

**Patch na DW1000Ng: verificado, não foi necessário.** A suspeita era de que a
biblioteca fizesse `digitalWrite(RST, HIGH)` em algum caminho de reset — padrão
comum em bibliotecas Arduino e violação direta da p.17. Não faz: em
`DW1000Ng.cpp:1256` e `:1493` ela deixa o pino em alta impedância com
`pinMode(INPUT)`, citando explicitamente a seção do datasheet. A troca
automática de clock SPI lento→rápido na inicialização (`:1269` e `:1298`)
também respeita o limite de 3 MHz. A cópia local é literal, sem modificações
— ver `firmware/lib/DW1000Ng/VENDORING.md`.

### D7 — Antenna delay: **NVS, não OTP**

Gravar OTP exige elevar VDD3V3 a 3,8 V (p.10/p.12), perigosamente perto do
máximo absoluto de 4,0 V, um módulo por vez e de forma irreversível. Não vale o
risco — ainda mais com exatamente dois módulos e nenhum sobressalente.

**Escolha:** valor calibrado por dispositivo em NVS (`Preferences`, namespace
`uwb`, chave `antdelay`), com fallback para 16436. Reversível e sem hardware
extra.

### D8 — Bearing: **passa a ser explicitamente desconhecido**

Consequência direta da decisão de substituir o GPS: **o sistema deixa de ter
qualquer fonte de ângulo.** Isso não é contornável em software — o DWM1000 mede
tempo de voo, não direção.

O que **não** vamos fazer: manter `bearing: 0.0` como hoje (`main.cpp:93`), que
faria todos os peers aparecerem à frente do veículo no radar. Isso seria o
sistema mentindo com aparência de dado válido.

**Escolha:** representar a ausência de forma explícita ponta a ponta.

- Firmware: `bearingTo()` retorna `false`; o NDJSON ganha
  `"bearing_valid":false`.
- Monitor (Go): peer sem bearing é desenhado como **anel pontilhado no raio
  medido**, colorido pela zona (vermelho <5 m, amarelo <20 m, verde além), em vez
  de um ponto numa direção inventada. Na legenda, a coluna de ângulo mostra `—`.

Semanticamente correto: "o peer está a 3,2 m, em alguma direção" é exatamente o
que o UWB sabe. Com dois carros há um anel só, e a leitura fica limpa.

Recuperar o ângulo no futuro exige hardware: DW3000 com PDoA (duas antenas), ou
trilateração com três âncoras. Fica registrado como trabalho futuro, não como
lacuna escondida.

### D9 — Frescor: prazo próprio para o UWB

`POSITION_MAX_AGE_MS = 1500` foi dimensionado para GPS (~1 Hz). O UWB entrega a
10 Hz, e a 50 km/h (≈14 m/s) 1,5 s são 21 m de deslocamento — um valor
inutilizável para alerta de colisão.

**Escolha:** `UWB_RANGE_MAX_AGE_MS = 300` (3 ciclos de ranging perdidos),
separado da constante do GPS. `POSITION_MAX_AGE_MS` permanece em `config.h`
enquanto o backend GPS existir no repositório.

### D10 — O que acontece com o código de GPS

O `PositionGPSHandler` **permanece no repositório**, compilável, mas **não é mais
o backend ativo** — a troca é uma linha em `main.cpp`. Motivos: é o que a
abstração `PositionHandler` existe para permitir; serve de referência na
campanha de métricas (comparar UWB × GPS nas mesmas corridas, `METRICS.md` §2);
e removê-lo é irreversível sem custo de manutenção correspondente.

Consequências registradas: `PositionPacket`, `comms::broadcastRaw()` e
`comms::setPositionRxHandler()` saem do caminho de produção e passam a existir
só para o backend GPS. **Reflexo no hardware:** a PCB v3 pode dispensar o
NEO-6M (hoje `U3`, footprint `ZC323200` em `pcb/pcb.kicad_sch`) e usar o espaço
para o DWM1000 — respeitando o *keep-out* de antena da p.17.

### D11 — O que entra no lugar do ângulo: velocidade de aproximação e TTC

Perder o bearing (D8) deixa o radar com uma informação a menos. Mas o UWB
entrega, de graça, algo que o GPS nunca permitiu: **derivada da distância**.

Com ranging a 10 Hz e ruído de ~10 cm, o ajuste de mínimos quadrados sobre 5
amostras (janela de 0,5 s) resolve a velocidade de aproximação com erro da
ordem de 0,3 m/s. Daí sai o tempo até a colisão:

```
TTC = distância / velocidade_de_aproximação      (quando está fechando)
```

Isso é o que sistemas ADAS reais usam para disparar alerta, e é mais acionável
que ângulo: *"3,2 m, fechando a 1,9 m/s, contato em 1,8 s"* é uma decisão de
segurança completa; *"3,2 m a 45°"* não é.

Implementado em `track.h/.cpp`, deliberadamente **agnóstico de backend** — ele
consome a distância que o `PositionHandler` ativo produziu, então continua
funcionando se o backend GPS for selecionado para uma corrida de comparação.
Detalhes que importam:

- Mínimos quadrados, não diferença simples: com 10 cm de ruído sobre um passo
  de 100 ms, a diferença de duas amostras oscilaria ±1 m/s com os carros
  parados.
- Histórico é descartado após 1 s sem amostras. Ajustar uma reta atravessando
  um intervalo desses inventaria uma aproximação que não aconteceu.
- Abaixo de `MIN_CLOSING_SPEED_MPS = 0,3` o TTC é reportado como indisponível
  (`-1`) em vez de um número enorme e sem significado.

---

## 4. Correções de firmware incluídas nesta entrega

Encontradas na leitura do código e confirmadas para entrar junto.

### C1 — Histerese de movimento compartilhada entre peers

**Problema.** `motion.cpp:27` mantém `MotionState lastState` como **uma única
variável global para todos os peers**. Com 2+ peers, o estado de um contamina a
classificação do outro. O próprio comentário do código admite a limitação
("fine for the common 1-peer case"). Afeta diretamente a métrica de latência de
frenagem do `METRICS.md` §3.

**Agravante encontrado:** `main.cpp` chama `motion::classify()` **duas vezes por
peer por ciclo** — uma no laço do NDJSON (`main.cpp:81`) e outra dentro de
`aggregateState()` (`main.cpp:49`). Com estado interno, a máquina avança duas
vezes por ciclo.

**Correção.** Tornar `classify()` uma **função pura**:

```cpp
MotionState classify(int16_t localAccel, int16_t remoteAccel, MotionState prev);
```

O estado passa a viver por peer, em `PeerState` (`peers.h`), com um
`peers::updateMotion(mac, state)` para a escrita. Em `main.cpp`, classificar
**uma vez por peer**, guardar o resultado e derivar dele tanto o NDJSON quanto
o LED — o que elimina `aggregateState()` e o problema da dupla chamada.

### C2 — Peer que reinicia é rejeitado para sempre

**Problema.** `peers.cpp:59`:

```cpp
if (seq > table[idx].lastSeq || table[idx].lastSeq == 0) { /* aceita */ }
```

Se um peer reinicia, seu `seq` volta a 1. Como `lastSeq` já está alto e
`lastSeq == 0` só é verdade no slot recém-alocado, **nenhum pacote dele é aceito
outra vez.** O peer some do radar permanentemente até que o *nosso* dispositivo
reinicie. Num sistema de segurança veicular, é uma falha silenciosa grave.

**Correção.** Detectar reinício por dois sinais complementares:

1. **Inatividade:** se `nowMs - lastRxMillis > REMOTE_TIMEOUT_MS`, o peer
   reapareceu — aceitar e reinicializar `lastSeq`.
2. **Salto grande para trás:** se `seq < lastSeq` e a diferença for maior que
   `SEQ_REBOOT_GAP` (proposta: 100), tratar como reinício.

O sinal (1) sozinho cobre o caso real (o boot de um ESP32 com WiFi leva bem mais
que 500 ms); (2) entra como rede de segurança. A proteção contra duplicatas e
reordenação — que é a razão de existir da regra — fica preservada.

### C3 — Correções que a saída do GPS dissolve

Registradas por completude; deixam de existir com o GPS fora do caminho ativo:

- **Azimute geográfico exibido como relativo ao veículo.** `forwardAzimuth()`
  devolve 0 = Norte, mas `radar.go` desenha 0 = à frente do carro. Sem subtrair
  o *heading*, um peer atrás aparecia à frente quando o carro seguia para o sul.
  Some com D8.
- **Divergência de pinagem do GPS** entre `config.h:35-36` (`GPS_RX_PIN = 17`) e
  `monitor/README.md` (GPS TX → GPIO 16). A tabela do README é substituída pela
  do DWM1000.

### C4 — Dívidas registradas, fora do escopo desta entrega

- `comms.cpp:29,38` despacha pacotes **por tamanho do payload**. Funciona, mas
  qualquer tipo novo precisa ter tamanho único. Um byte de tipo no cabeçalho
  seria mais robusto. Não é necessário para o UWB (D4).
- `comms.cpp:88`: `esp_now_send() == ESP_OK` significa "enfileirado", não
  "entregue". Relevante quando formos instrumentar o PDR (`METRICS.md` §1).

---

## 5. Desenho do código

### Arquivos novos

```
firmware/lib/DW1000Ng/…              biblioteca vendorizada (MIT), cópia literal
firmware/lib/DW1000Ng/VENDORING.md   origem, commit e registro de modificações
firmware/src/uwb_twr.h/.cpp          quadros, máquina de estados DS-TWR, escalonamento
firmware/src/track.h/.cpp            velocidade de aproximação e TTC (D11)
firmware/src/tools/uwb_probe.cpp     env separado: leitura crua do DEV_ID (E1)
firmware/src/tools/uwb_calib.cpp     env separado: calibração de antenna delay (E4)
```

### Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `config.h` | bloco `--- UWB ---` (pinos, PAN ID, cadência, timeouts, reply delay, antenna delay), `UWB_RANGE_MAX_AGE_MS`, `SEQ_REBOOT_GAP`, constantes de TTC |
| `position_dwm.h/.cpp` | implementação real sobre `uwb_twr` (substitui os no-ops) |
| `main.cpp` | backend passa a ser o DWM; classificação uma vez por peer (C1); `aggregateState()` removida; NDJSON com `bearing_valid`, `closing` e `ttc` |
| `motion.h/.cpp` | `classify()` vira função pura recebendo o estado anterior (C1) |
| `peers.h/.cpp` | `lastMotion` por peer + `updateMotion()` (C1); detecção de reinício (C2) |
| `protocol.h` | `PositionPacket` documentado como exclusivo do backend GPS |
| `platformio.ini` | `[env]` comum + `uwb_probe` e `uwb_calib` com `build_src_filter` |
| `monitor/protocol/protocol.go` | `BearingValid`, `Closing`, `TTC` |
| `monitor/radar/radar.go` | anel para peer sem bearing; escala radial em raiz quadrada; colunas de closing/TTC; correção do `setStr` por rune |
| `monitor/source/serial.go` | `staleAfter` 1500 → 500 ms, alinhado ao frescor do UWB |
| `monitor/source/mock.go` | `closing`/`ttc` simulados; peers como anéis, igual à produção |
| `monitor/README.md` | ligação do DWM1000 no lugar do NEO-6M; protocolo e display atualizados |

### Máquina de estados (task de ranging)

```
                    ┌──────────────────────────────────────┐
                    ▼                                      │
  IDLE/RX ──(POLL recebido)──► RESPOND ──► envia POLL_ACK ──┤
     │                            │                         │
     │                       (RANGE recebido)               │
     │                            ▼                         │
     │                    calcula distância                 │
     │                    envia RANGE_REPORT ───────────────┤
     │                                                      │
     └──(minha vez, sou iniciador do peer P)──► POLL ──► aguarda POLL_ACK
                                                  │              │
                                              timeout        RANGE (TX atrasado)
                                                  │              │
                                                  │        aguarda RANGE_REPORT
                                                  │              │
                                                  ▼              ▼
                                            retry / próximo   grava distância
```

Timeout por troca: `UWB_EXCHANGE_TIMEOUT_MS = 30`. Após 3 falhas seguidas, o
peer é despriorizado no *round-robin* até voltar a responder.

### Formato do NDJSON

```jsonc
// antes
{"mac":"44:17:93:4C:7F:90","distance":12.3,"bearing":45.0,"state":"BRAKING"}
// depois
{"mac":"44:17:93:4C:7F:90","distance":3.24,"bearing":0.0,"bearing_valid":false,
 "closing":1.85,"ttc":1.8,"state":"BRAKING"}
```

`bearing` permanece no formato por compatibilidade com o parser atual do
monitor; `bearing_valid` é que carrega a verdade. `ttc` vale `-1` quando o par
não está se aproximando rápido o bastante para o número significar algo.

---

## 6. Bring-up em estágios

Cada estágio é um portão: **não avança sem passar**.

| # | Estágio | Critério de aceite |
|---|---|---|
| **E0** | Conferência elétrica com o módulo **desligado** | Continuidade; 3,3 V confirmado no multímetro; RSTn sem caminho para 3V3; GND comum |
| **E1** | `uwb_probe` — leitura do `DEV_ID` a 2 MHz | Lê **`0xDECA0130`**. É o go/no-go do módulo. `0x00000000` ou `0xFFFFFFFF` indicam fiação/alimentação, não módulo morto. Rodar **nos dois módulos**. |
| **E2** | TX/RX simples entre as duas placas | Uma envia quadro, a outra imprime o conteúdo |
| **E3** | DS-TWR par-a-par, distância bruta | Valor estável, desvio-padrão < 10 cm com os módulos parados |
| **E4** | Calibração do antenna delay a 5,00 m de trena | Erro médio < 10 cm; valor gravado em NVS **em cada dispositivo** |
| **E5** | Integração como `PositionDWMHandler` + radar com anéis | Monitor exibe a distância UWB |
| **E6** | Campanha de métricas | RMSE por faixa (0–5 / 5–20 / >20 m) conforme `METRICS.md` §2 |

E1 é o estágio de maior valor: responde em minutos, e com os dois módulos, a
pergunta "o hardware está bom?" — antes de escrever qualquer lógica de ranging.

Para o **E6**, o backend GPS preservado (D10) permite rodar as mesmas corridas
com os dois backends e produzir a comparação UWB × GPS que fecha a Tabela 1 do
artigo.

---

## 7. Checklist anti-queima (colar na bancada)

Com **dois módulos e nenhum sobressalente**, este checklist é obrigatório.

- [ ] Alimentação **3,3 V** medida com multímetro **antes** de plugar o módulo. Nunca 5 V (máx. abs. 4,0 V).
- [ ] GND comum entre ESP32 e DWM1000.
- [ ] **RSTn sem nenhum caminho para 3V3.** Só GPIO em alta impedância.
- [ ] 10 µF + 100 nF de desacoplamento junto aos pinos de alimentação.
- [ ] Fonte externa ≥1 A ou hub USB alimentado (pico agregado ~400 mA).
- [ ] Pulseira antiestática; não tocar na antena cerâmica (ESD 2 kV HBM).
- [ ] ≥10 mm livres de metal em volta da antena; nada acima nem abaixo dela.
- [ ] Nunca plugar/desplugar o módulo com o circuito energizado.
- [ ] Se um módulo "morrer": **desligar tudo e esperar VDDAON cair abaixo de 100 mV** antes de religar (p.19, §5.2.3). Pode não estar morto.
- [ ] SPI a ≤3 MHz até o `DEV_ID` ler certo; só depois subir para 8–16 MHz.
- [ ] Fazer E0/E1 **com um módulo de cada vez**, para que um erro de fiação não leve os dois.

---

## 8. Riscos

| Risco | Prob. | Impacto | Mitigação |
|---|---|---|---|
| Queimar módulo (sem sobressalente) | Média | **Crítico** | Checklist §7; E0/E1 um módulo por vez, antes de qualquer código |
| DW1000Ng com bug em ESP32 | Média | Médio | Vendorizada ⇒ corrigível; modo polled evita o problema mais comum |
| RMSE não bater ≤1 m | Baixa | Alto | Calibração de antenna delay (E4) + `correctRange()`; DS-TWR já elimina o erro de clock |
| Brownout confundido com falha do UWB | **Alta** | Médio | Fonte externa desde o E1; registrar tensão nos ensaios |
| Perda do bearing empobrecer o radar | Certa | Médio | Aceita e explicitada (D8); anel em vez de direção falsa |
| Alcance UWB < ESP-NOW | Média | Médio | Peer aparece na tabela sem distância; `main.cpp` já não emite linha sem distância. Medir no E6. |
| PlatformIO quebrado bloquear a build | Certa | Baixo | `pipx reinstall platformio` antes de começar |

**Sobre o último risco de alcance:** o ESP-NOW alcança bem mais que o UWB a
850 kbps. Peers descobertos por rádio mas fora do alcance de ranging ficarão sem
distância e, pela regra atual de `main.cpp:84`, simplesmente não aparecem no
radar. É o comportamento seguro (nada é inventado), mas precisa ser medido no E6
para sabermos a que distância o radar começa a enxergar.

---

## 9. Status

### Implementado

Todo o software das decisões D1–D11 e das correções C1–C2 está escrito e
compila. Os três ambientes do PlatformIO fecham sem erro:

| Ambiente | Uso |
|---|---|
| `esp32doit-devkit-v1` | firmware de produção |
| `uwb_probe` | estágio E1 — leitura crua do `DEV_ID` |
| `uwb_calib` | estágio E4 — calibração do antenna delay |

O monitor em Go compila e passa no `go vet`.

### Falta — tudo depende de hardware

Nada disso pode ser verificado sem os módulos na bancada:

1. **E0 + E1** — conferência elétrica e `DEV_ID = 0xDECA0130`, **um módulo por
   vez**. Portão de hardware; nada adiante faz sentido antes disso.
2. **E2 → E3** — primeira troca de quadros e primeiro ranging bruto entre as
   duas placas.
3. **E4** — calibração do antenna delay a uma distância de trena, `cal <m>` em
   cada placa.
4. **E5** — radar exibindo distância real.
5. **E6** — campanha de métricas UWB × GPS, alimentando `METRICS.md` §2.

### Nota sobre o ambiente de build

O PlatformIO da máquina de desenvolvimento está quebrado por um motivo alheio
ao projeto: os venvs do `pipx` e do próprio PlatformIO apontam para um Python
3.14.4 instalado via `mise` que não existe mais (o `mise` tem 3.14.7). Sintoma:
`bad interpreter`. O `pipx reinstall` não resolve porque o próprio `pipx` está
quebrado do mesmo jeito. Reconstruir os venvs com o Python do sistema resolve:

```
python3 -m venv ~/.platformio/penv-new && \
  ~/.platformio/penv-new/bin/pip install platformio
```

As builds validadas aqui usaram um venv desse tipo com
`PLATFORMIO_CORE_DIR=~/.platformio`, reaproveitando o toolchain já baixado.
