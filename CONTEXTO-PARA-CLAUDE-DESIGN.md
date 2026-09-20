# ChargeGrid — contexto do projeto para o Claude Design

> Documento de handoff. Quem trabalha nos arquivos `.dc.html` do Claude Design deve ler
> isto antes de mexer em qualquer tela.
>
> **O ponto central:** estes designs não terminam no navegador. Eles são portados à mão
> para firmware C++ que roda numa tela de **320 × 240 pixels** acoplada a um ESP32. Toda
> decisão de layout tem que sobreviver a essa redução. As seções 4 e 5 listam as regras
> que já custaram retrabalho.
>
> Estado descrito: 20 de setembro de 2026.

---

## 1. O que é o projeto

**ChargeGrid Intelligence** é um trabalho acadêmico da FIAP (Computação em Nuvem e
Sistemas Embarcados, turma 1CCP), estruturado como *Challenge Sprint*. A proposta
comercial é um **mini eletroposto inteligente** para recarga de veículos elétricos, com
telemetria, pagamento antecipado via QR Code/Pix e um modelo de negócio de franquias
regionais. A marca usada nas telas é **GOODWE**, e o carregador de referência é o
GoodWe HCA-G2.

O entregável físico é um **painel embarcado**: as telas desenhadas no Claude Design
rodam dentro de uma tela ESP32 CYD de 2,8 polegadas, desenhadas pixel a pixel em C++
com a biblioteca TFT_eSPI. Não há navegador, não há HTML em execução — o `.dc.html` é
a **especificação visual**, e o firmware é a implementação.

Existe um segundo módulo no projeto original (um chatbot RAG com FastAPI + FAISS +
LLaMA 3.3 via Groq) que **não faz parte deste repositório** nem destes designs.

---

## 2. As três telas e as duas personas

O mesmo aparelho atende dois públicos completamente diferentes. Essa separação é a
decisão estrutural mais importante do projeto.

| Persona | Quem é | Telas | Arquivo de design |
|---|---|---|---|
| **Motorista** | usuário final, no eletroposto | Sessão · Pagamento | `ChargeGrid Sessao.dc.html` · `ChargeGrid Pagamento.dc.html` |
| **Operador** | dono/franqueado do eletroposto | Dashboard | `ChargeGrid Dashboard.dc.html` |

**O aparelho liga na tela de Sessão.** É o que o motorista vê em ~99% do tempo. O
dashboard do dono é a tela escondida, alcançada por um `‹` discreto no canto superior
esquerdo — e não o contrário.

### Fluxo do motorista

```
Sessão ──[ENCERRAR E PAGAR]──► Pagamento ──[escolhe método]──► painel do método
                                                                      │
   ▲                                                          [simular confirmação]
   │                                                                  ▼
   └──────────────────[NOVA SESSÃO]───────────────────────────── Aprovado
```

O `‹` volta um passo: do painel do método para a lista, e da lista de volta para a
Sessão.

### Fluxo do operador

Uma página só, rolável. Sem abas. O cabeçalho tem duas pílulas: **Dashboard** (volta ao
topo da rolagem) e **Motorista** (devolve o aparelho ao usuário final).

---

## 3. O que mudou dos designs para o firmware

Os `.dc.html` são a origem, mas o produto embarcado divergiu por decisão do cliente.
**Se você for atualizar os designs para refletir o produto, é esta a lista.**

### Dashboard — o que foi removido

O design original tem três abas (`showHero`, `showKpis`, `showSessions`, `showRede`
combinando conforme `tab`). No firmware isso virou **uma página única**, e estas seções
saíram por completo:

| Removido | Era |
|---|---|
| Abas Estação / Rede / Financeiro | três combinações de seções |
| Mapa da rede de São Paulo | painel com vias e pontos no `<aside>` |
| Eventos · 24 h | lista de 4 alertas |
| Indicadores do dia (KPIs) | grade de 6 cartões |
| Métodos de pagamento | 4 barras de proporção |
| Rede · por eletroposto | tabela de 6 pontos + total |

E estes campos saíram de seções que ficaram:

