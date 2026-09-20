# ChargeGrid CYD — eletroposto interativo embarcado

Porte de três telas do Claude Design para firmware C++ rodando dentro do **CYD 2.8"**
(placa laranja, painel `TPM408-2.8`, ILI934x **nativo 320×240** + toque resistivo XPT2046).

O aparelho tem **duas personas**:

| Persona | Telas | Origem |
|---|---|---|
| **Motorista** (padrão no boot) | Sessão · Pagamento | `ChargeGrid Sessao.dc.html` · `ChargeGrid Pagamento.dc.html` |
| **Operador** (dono do eletroposto) | Dashboard (página única) | `ChargeGrid Dashboard.dc.html` |

---

## 1. As telas

### Motorista — tela cheia, sem rolagem

O eletroposto **liga direto na tela de Sessão**: é o que o usuário final vê quase sempre.
Nada de aba escondida, nada para procurar — cada tela tem exatamente um botão grande de
ação na base.

```
┌─ SESSÃO ──────────────────────────────┐
│ ‹ GOODWE │ CARREGANDO  VILA OLÍMPIA·C01 │
│    ╭─────╮      FALTAM                 │
│   │ RESTANTE│     22,8 kWh              │
│   │   31    │     24,8 kWh já carregados│
│   │   min   │    ────────────────── │
│    ╰─────╯      CUSTO ATUAL            │
│   (62% DA BATERIA)  R$ 58,28          │
│ ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░░░░░░░ │
│ Início 19:08      Conclusão 19:39     │
│ ┌───── ENCERRAR E PAGAR ──────────┐ │
└──────────────────────────────────┘
```

**Pagamento** tem cinco estados, um por vez, sempre com um botão único na base:

| Estado | O que aparece |
|---|---|
| Escolha | Total a pagar em destaque + lista de 4 linhas de largura cheia (PIX, Google Pay, Apple Pay, PayPal) |
| PIX | QR Code, contagem de expiração, "Copiar código", "Simular confirmação" |
| Apple/Google Pay | Anéis NFC pulsando + "Simular aproximação" |
| PayPal | E-mail da conta + "Continuar para o PayPal" |
| Aprovado | Check verde, autorização, recibo, repasse D+1, "Nova sessão" |

O **‹ no canto superior esquerdo** volta um passo: do método escolhido para a lista de
métodos, e da lista de volta para a sessão. Na tela de Sessão, esse mesmo ‹ é a porta de
entrada do dono para o dashboard.

### Operador — uma página só, rolável

Não há mais abas. Tudo vive numa rolagem única (~1.220 px), do operacional para o
gerencial, e o cabeçalho tem apenas duas pílulas:

```
┌────────────────────────────────────┐
│ GOODWE │ TORRE DE CONTROLE  t+3s [❚❚] │
│ ( Dashboard )            ( Motorista ) │
└────────────────────────────────────┘
```

Ordem das seções: anel de potência + vitais · curva 24 h · conectores C01–C04 · sessão
em curso · sessões recentes · sessões por hora · rodapé.

