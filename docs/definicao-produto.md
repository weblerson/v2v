---
title: "V2V Proximity System: Definição de Produto e Serviço"
author: "William Yuri Ribeiro Silva"
date: "10 de maio de 2026"
lang: pt-BR
---

# V2V Proximity System: uma rede de consciência entre veículos

## Resumo

O **V2V Proximity System** é uma plataforma embarcada de comunicação
veículo-a-veículo (do inglês *Vehicle-to-Vehicle*, ou simplesmente V2V) que
permite que automóveis próximos uns dos outros troquem, em tempo real, dados
de movimento e posicionamento, sem depender de infraestrutura externa, sem
servidores na nuvem e sem latência de rede celular. A solução é composta por
três camadas integradas: um **firmware** executado em microcontroladores
ESP32 instalados em cada veículo, uma **placa de circuito impresso (PCB)**
dedicada que abriga o conjunto de sensores e o rádio, e um **aplicativo
monitor** em modo texto que apresenta ao condutor — ou ao avaliador técnico,
em cenário de protótipo — uma visão tipo *radar* dos veículos vizinhos, com
distância, direção e estado de movimento. Este documento descreve, em formato
de artigo, o que o produto oferece e qual problema resolve.

## 1. Contexto e problema

Acidentes de trânsito em rodovias e perímetros urbanos têm, com frequência,
uma causa comum: o tempo de reação humano é insuficiente quando o veículo
imediatamente à frente realiza uma frenagem brusca, quando um veículo
aproxima-se em ângulo morto ou quando um obstáculo está oculto por outro
automóvel. Os atuais sistemas de assistência ao motorista (ADAS) — câmeras,
radares de ondas milimétricas, LiDAR — funcionam bem em linha de visada e em
condições favoráveis, mas degradam em situações de baixa visibilidade,
neblina, chuva intensa, curvas fechadas, e simplesmente não enxergam aquilo
que está atrás do veículo da frente. Em outras palavras, a percepção de cada
veículo termina onde termina o alcance dos seus próprios sensores.

A comunicação V2V resolve essa limitação por um caminho diferente: em vez de
**ver** o outro carro, o veículo **ouve** o outro carro. Cada automóvel
participante anuncia continuamente seu estado de movimento (aceleração,
frenagem, posição) por rádio de curto alcance, e cada vizinho dentro do
alcance recebe esse anúncio quase instantaneamente. O resultado é uma rede
cooperativa em que a consciência situacional de um se torna a consciência
situacional de todos.

O **V2V Proximity System** é uma implementação concreta, de baixo custo e de
arquitetura aberta dessa ideia, voltada inicialmente para uso em protótipos
acadêmicos, pesquisa aplicada e demonstrações didáticas, com caminho técnico
claramente definido para evolução em direção a um produto embarcado em
veículos reais.

## 2. O que é oferecido

O produto entrega ao cliente um sistema completo, ponta a ponta, composto
pelos seguintes itens:

### 2.1. Hardware embarcado (a PCB e os sensores)

Cada veículo participante recebe uma **placa eletrônica dedicada**,
projetada em KiCad e disponível neste repositório (diretório `pcb/`), que
integra:

- um microcontrolador **ESP32 DevKit V1**, responsável por toda a lógica do
  nó — radio, leitura de sensores e formatação dos dados para o aplicativo
  monitor;
- um sensor de movimento **MPU6050** (acelerômetro de três eixos +
  giroscópio), conectado por barramento I²C nos pinos GPIO 21 (SDA) e
  GPIO 22 (SCL), responsável por detectar acelerações longitudinais e,
  portanto, classificar o estado de movimento como **ocioso (IDLE)**,
  **acelerando (ACCELERATING)** ou **freando (BRAKING)**;
- um módulo GPS **NEO-6M**, conectado à segunda UART do ESP32 (GPIO 16 e
  GPIO 17), responsável por fornecer latitude e longitude em WGS84;
- LED de sinalização e os componentes passivos de suporte.

A placa foi pensada para ser fisicamente instalada no veículo, alimentada
por um barramento de 5 V, com fixação compatível com o ambiente automotivo.