- **Hero:** o vital "DISPONIBILIDADE" e a linha "3/4 conectores ativos"
- **Conectores C01–C04:** o percentual de SOC (ficou só o valor em kW)
- **Sessão em curso:** o campo "DECORRIDO"
- **Sessões recentes:** reduzida a 3 linhas (Tesla, BYD, Bolt) e sem a coluna "CX"

**Ordem atual do dashboard:** anel de potência + 3 vitais · curva de carga 24 h ·
conectores C01–C04 · sessão em curso · sessões recentes · sessões por hora · rodapé.
Isso dá ~655 px de rolagem (era ~1.360 px).

### Pagamento — o que mudou

A escolha de método era uma **grade 2×2**. No firmware virou uma **coluna única de 4
linhas de largura cheia**. O motivo está na seção 5 e não é estético.

### Sessão

Portada praticamente como desenhada. O rótulo "TEMPO RESTANTE" dentro do anel virou
"RESTANTE" — 14 caracteres não cabiam no vão do anel em 320 px.

---

## 4. Restrições duras do hardware

Não são preferências. São limites físicos do alvo.

### 4.1 Tela: 320 × 240 pixels, paisagem

Os designs originais são de **900 × 1180** (Sessão e Pagamento) e **1280 de largura**
(Dashboard). A redução é de cerca de **8× em área**. Isso não é "encolher o layout" — é
recompor.

Regra prática: **um design que precisa de mais de 6 blocos de informação visíveis ao
mesmo tempo não cabe.** Na tela real, uma linha de texto pequeno ocupa 8 px de altura e
um título ocupa 13 px.

Grade útil do firmware:

| Medida | Valor |
|---|---|
| Tela | 320 × 240 |
| Margem lateral (`PAD`) | 8 px de cada lado |
| Largura útil de card (`CW`) | 300 px (x de 8 a 308) |
| Barra de rolagem | x 313–315 |
| Cabeçalho do dashboard | 60 px (32 de header + 28 de pílulas) |
| Área rolável | 180 px |
| Telas do motorista | 240 px inteiros, sem rolagem |

### 4.2 Sem acentuação

As fontes GFX embutidas na TFT_eSPI cobrem **apenas ASCII 32–126**. Todo rótulo no
firmware está sem acento: `ESTACAO`, `POTENCIA`, `SESSOES`, `MANUTENCAO`,
`VILA OLIMPIA`. Não é bug de encoding.

Nos designs pode manter acento — quem porta remove. Mas **evite textos onde o acento é
essencial ao sentido**, e lembre que a versão sem acento é a que o professor vê.

Para ter acento seria preciso gerar uma fonte VLW pelo *Font Creator* do TFT_eSPI e
subir para o SPIFFS. Custa flash e trabalho; foi deliberadamente evitado.

### 4.3 Tipografia disponível

Só existem estes tamanhos. Não há pesos intermediários nem tracking.

| No firmware | Fonte real | Altura aprox. | Uso |
|---|---|---|---|
| `FS1` | fonte 1 embutida, 6×8 | 8 px | rótulos pequenos, tabelas, metadados |
| `F9` / `FB9` | FreeSans / Bold 9 pt | 13 px | títulos de seção, nomes |
| `F12` / `FB12` | FreeSans / Bold 12 pt | 17 px | destaque médio, botões |
| `F18` / `FB18` | FreeSans / Bold 18 pt | 25 px | números grandes (kW, R$, minutos) |
| `F24` | FreeSans 24 pt | 33 px | disponível, hoje sem uso |

`letter-spacing` do CSS não existe. Textos com `letter-spacing:.2em` nos designs
aparecem colados no firmware.

### 4.4 Paleta em RGB565

Os valores do CSS foram convertidos uma vez e estão fixos no firmware. **Transparência
não existe** — toda cor com alpha foi achatada contra o fundo `#0a0a0b`.

