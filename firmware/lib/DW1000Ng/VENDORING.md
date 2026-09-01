# DW1000Ng — biblioteca vendorizada

Fonte: https://github.com/F-Army/arduino-dw1000-ng
Commit: `e83532002311d0f9028513312b214f8ee1fe382b` (2023-11-09)
Licença: MIT (ver `LICENSE.md`)

## Por que está copiada no repositório em vez de `lib_deps`

O repositório upstream está **arquivado** desde nov/2023 — não receberá
correções. Vendorizar garante build reprodutível, permite patch local se
necessário e deixa a dependência auditável junto do resto do código.

## Modificações locais

**Nenhuma até o momento.** Os arquivos são cópia literal do commit acima.

Vale registrar o que foi verificado e *não* precisou de patch: a biblioteca
já trata o pino RSTn corretamente (`DW1000Ng.cpp:1256` e `:1493`), deixando-o
em alta impedância com `pinMode(INPUT)` em vez de acionar nível alto — citando
explicitamente a seção do datasheet que proíbe isso. A troca automática de
clock SPI lento→rápido durante a inicialização (`DW1000Ng.cpp:1269` e `:1298`)
também respeita o limite de 3 MHz antes do CLKPLL travar.

Qualquer alteração futura deve ser listada aqui, com o motivo.

## Como é usada neste projeto

Apenas em modo *polled* (`initializeNoInterrupt`), sem ISR — ver
`docs/PLAN_DWM1000.md`, decisões D1 e D3. A máquina de estados de Two-Way
Ranging é nossa, em `firmware/src/uwb_twr.cpp`; da biblioteca usamos o driver
de registradores, os timestamps e `DW1000NgRanging::computeRangeAsymmetric()`.