### 2.2. Firmware em tempo real

O firmware (diretório `firmware/`, escrito em C++ sobre o framework Arduino
e construído com PlatformIO) implementa o coração da solução. Suas
responsabilidades são:

- **Difusão por ESP-NOW.** O ESP32 emprega o protocolo proprietário ESP-NOW
  da Espressif, que opera sobre a camada física 802.11 a 2,4 GHz, mas sem
  exigir associação a um ponto de acesso. Pacotes pequenos e de tamanho
  fixo são difundidos para o endereço de broadcast (`FF:FF:FF:FF:FF:FF`) e
  recebidos por todos os ESP32 vizinhos no mesmo canal. Isso elimina a
  necessidade de roteadores, operadoras de celular ou qualquer
  infraestrutura externa: dois veículos só precisam estar dentro do alcance
  de rádio um do outro.
- **Detecção de movimento.** O MPU6050 é amostrado continuamente. O
  firmware aplica uma média móvel de cinco amostras sobre o eixo de
  aceleração longitudinal, compara o valor à linha-base obtida na
  calibração de partida e classifica o movimento com histerese, evitando
  oscilação ao redor do limiar.
- **Posicionamento e ranging.** Sentenças NMEA do GPS são decodificadas
  pela biblioteca TinyGPSPlus; ao haver fix válido, o veículo passa a
  difundir sua latitude e longitude num *PositionPacket*. O receptor
  calcula a distância pela fórmula de Haversine e o azimute (rumo) por
  trigonometria esférica. Toda informação tem prazo de validade: dados com
  mais de 1,5 s são tratados como inexistentes — em um veículo em
  movimento, uma "última posição conhecida" desatualizada é potencialmente
  perigosa.
- **Saída padronizada.** Para cada vizinho cuja distância pôde ser
  computada, o firmware emite, pela porta serial USB, uma linha JSON
  (formato NDJSON) descrevendo o MAC do veículo, a distância em metros,
  o rumo em graus e o estado de movimento. Esse fluxo alimenta o
  aplicativo monitor.
- **Arquitetura de posicionamento intercambiável.** O firmware define uma
  interface abstrata `PositionHandler` com duas implementações: a atual,
  baseada em GPS, e uma reservada para um módulo **UWB Decawave
  DWM1000/DWM3000**, que mede distância ponto-a-ponto por *Two-Way
  Ranging* com precisão de aproximadamente 10 cm. A troca da
  implementação não requer alteração nos chamadores, viabilizando uma
  transição suave para tecnologia automotiva de classe superior.

### 2.3. Aplicativo monitor (radar em modo terminal)

O monitor (diretório `monitor/`, escrito em Go com o framework
`bubbletea`) é a interface humana do sistema. Ele lê o fluxo NDJSON
emitido pelo ESP32 — ou um conjunto de dados simulados, em modo `--mock`,
para demonstração sem hardware — e desenha, no terminal, um **radar
circular de 360 graus** centrado no próprio veículo:

- a **zona vermelha** (anel interno) sinaliza vizinhos a menos de 5 m;
- a **zona amarela** (anel intermediário), entre 5 m e 20 m;
- a **zona verde** (anel externo), além de 20 m.

Cada vizinho aparece como um ponto colorido na posição angular
correspondente ao seu rumo, com uma legenda inferior listando MAC,
distância, rumo e estado de movimento. O radar redesenha-se a cada
100 ms, oferecendo ao avaliador uma visão imediata e densa da
vizinhança veicular.

## 3. Qual problema o produto resolve

Em termos diretos, o V2V Proximity System resolve **a opacidade entre
veículos**. Ele entrega ao motorista — ou ao sistema autônomo do veículo
— três informações que hoje são caras de obter por sensores ópticos e
impossíveis quando há obstrução visual:

1. **Existe alguém perto de mim?** Cada nó conhece, em tempo real, a
   lista de vizinhos ativos cuja transmissão chegou no último meio
   segundo. Vizinhos silenciosos são automaticamente removidos.
2. **Onde, exatamente, ele está?** O par distância–rumo permite
   localizar cada vizinho num círculo de 360°, sem depender de linha de
   visada nem de iluminação adequada.