| Design | Firmware | Papel |
|---|---|---|
| `#0a0a0b` | `0x0841` | fundo |
| `#121215` | `0x1082` | painel |
| `rgba(244,242,238,.10)` | `0x2124` | linha/borda |
| `#f4f2ee` | `0xF79D` | texto |
| 54 % do texto | `0x8C51` | texto secundário |
| 34 % do texto | `0x5ACB` | texto terciário |
| `#e60012` | `0xE002` | vermelho da marca |
| `#ff3b45` | `0xF9C8` | vermelho claro |
| `#3ddc97` | `0x3EF2` | verde (ok) |
| `#ffb020` | `0xFD84` | âmbar (alerta) |

Gradientes viram cor chapada. Sombras (`drop-shadow`, `text-shadow`) e `backdrop-filter`
não existem. `border-radius` existe.

### 4.5 O painel é 320 × 240 nativo

Detalhe técnico que custou uma sessão inteira de diagnóstico: a placa **não** é o
ESP32-2432S028R clássico. É uma variante laranja com USB-C e painel `TPM408-2.8`, cujo
controlador é **nativo paisagem**. Exige `ILI9342_DRIVER` e `TFT_RGB_ORDER TFT_RGB` no
`User_Setup.h` da TFT_eSPI. Com a configuração padrão de ILI9341 a biblioteca pinta uma
janela de 240 px de largura e sobra uma faixa de 80 px com o frame anterior.

Irrelevante para o design, mas registrado porque contradiz a documentação antiga do
projeto.

---

## 5. Regras de design que já custaram retrabalho

Estas não vieram de teoria. Vieram de coisas que quebraram na placa.

### 5.1 Nada de alvos tocáveis lado a lado

**Esta é a regra mais importante.** O toque é resistivo e tem desvio. Enquanto todo
alvo for de largura cheia ou estiver isolado num canto, um desvio de toque erra para
"não aconteceu nada" — confuso, mas recuperável. Dois botões vizinhos transformam o
mesmo desvio em "abriu a coisa errada", que é muito pior.

A escolha de método de pagamento nasceu como grade 2×2 e foi **o único ponto da
interface com esse problema**: tocar em PayPal abria o PIX. Virou coluna única e o
sintoma sumiu.

Concretamente: **botões de ação ocupam os 300 px de largura útil.** Altura mínima
confortável: 34 px.

### 5.2 Uma ação principal por tela

Cada tela do motorista tem exatamente um botão grande, sempre na base, sempre no mesmo
lugar. O usuário do eletroposto não procura; ele vê ou não vê.

### 5.3 Texto tem que caber em 300 px

Contas rápidas para validar um texto ainda no design:

- fonte pequena: **6 px por caractere** → 50 caracteres cabem na largura útil
- `F9`: ~8 px por caractere → ~37 caracteres
- `FB18`: ~20 px por caractere → ~15 caracteres

Frases já cortadas por não caberem: "TEMPO RESTANTE" dentro do anel, "Aponte a câmera do
banco", "Cobrança única de R$ X. Nada é salvo no eletroposto.", "Você será redirecionado
ao app do PayPal para autorizar."

O firmware trunca com `..` quando estoura, o que fica feio. Melhor resolver no design.

### 5.4 Números grandes precisam de espaço reservado

Valores que crescem (`R$ 1.234,56`, `104,2 kW`) precisam de folga. Dentro de um anel, o
espaço útil é bem menor que o diâmetro — a meia-largura disponível cai rápido conforme
se afasta do centro vertical.

---

## 6. Modelo de dados

Os `data-props` dos `.dc.html` viraram constantes no firmware. Mantenha os dois lados
sincronizados.

| Parâmetro | Valor | Onde aparece |
|---|---|---|
| `precoKwh` | R$ 2,35 / kWh | todas as telas |
| `tarifaEnergia` | R$ 0,92 / kWh | custo do operador |
| `taxaRede` | R$ 1,90 | tela de pagamento |
| `comissaoFranqueado` | 70 % | repasse D+1 |
| `limiteTermico` | 48 °C | cor do vital de temperatura |
| `capacidadeBateria` | 60 kWh | cálculo de tempo restante na Sessão |

### Telemetria simulada

**Dashboard** (passeio aleatório, 1 Hz): potência 22–105 kW, temperatura 30–56 °C,
eficiência 92–99 %, energia acumulando. SOC do C01 sobe 1 %/s e cicla.