As funções `secKpis` ("Indicadores do dia"), `secPagamentos` ("Métodos de
pagamento") e `secRede` ("Rede — por eletroposto") continuam no `.ino`, apenas sem
chamada em `renderDash()`. Para trazer qualquer uma de volta, reinsira a chamada na
posição desejada; sem chamada o compilador as descarta e elas não ocupam flash.

**Dashboard** volta ao topo da rolagem; **Motorista** devolve o aparelho ao usuário final.

## 2. Interações

| Gesto | Efeito |
|---|---|
| **ENCERRAR E PAGAR** (Sessão) | vai para o Pagamento |
| Toque num dos 4 métodos | abre o painel daquele método |
| **Simular confirmação / aproximação** | aprova o pagamento |
| **NOVA SESSÃO** | zera a sessão e volta ao início |
| **‹** no canto | volta um passo; na Sessão, abre o dashboard |
| Pílula **MOTORISTA** (dashboard) | volta para a tela de Sessão |
| Pílula **Dashboard** | volta ao topo da rolagem |
| **Arrastar o dedo** no dashboard | rolagem com inércia |
| Toque num box **C01…C04** | troca o conector detalhado |
| **PAUSAR / SEGUIR** | congela ou retoma a simulação |
| **Segurar o logo GOODWE 2 s** (dashboard) | recalibra o toque |
| Serial | `TELA <sessao\|pagamento\|dashboard>`, `PLAY`, `PAUSE`, `CAL`, `DEBUG` |

---

## 3. Passo a passo

### Passo 1 — `User_Setup.h` do TFT_eSPI (já aplicado)

Arquivo: `Documentos\Arduino\libraries\TFT_eSPI\User_Setup.h`
(o original foi salvo ao lado como **`User_Setup.h.bak`**)

Duas linhas foram trocadas, e as duas vieram de diagnóstico na placa — ver
[seção 6](#6-o-diagnóstico-do-painel-o-que-estava-errado):

```c
#define ILI9342_DRIVER          // era ILI9341_DRIVER
#define TFT_RGB_ORDER TFT_RGB   // linha nova
```

`ILI9342_DRIVER` usa **a mesma sequência de init e a mesma tabela de rotação** do
ILI9341 — a única diferença é `TFT_WIDTH 320 / TFT_HEIGHT 240` em vez de 240/320.
Confirme também que `LOAD_GLCD` e `LOAD_GFXFF` continuam ativos: são as fontes que o
dashboard usa.

> ⚠️ Isso vale para **todos** os sketches que usam TFT_eSPI nesta máquina. O antigo
> `IoT-dashboardCYD.ino` usava `setRotation(3)`, que agora dá **retrato** — para ele
> voltar a ficar deitado, troque para `setRotation(2)`.

### Passo 2 — Entender por que o toque é feito "na mão"

No CYD o XPT2046 **não** compartilha o barramento do display. Ele tem pinos próprios:

| Sinal | GPIO |
|---|---|
| T_CLK | 25 |
| T_CS  | 33 |
| T_DIN (MOSI) | 32 |
| T_DO  (MISO) | 39 |
| T_IRQ | 36 |

`tft.getTouch()` fala pelos pinos 13/12/14 do display e por isso **nunca responde** nesta
placa — é a armadilha clássica do CYD. O firmware implementa SPI por software nesses
cinco pinos (função `xptRead`, seção 15 do `.ino`). Vantagem prática: **zero biblioteca
nova para instalar** e zero disputa de barramento.

### Passo 3 — Gravar o firmware

A pasta do sketch já tem o mesmo nome do `.ino` (`ChargeGridCYD/ChargeGridCYD.ino`),
que é o que o `arduino-cli` exige.

> ⚠️ Se a ponte USB estiver rodando, **encerre-a antes** (Ctrl+C) — ela segura a COM7 e
> o upload falha com porta ocupada.

```bash
cd "C:\Users\gabri\Documents\DashboardPagamento-V1.0.0" && set "ARDUINO_DIRECTORIES_DATA=%LOCALAPPDATA%\Arduino15" && set "ARDUINO_DIRECTORIES_USER=%USERPROFILE%\Documents\Arduino" && "%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 -u -p COM7 ChargeGridCYD
```

Se o upload travar em `Connecting...`, segure o botão **BOOT** da placa enquanto ele tenta.

**Compilação já validada nesta máquina** (core esp32 3.3.10, TFT_eSPI 2.5.43,
ArduinoJson 7.4.3):

```
Sketch uses 403983 bytes (30%) of program storage space.
Global variables use 24476 bytes (7%) of dynamic memory, leaving 303204 bytes free.
```

### Passo 4 — Calibrar o toque (só no primeiro boot)

Na primeira vez a tela pede quatro toques, um em cada canto. A rotina descobre sozinha
se os eixos estão trocados ou invertidos e grava o resultado na NVS — nos boots seguintes
ela não aparece mais.

A calibração está **forçada a rodar de novo** nesta versão: a anterior foi feita com a
geometria errada do painel e não vale mais. O que invalida é a constante `CAL_MAGIC` no
`.ino` — se um dia mexer em rotação ou driver, bump nela e a calibração se refaz sozinha.

Para refazer: segure o logo **GOODWE** por 2 segundos, ou mande `CAL` pelo Monitor Serial.

### Passo 5 — Usar

A placa abre na **tela de Sessão**, com a carga correndo sozinha (mesma lógica do
`componentDidMount` do HTML: SOC +1% a cada 4 s, energia entregue subindo 0,012 kWh/s,
tempo restante e custo recalculados a cada segundo).

Percurso completo da demo: **ENCERRAR E PAGAR** → escolha um método → **simular
confirmação** → comprovante → **NOVA SESSÃO**. Para mostrar o painel do dono, toque no
**‹** no canto superior esquerdo da Sessão; para voltar, use a pílula **MOTORISTA**.

### Passo 6 — (opcional) Alimentar com dados reais pela ponte USB

O firmware aceita o **mesmo JSON** do `GET /status` do seu Flask. Basta enviar uma linha
por vez pela serial. Com os dois processos de pé:

```bash
py "C:\...\Servidor python\ServidorFlask-Dados.py"
```

```bash
py "C:\Users\gabri\Documents\DashboardPagamento-V1.0.0\ponte-usb\ponte-usb.py" COM7
```

A placa responde `OK desenhado: 87.4 kW | 41.2 C | 187.4 kWh | aba=Estacao` a cada pacote —
a mesma prova positiva da arquitetura anterior. Se a ponte ficar 15 s em silêncio, o
firmware volta sozinho para a simulação local.

Chaves consumidas (todas opcionais): `potencia_total_kw`, `temperatura_c`,
`energia_entregue_hoje_kwh`, `eficiencia_pct`, `tempo_medio_sessao_min`,
`veiculos_carregando`, `status_sistema`, `veiculo_destaque.marca`, `veiculo_destaque.modelo`.

`Serial.setRxBufferSize(2048)` já está antes do `Serial.begin()` — sem isso o JSON de
~260 B chega truncado no buffer padrão de 256 B.

---

## 4. Como o desenho funciona (para quem for mexer)

O conteúdo de cada aba é bem mais alto que os 180 px da área útil (a aba Estação passa de
1.300 px). Desenhar direto na tela a cada rolagem daria flicker, e um framebuffer inteiro
não caberia na RAM.

A solução é **renderização em faixas**: um único sprite de 320×60 (38 KB) é reaproveitado
como tela de rascunho. No dashboard a área útil é composta em 3 passadas de 60 px; nas
telas do motorista, que ocupam a tela inteira e não rolam, são 4 passadas cobrindo
0–239 com `scrollY` fixo em zero — mesmo mecanismo, mesmas primitivas:

```
para cada faixa i em 0..2:
    g_bandTop = scrollY + i*60      # topo da faixa em coordenadas de conteúdo
    sprite.fillSprite(fundo)
    renderTab()                     # desenha TUDO; o que não toca a faixa é descartado
    sprite.pushSprite(0, 60 + i*60)
```

Todas as primitivas (`bFill`, `bTxt`, `bDot`, `bArc`…) recebem Y em coordenadas de
**conteúdo** e subtraem `g_bandTop` internamente; o `secSkip()` no topo de cada seção
descarta blocos inteiros que não tocam a faixa, o que mantém o custo baixo.

O mesmo sprite desenha o cabeçalho (que também tem 60 px de altura — por isso
`HEADER_H 32 + TABS_H 28`).

Rodando `renderTab()` com `g_measure = true` nada é desenhado, mas as áreas tocáveis são
registradas e a altura total do conteúdo é devolvida — é assim que `relayout()` calcula os
limites de rolagem e a tabela de hit-test ao trocar de aba.

**Para mudar dados**: as tabelas ficam todas na seção 5 do `.ino` (`SESSOES`, `REDE`,
`METODOS`, `ALERTAS`, `DAY`, `SESS_H`) e os parâmetros comerciais na seção 4
(`PRECO_KWH`, `TARIFA_KWH`, `COMISSAO`, `LIMITE_TERM`) — equivalem aos `data-props` do
`.dc.html`.

**Para mudar layout**: cada seção é uma função `secXxx(int y)` que devolve o próximo Y.
Adicionar/remover/reordenar seção é editar `renderTab()`.

---

## 5. Coisas para saber antes de estranhar

**Acentuação.** As fontes GFX do TFT_eSPI cobrem só ASCII 32–126, então os rótulos estão
sem acento ("ESTACAO", "POTENCIA", "SESSOES"). É proposital, não é bug de encoding. Para
ter acento seria preciso gerar uma fonte VLW com o *Font Creator* do TFT_eSPI, subir para
o SPIFFS e trocar `useFont()` por `loadFont()` — dá mais trabalho e consome flash.

**Rotação.** Está em `#define TFT_ROT 2`. Com `ILI9342_DRIVER` as rotações trocam de
significado: **0 e 2 são paisagem** (320x240), 1 e 3 são retrato. Se a tela aparecer de
cabeça para baixo, troque para `0` — é a única linha a mexer, e a calibração de toque se
ajusta sozinha ao refazer os quatro toques.

**Cores.** A paleta RGB565 na seção 2 é a conversão direta do CSS
(`--k-red #e60012` → `0xE002`, `--k-text #f4f2ee` → `0xF79D`, etc.), inclusive as
transparências já achatadas contra o fundo `#0a0a0b`.

**O que já foi verificado na placa.** Compilação, uso de memória, driver do painel, ordem
de cor e rotação — tudo conferido com a tela ligada. Falta confirmar a precisão do toque
depois da calibração e o fps real da rolagem. Se o toque cair sistematicamente deslocado,
o primeiro suspeito é a calibração: segure o logo 2 s e refaça.

**Todo alvo tocavel e de largura cheia ou fica isolado num canto.** Nao ha nenhum ponto
da interface com dois botoes lado a lado. Isso e deliberado: a escolha de metodo de
pagamento comecou como grade 2x2 e era o unico lugar onde um desvio de toque selecionava
o vizinho em vez de simplesmente nao acertar nada. Virou coluna unica.

**O toque e tolerante de proposito.** Painel resistivo entrega lixo nas primeiras
amostras depois do encosto, e o dedo sempre escorrega. Um toque só é descartado se
andar mais de **14 px** ou durar mais de **1,6 s**; as duas primeiras amostras são
jogadas fora, a posição usada é a do momento de soltar, e a solta só conta após duas
leituras seguidas sem contato. Com os valores antigos (7 px / 700 ms) só respondiam os
alvos encostados nos cantos, porque o `constrain()` gruda o ruído no próprio canto.

O comando `DEBUG` na serial liga/desliga o log de toque, que imprime `TAP x,y (ms)`
a cada toque aceito e o motivo de cada descarte. Ele vem **ligado** nesta versão.

**O QR Code não é escaneável.** Ele reproduz o mesmo desenho do HTML original, que é
decorativo: um LCG de semente fixa preenchendo 29×29 módulos, mais os três quadrados
localizadores. Parece um QR, mas nenhum leitor decodifica — foi a decisão combinada.
Para um QR de verdade seria preciso um encoder no firmware.

**Sessão do motorista e telemetria do dashboard são independentes.** A sessão do
motorista tem ritmo próprio (1%/4 s, até 100%) e o C01 do dashboard tem o dele (1%/s,
ciclando) — igual ao HTML, onde são dois componentes separados. Os números das duas
telas não batem entre si de propósito.

---

## 6. O diagnóstico do painel: o que estava errado

A primeira gravação subiu embaralhada. O caminho até a causa, porque vale para qualquer
CYD que se comporte assim:

**Sintomas.** Texto em orientação errada, azul saindo vermelho, e uma faixa lateral com
"resto" de desenho antigo que mudava de lado conforme a rotação.

**O que foi descartado.** Um sketch de diagnóstico (`TesteTela/`) mostrou que o desenho
direto e o caminho de sprite funcionavam, e que a biblioteca reportava 320x240 em rot=1/3
corretamente. Baixar o SPI de 55 para 27 MHz não mudou nada — **não era velocidade de
barramento**, que era a hipótese mais provável no começo. Ler os registradores do
controlador (`LeID/`) devolveu `RDDMADCTL` = `0x48` (deslocado 1 bit na leitura), exatamente
o valor que o TFT_eSPI escreve — ou seja, o painel *aceitava* os comandos normalmente.

**A causa.** Na foto do rot=2 havia uma linha branca vertical a ~75% da largura, com o
desenho antigo à direita dela. 240/320 = 75%. Somando a isso o fato de rot=0/rot=2 darem
texto deitado e rot=1/rot=3 texto em pé — o inverso do que a biblioteca assume — a
conclusão é que **este painel é nativo 320x240 (paisagem)**, e não 240x320. Com
`ILI9341_DRIVER` o TFT_eSPI pintava uma janela de 240 px de largura e os 80 px restantes
nunca eram tocados.

**A correção.** `ILI9342_DRIVER` (que é o ILI9341 com `TFT_WIDTH 320 / TFT_HEIGHT 240`),
`TFT_RGB_ORDER TFT_RGB` para a inversão R/B, e `setRotation(2)` no firmware.

Vale registrar: **o modelo não é o ESP32-2432S028R clássico**. A placa é laranja com USB-C
e painel `TPM408-2.8`, e a documentação antiga do projeto (que descreve ILI9341 240x320 com
`setRotation(3)`) não corresponde a esta unidade.

**Ferramentas que ficaram no repositório.** `TesteTela/` (cantos coloridos, faixa de cores,
teste de sprite, ciclo de rotações) e `LeID/` (dump dos registradores do controlador). Se
um dia trocar de placa e a tela vier estranha, comece por elas.

Para testar variações de driver **sem mexer na biblioteca**, dá para passar tudo por flag
de build — foi assim que as hipóteses foram testadas aqui:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 -u -p COM7 --build-property "compiler.cpp.extra_flags=-DUSER_SETUP_LOADED=1 -DILI9342_DRIVER=1 -DTFT_MISO=12 -DTFT_MOSI=13 -DTFT_SCLK=14 -DTFT_CS=15 -DTFT_DC=2 -DTFT_RST=-1 -DTFT_BL=21 -DTFT_BACKLIGHT_ON=HIGH -DTFT_RGB_ORDER=1 -DLOAD_GLCD=1 -DLOAD_FONT2=1 -DLOAD_FONT4=1 -DLOAD_GFXFF=1 -DSMOOTH_FONT=1 -DSPI_FREQUENCY=55000000 -DSPI_READ_FREQUENCY=6000000 -DSPI_TOUCH_FREQUENCY=2500000" TesteTela
```

---

## 7. Arquivos

```
DashboardPagamento-V1.0.0\
├── ChargeGridCYD\
│   └── ChargeGridCYD.ino          firmware completo
├── TFT_eSPI-User_Setup\
│   └── User_Setup.h               referência da config do display
├── ponte-usb\
│   └── ponte-usb.py               ponte Flask -> serial (opcional)
├── TesteTela\
│   └── TesteTela.ino              diagnostico de painel/rotacao/sprite
├── LeID\
│   └── LeID.ino                   dump dos registradores do controlador
└── README.md
```