3. **O que ele está fazendo?** A classificação cooperativa de estados
   (frear/acelerar/ocioso), trocada diretamente entre os veículos,
   permite que o motorista atrás saiba que o motorista da frente acaba
   de frear **antes** que a luz de freio seja sequer processada
   visualmente — e mesmo quando outro veículo a oculta.

Esses três sinais, combinados, antecipam decisões que hoje dependem de
tempo de reação humano. Esse é o ganho funcional central. Em torno dele,
o produto oferece também ganhos auxiliares relevantes:

- **Independência de infraestrutura.** A solução não exige cobertura
  celular, não envia dados para a nuvem e não impõe latência de rede de
  longa distância. Funciona em rodovias remotas e em túneis (na medida
  do alcance do rádio).
- **Privacidade por construção.** Os pacotes V2V transportam apenas o
  endereço MAC do dispositivo, o número de sequência e os dados
  cinemáticos do instante. Nenhum dado pessoal do condutor trafega na
  rede.
- **Custo acessível.** O conjunto ESP32 + MPU6050 + NEO-6M tem custo
  unitário de poucas dezenas de reais, viabilizando experimentação em
  larga escala, frotas de pesquisa e instalação retroativa em veículos
  já em uso.
- **Arquitetura aberta e evoluível.** O caminho de evolução para UWB
  (DWM1000/DWM3000), com ranging de precisão centimétrica, já está
  contemplado no projeto sem reescrita do restante do sistema.

## 4. Para quem é este produto

O V2V Proximity System se posiciona, neste momento, como uma plataforma
de **prova de conceito e pesquisa aplicada** voltada para três públicos
distintos:

- **Instituições de ensino e laboratórios universitários** que precisam
  de uma base concreta, de baixo custo e auditável para investigar
  comunicação cooperativa entre veículos, fusão de sensores e
  algoritmos de alerta antecipado.
- **Equipes de protótipo automotivo** que desejem demonstrar a
  viabilidade de funcionalidades V2V antes de investir em hardware
  certificado (rádios DSRC/C-V2X), validando comportamento, ergonomia
  e benefício percebido com um custo de hardware marginal.
- **Frotas e operadores logísticos** dispostos a experimentar uma camada
  adicional de segurança ativa em comboios e operações controladas,
  onde a homogeneidade dos veículos torna a adoção do mesmo dispositivo
  em todos eles especialmente simples.

## 5. Limites atuais e roteiro de evolução

É importante delimitar o que o produto **não** é hoje. Ele não é um
dispositivo automotivo homologado, não substitui sistemas ADAS
certificados, não opera nas faixas de rádio reservadas a V2X
(5,9 GHz) e tem alcance, robustez e garantias de tempo real
compatíveis com a sua origem em hardware de prototipagem. A evolução
prevista para superar essas limitações inclui: (i) substituição do
posicionamento GPS por UWB com ranging centimétrico; (ii) integração
com a CAN do veículo para captura de sinais reais de freio,
acelerador e velocidade; (iii) migração da camada de rádio para
módulos C-V2X quando justificável; e (iv) projeto de um invólucro
automotivo definitivo, com alimentação isolada e proteção ambiental.

## 6. Conclusão

O V2V Proximity System oferece, de forma tangível, a experiência de
um trânsito em que cada veículo enxerga não apenas o que está à sua
frente, mas o que os outros veículos enxergam — e o que estão fazendo.
A entrega é composta por uma placa eletrônica dedicada, um firmware
em tempo real escrito sobre ESP32, e um aplicativo de monitoramento
em forma de radar de terminal. O problema que se resolve é simples
de enunciar e difícil de atacar por outros meios: **eliminar a
opacidade entre veículos próximos**, antecipando ao motorista
informações que, até hoje, dependem do tempo de reação humano e da
sorte de haver linha de visada. Trata-se de uma plataforma aberta,
de baixo custo, com arquitetura preparada para evoluir até um
produto embarcado pleno, e que, já em sua forma atual, viabiliza
pesquisa, ensino e demonstrações de uma das ideias mais promissoras
da próxima geração de segurança veicular.