**Sessão do motorista** (1 Hz): SOC +1 % a cada 4 s partindo de 62 %, energia entregue
+0,012 kWh/s partindo de 24,8 kWh, tempo restante e custo recalculados a cada segundo.

As duas simulações são **independentes de propósito** — como no HTML, onde são dois
componentes separados. Os números das duas telas não batem entre si.

### Entrada de dados reais (opcional)

O firmware aceita, pela serial, o mesmo JSON do endpoint `GET /status` do servidor Flask
do projeto: `potencia_total_kw`, `temperatura_c`, `energia_entregue_hoje_kwh`,
`eficiencia_pct`, `tempo_medio_sessao_min`, `veiculos_carregando`, `status_sistema`,
`veiculo_destaque.{marca,modelo}`.

---

## 7. Decisões que já estão fechadas

Não reabra sem motivo novo.

**O QR Code não é escaneável.** Reproduz o desenho decorativo do HTML original: um LCG
de semente fixa preenchendo 29 × 29 módulos, mais os três quadrados localizadores.
Parece um QR, nenhum leitor decodifica. Foi decisão explícita do cliente.

**O dashboard é uma página só.** Abas foram removidas a pedido.

**O aparelho liga no modo motorista.** O dashboard é a tela escondida.

**Sem acentuação no firmware.** Ver 4.2.

---

## 8. Estado atual

O firmware está **funcionando na placa**, gravado e validado: as três telas rodam, o
toque responde, a navegação entre personas funciona, a rolagem tem inércia.

Ocupação: **403.983 bytes de flash** (30,8 % de 1,25 MB) e cerca de **63 KB de RAM** em
operação, dos 320 KB disponíveis (24,5 KB estáticos + 38,4 KB do sprite de renderização).

Pendência conhecida: pode haver um desvio residual na calibração do toque. A coluna
única de métodos contorna o sintoma mais visível, mas não foi confirmado que a causa
sumiu. O firmware tem log de toque na serial para fechar isso.

---

## 9. Se você for mexer nos designs

1. **Desenhe pensando em 320 × 240.** Vale abrir o `.dc.html` numa janela estreita e
   perguntar: isto ainda funciona com 8× menos área?
2. **Conte os caracteres** dos textos novos com as contas de 5.3.
3. **Não coloque dois botões lado a lado.** Nunca.
4. **Não invente cor nova** sem verificar que ela sobrevive ao RGB565 num fundo quase
   preto. Tons próximos do fundo somem.
5. **Se remover ou adicionar seção**, diga explicitamente — o porte é manual e nada
   sincroniza sozinho.
6. **Cada tela nova precisa caber em 240 px de altura** se for do motorista (sem
   rolagem), ou pode crescer livremente se for do dashboard (rolável).

---

## 10. Arquivos

### No Claude Design (projeto `6f9ae4b2-39cb-47f7-9f7c-9a4158c65122`)

```
ChargeGrid Dashboard.dc.html    painel do operador (origem; hoje divergente)
ChargeGrid Sessao.dc.html       tela de carregamento do motorista
ChargeGrid Pagamento.dc.html    tela de pagamento do motorista
uploads/CONTEXTO-ChargeGrid.md  contexto antigo — descreve a arquitetura Wi-Fi e a
                                placa ILI9341 240x320; ambos superados, ver 4.5
```

### No repositório do firmware

```
DashboardPagamento-V1.0.0\
├── ChargeGridCYD\ChargeGridCYD.ino    firmware completo (1.604 linhas)
├── TFT_eSPI-User_Setup\User_Setup.h   config validada do display
├── ponte-usb\ponte-usb.py             ponte Flask -> serial (opcional)
├── TesteTela\TesteTela.ino            diagnóstico de painel/rotação/sprite
├── LeID\LeID.ino                      dump dos registradores do controlador
├── README.md                          documentação técnica do firmware
└── CONTEXTO-PARA-CLAUDE-DESIGN.md     este arquivo
```

O `README.md` tem o detalhe técnico: renderização em faixas, driver de toque por
software, calibração, e o registro completo do diagnóstico do painel.
