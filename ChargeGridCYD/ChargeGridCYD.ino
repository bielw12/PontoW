/* =====================================================================
   ChargeGrid Intelligence - firmware embarcado
   Alvo: CYD 2.8" (placa laranja, painel TPM408-2.8)
         Painel ILI934x NATIVO 320x240 + toque resistivo XPT2046
         Exige ILI9342_DRIVER e TFT_RGB_ORDER TFT_RGB no User_Setup.h

   Duas personas no mesmo aparelho:

   MOTORISTA (padrao no boot) - tela cheia, sem rolagem
     - Sessao     : porte de "ChargeGrid Sessao.dc.html"
     - Pagamento  : porte de "ChargeGrid Pagamento.dc.html"

   OPERADOR (dono do eletroposto) - cabecalho + abas rolaveis
     - Estacao / Rede / Financeiro : porte de "ChargeGrid Dashboard.dc.html"

   Navegacao
     Sessao      -> "ENCERRAR E PAGAR" -> Pagamento
     Pagamento   -> metodo -> confirmar -> "NOVA SESSAO" -> Sessao
     Sessao      -> "<" no canto superior esquerdo -> dashboard do operador
     Dashboard   -> pilula "MOTORISTA" na barra de abas -> Sessao
     Calibracao  -> segurar o logo GOODWE 2 s no modo operador, ou "CAL" na serial

   Dependencias: TFT_eSPI 2.5.43   ArduinoJson 7.4.3   core esp32 3.3.10

   OBS. de fonte: as fontes GFX embutidas cobrem apenas ASCII 32..126,
   por isso os rotulos estao sem acento (ESTACAO, POTENCIA, SESSOES...).
   ===================================================================== */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <strings.h>

/* As fontes FreeSans* ja vem declaradas por Fonts/GFXFF/gfxfont.h,
   incluido pelo proprio TFT_eSPI.h quando LOAD_GFXFF esta ligado.
   Nao inclua os .h individuais: da "redefinition of ...". */

#ifndef LOAD_GFXFF
#error "Habilite #define LOAD_GFXFF no User_Setup.h do TFT_eSPI (ver README)."
#endif

/* ---------------------------------------------------------------- */
/* 1. GEOMETRIA                                                      */
/* ---------------------------------------------------------------- */
#define SCR_W       320
#define SCR_H       240
#define TFT_ROT     2            // painel 320x240 NATIVO (ILI9342_DRIVER):
                                 // 0 e 2 = paisagem, 1 e 3 = retrato.
                                 // Se ficar de cabeca para baixo, use 0.

#define HEADER_H    32
#define TABS_H      28
#define CHROME_H    (HEADER_H + TABS_H)   // 60
#define CONTENT_Y   CHROME_H              // 60
#define CONTENT_H   (SCR_H - CONTENT_Y)   // 180
#define BAND_H      60                    // 180 = 3 faixas; tela cheia = 4 faixas
#define N_BANDS     (CONTENT_H / BAND_H)  // 3
#define N_BANDS_FULL (SCR_H / BAND_H)     // 4

#define PAD         8
#define CW          300                   // largura util dos cards (8..308)
#define RGT         (PAD + CW)            // 308
#define SBAR_X      313                   // barra de rolagem 313..315

/* ---------------------------------------------------------------- */
/* 2. PALETA (RGB565 equivalente ao CSS das telas)                   */
/* ---------------------------------------------------------------- */
#define C_BG        0x0841   // #0a0a0b
#define C_PANEL     0x1082   // #121215
#define C_LINE      0x2124   // rgba(244,242,238,.10) sobre o fundo
#define C_TEXT      0xF79D   // #f4f2ee
#define C_DIM       0x8C51   // 54%
#define C_DIM2      0x5ACB   // 34%
#define C_RED       0xE002   // #e60012
#define C_RED2      0xF9C8   // #ff3b45
#define C_REDSOFT   0x2841   // rgba(230,0,18,.12)
#define C_REDGLOW   0x5000   // rgba(230,0,18,.22)
#define C_AREA      0x4021   // rgba(230,0,18,.28) - preenchimento do grafico
#define C_OK        0x3EF2   // #3ddc97
#define C_OKSOFT    0x0A44   // rgba(61,220,151,.10)
#define C_WARN      0xFD84   // #ffb020
#define C_WARNSOFT  0x2920   // rgba(255,176,32,.10)
#define C_WHITE     0xFFFF

#define FS1  ((const GFXfont*)nullptr)   // fonte 1 embutida (6x8)
#define F9   (&FreeSans9pt7b)
#define FB9  (&FreeSansBold9pt7b)
#define F12  (&FreeSans12pt7b)
#define FB12 (&FreeSansBold12pt7b)
#define F18  (&FreeSans18pt7b)
#define FB18 (&FreeSansBold18pt7b)
#define F24  (&FreeSans24pt7b)

/* ---------------------------------------------------------------- */
/* 3. TOQUE XPT2046 (SPI por software - nao disputa o barramento     */
/*    do display, por isso nao depende de TOUCH_CS no User_Setup)    */
/* ---------------------------------------------------------------- */
#define T_CLK  25
#define T_CS   33
#define T_DIN  32
#define T_DO   39   // somente entrada
#define T_IRQ  36   // somente entrada

#define TFT_BL_PIN 21

/* ---------------------------------------------------------------- */
/* 4. PARAMETROS DE NEGOCIO (equivalem aos props dos .dc.html)       */
/* ---------------------------------------------------------------- */
static float PRECO_KWH   = 2.35f;   // R$/kWh
static float TARIFA_KWH  = 0.92f;   // custo de energia
static float TAXA_REDE   = 1.90f;   // taxa de uso da rede (tela de pagamento)
static int   COMISSAO    = 70;      // % repasse ao franqueado
static int   LIMITE_TERM = 48;      // grau C
static int   CAPACIDADE  = 60;      // kWh da bateria do veiculo (tela de sessao)

/* ---------------------------------------------------------------- */
/* 5. DADOS ESTATICOS                                                */
/* ---------------------------------------------------------------- */
struct Car { const char* name; float kwh; int min; };
static const Car CARS[3] = {
  { "Tesla Model 3", 18.4f, 27 },
  { "BYD Dolphin",   11.2f, 22 },
  { "Volvo EX30",    21.7f, 31 },
};

static const uint8_t DAY[24]    = { 12,9,7,6,8,18,34,52,61,55,48,44,58,63,57,51,62,78,96,104,88,63,41,24 };
static const uint8_t SESS_H[24] = { 1,0,0,0,1,2,3,5,6,5,4,4,5,6,5,4,6,7,8,9,7,5,3,2 };

enum { PG_PIX = 0, PG_GPAY, PG_APAY, PG_PPAL };
static const char*    PG_NAME[4]  = { "PIX", "GPay", "APay", "PPal" };
static const char*    PG_FULL[4]  = { "PIX", "Google Pay", "Apple Pay", "PayPal" };
static const char*    PG_SUB[4]   = { "Aprovacao imediata", "Aproxime o Android",
                                      "Aproxime o iPhone",  "Conta ou cartao" };
static const uint16_t PG_COLOR[4] = { C_RED, C_OK, C_TEXT, C_WARN };

/* Sessoes recentes: 3 linhas, sem a coluna CX */
struct Sess { const char* hora; const char* car; float kwh; int min; uint8_t pg; };
static const Sess SESSOES[3] = {
  { "19:42", "Tesla Model 3",     18.4f, 27, PG_PIX  },
  { "19:20", "BYD Dolphin",       11.2f, 22, PG_GPAY },
  { "18:58", "Chevrolet Bolt EV",  9.6f, 19, PG_PIX  },
};
#define N_SESSOES 3

struct Metodo { const char* label; const char* n; float pct; uint8_t pg; };
static const Metodo METODOS[4] = {
  { "PIX",        "48 sessoes", 57.8f, PG_PIX  },
  { "Google Pay", "13 sessoes", 15.7f, PG_GPAY },
  { "Apple Pay",  "12 sessoes", 14.5f, PG_APAY },
  { "PayPal",     "10 sessoes", 12.0f, PG_PPAL },
};

struct Node { const char* nome; const char* id; const char* estado; uint16_t color;
              float kw; int sess; float kwh; float up; float temp; };
static const Node REDE[6] = {
  { "Vila Olimpia", "HCA-G2-001", "ATIVO",      C_OK,   0,     24, 0,      99.4f, 0     }, // linha viva
  { "Pinheiros",    "HCA-G2-002", "ATIVO",      C_OK,   62.8f, 19, 142.8f, 97.1f, 44.6f },
  { "Moema",        "HCA-G2-003", "ATIVO",      C_OK,   41.2f, 16, 121.3f, 99.8f, 38.9f },
  { "Itaim Bibi",   "HCA-G1-004", "ATIVO",      C_OK,   28.6f, 13,  96.5f, 99.2f, 36.4f },
  { "Santo Amaro",  "HCA-G1-005", "ALERTA",     C_WARN, 15.4f, 11,  74.2f, 92.4f, 53.1f },
  { "Tatuape",      "HCA-G1-006", "MANUTENCAO", C_DIM2, 0,      0,   0,     0,     0    },
};

/* ---------------------------------------------------------------- */
/* 6. ESTADO VIVO                                                    */
/* ---------------------------------------------------------------- */
enum { MODE_MOTORISTA = 0, MODE_OPERADOR };
enum { SCR_SESSAO = 0, SCR_PAGAMENTO };

struct State {
  /* --- navegacao --- */
  uint8_t  mode      = MODE_MOTORISTA;
  uint8_t  scr       = SCR_SESSAO;

  /* --- telemetria do eletroposto (dashboard) --- */
  bool     playing   = true;
  uint32_t t         = 0;
  int      ago       = 2;
  float    kw        = 87.4f;
  float    kwh       = 187.4f;
  float    temp      = 41.2f;
  float    eff       = 96.3f;
  int      soc       = 62;
  int      hero      = 0;
  int      mins      = 14;
  int      veicCarregando = 3;
  char     statusSis[20] = "OPERACIONAL";
  uint8_t  selBay    = 0;

  /* --- sessao do motorista --- */
  int      sSoc      = 62;
  float    sEntregue = 24.8f;
  uint32_t sSeg      = 0;

  /* --- pagamento --- */
  int8_t   payMet    = -1;      // -1 = nenhum metodo escolhido
  bool     payDone   = false;
  int      payExpira = 298;
  bool     payCopiado = false;

  /* --- ponte serial --- */
  bool     serialLive = false;
  uint32_t lastPkt   = 0;
} S;

struct BayInfo { const char* id; const char* state; uint16_t color;
                 char car[32]; float kw; int soc; bool live; };
static BayInfo BAYS[4];

/* ---------------------------------------------------------------- */
/* 7. OBJETOS GLOBAIS                                                */
/* ---------------------------------------------------------------- */
static TFT_eSPI    tft = TFT_eSPI();
static TFT_eSprite spr = TFT_eSprite(&tft);
static Preferences prefs;

static int   g_bandTop  = 0;      // topo da faixa em coordenadas de canvas
static bool  g_measure  = false;  // passe de medicao (nao desenha)
static int   contentH   = 600;    // altura total do conteudo da aba
static int   scrollY    = 0;
static float scrollVel  = 0;
static bool  dirty      = true;
static bool  chromeDirty= true;
static bool  touchDbg   = true;   // log de toque; desliga com o comando DEBUG

/* duas pilulas grandes na segunda linha do cabecalho */
static int   dashX = PAD, dashW = 96;             // "Dashboard" -> volta ao topo
static int   motoX = 212, motoW = 96;             // "Motorista" -> telas do usuario
static int   playX = 248, playW = 64, playY = 6, playH = 20;

/* ---------------------------------------------------------------- */
/* 8. AREAS TOCAVEIS                                                 */
/* ---------------------------------------------------------------- */
enum {
  HIT_BAY      = 100,   // 100..103
  HIT_SES_PAGAR = 200,
  HIT_SES_OPER  = 201,
  HIT_PAY_BACK  = 202,
  HIT_PAY_MET   = 210,  // 210..213
  HIT_PAY_OK    = 220,
  HIT_PAY_NOVA  = 221,
  HIT_PAY_COPIA = 222,
};
struct Hit { int16_t id, x, y, w, h; };
static Hit hits[20];
static int hitN = 0;

static void addHit(int id, int x, int y, int w, int h) {
  if (!g_measure || hitN >= 20) return;
  hits[hitN++] = { (int16_t)id, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
}

/* ---------------------------------------------------------------- */
/* 9. FORMATACAO pt-BR                                               */
/* ---------------------------------------------------------------- */
static String nf(float v, int d = 1) {
  char b[24];
  dtostrf(v, 0, d, b);
  String s(b);
  s.replace('.', ',');
  return s;
}

static String brl(float v, bool prefixo = true) {
  bool neg = v < 0; if (neg) v = -v;
  long cents = (long)(v * 100.0f + 0.5f);
  long ip = cents / 100; int fp = (int)(cents % 100);
  char ib[16]; snprintf(ib, sizeof(ib), "%ld", ip);
  String si(ib), out = "";
  int c = 0;
  for (int i = (int)si.length() - 1; i >= 0; i--) {
    out = String(si[i]) + out;
    if (++c % 3 == 0 && i > 0) out = "." + out;
  }
  char fb[8]; snprintf(fb, sizeof(fb), ",%02d", fp);
  String r = out + fb;
  if (neg) r = "-" + r;
  return prefixo ? ("R$ " + r) : r;
}

/* relogio da sessao: 19:08 + offset em minutos */
static String hhmm(int offsetMin) {
  int total = 19 * 60 + 8 + offsetMin;
  return String(total / 60 % 24 < 10 ? "0" : "") + String(total / 60 % 24) + ":" +
         String(total % 60 < 10 ? "0" : "") + String(total % 60);
}

static float randRange(float a, float b) {
  return a + (float)random(0, 10001) / 10000.0f * (b - a);
}

/* ---------------------------------------------------------------- */
/* 10. PRIMITIVAS DE DESENHO EM FAIXA                                */
/*     Y sempre em coordenadas de canvas; a faixa recorta o resto.   */
/* ---------------------------------------------------------------- */
static inline bool cull(int y, int h) {
  if (g_measure) return true;
  return (y - g_bandTop >= BAND_H) || (y + h - g_bandTop <= 0);
}

static void bFill(int x, int y, int w, int h, uint16_t c) {
  if (cull(y, h)) return;
  spr.fillRect(x, y - g_bandTop, w, h, c);
}
static void bRound(int x, int y, int w, int h, int r, uint16_t c) {
  if (cull(y, h)) return;
  spr.fillRoundRect(x, y - g_bandTop, w, h, r, c);
}
static void bFrame(int x, int y, int w, int h, int r, uint16_t c) {
  if (cull(y, h)) return;
  spr.drawRoundRect(x, y - g_bandTop, w, h, r, c);
}
static void bHLine(int x, int y, int w, uint16_t c) { bFill(x, y, w, 1, c); }
static void bVLine(int x, int y, int h, uint16_t c) { bFill(x, y, 1, h, c); }
static void bLine(int x0, int y0, int x1, int y1, uint16_t c) {
  int a = min(y0, y1), b = max(y0, y1);
  if (cull(a, b - a + 1)) return;
  spr.drawLine(x0, y0 - g_bandTop, x1, y1 - g_bandTop, c);
}
static void bDot(int x, int y, int r, uint16_t c) {
  if (cull(y - r, 2 * r + 1)) return;
  spr.fillCircle(x, y - g_bandTop, r, c);
}
static void bRing(int x, int y, int r, uint16_t c) {
  if (cull(y - r, 2 * r + 1)) return;
  spr.drawCircle(x, y - g_bandTop, r, c);
}

static void useFont(const GFXfont* f) {
  if (f) spr.setFreeFont(f);
  else { spr.setTextFont(1); spr.setTextSize(1); }
}

static void bTxt(const char* s, int x, int y, uint16_t c,
                 const GFXfont* f = FS1, uint8_t datum = TL_DATUM) {
  if (g_measure) return;
  int yy = y - g_bandTop;
  if (yy > BAND_H + 8 || yy < -48) return;
  useFont(f);
  spr.setTextDatum(datum);
  spr.setTextColor(c);
  spr.drawString(s, x, yy);
}

static int txtW(const char* s, const GFXfont* f) {
  useFont(f);
  return spr.textWidth(s);
}

static String fitTxt(const char* s, const GFXfont* f, int maxw) {
  String r(s);
  if (txtW(r.c_str(), f) <= maxw) return r;
  while (r.length() > 1) {
    r.remove(r.length() - 1);
    String t = r + "..";
    if (txtW(t.c_str(), f) <= maxw) return t;
  }
  return r;
}

/* anel: 0 grau = 12 h, sentido horario */
static void bArc(int cx, int cy, int r, int thick, float startDeg, float sweepDeg, uint16_t c) {
  if (g_measure) return;
  if (cull(cy - r - thick, 2 * (r + thick))) return;
  if (sweepDeg <= 0) return;
  float step = 60.0f / (float)r;
  for (float a = 0; a <= sweepDeg; a += step) {
    float rad = (startDeg + a) * 0.0174532925f;
    int x = cx + (int)lroundf(sinf(rad) * r);
    int y = cy - (int)lroundf(cosf(rad) * r);
    bDot(x, y, thick / 2, c);
  }
}

/* seta "<" desenhada a mao (as fontes GFX nao tem um chevron decente) */
static void bChevron(int x, int y, int h, uint16_t c) {
  int w = h / 2;
  for (int i = 0; i < 2; i++) {          // 2 px de espessura
    bLine(x + w + i, y,     x + i, y + h / 2, c);
    bLine(x + i,     y + h / 2, x + w + i, y + h, c);
  }
}

/* check de "pagamento aprovado" */
static void bCheck(int cx, int cy, int r, uint16_t c) {
  bRing(cx, cy, r, c);
  bRing(cx, cy, r - 1, c);
  for (int i = 0; i < 3; i++) {
    bLine(cx - r / 2, cy + i, cx - r / 6, cy + r / 2 + i, c);
    bLine(cx - r / 6, cy + r / 2 + i, cx + r / 2, cy - r / 3 + i, c);
  }
}

/* botao grande de acao, largura cheia */
static void bBotao(const char* label, int x, int y, int w, int h,
                   uint16_t borda, uint16_t fundo, uint16_t texto) {
  bRound(x, y, w, h, 8, fundo);
  bFrame(x, y, w, h, 8, borda);
  bTxt(label, x + w / 2, y + h / 2, texto, FB12, MC_DATUM);
}

/* ---------------------------------------------------------------- */
/* 11. QR DECORATIVO                                                 */
/*     Mesmo algoritmo do .dc.html: LCG com semente fixa + os tres   */
/*     quadrados localizadores. NAO e um QR real - nao decodifica.   */
/* ---------------------------------------------------------------- */
#define QR_N 29
static uint8_t QR[QR_N][4];
static bool    qrPronto = false;

static bool qrAnel(int x, int y, int r, int c) {
  return (r == y) || (r == y + 6) || (c == x) || (c == x + 6) ||
         (r >= y + 2 && r <= y + 4 && c >= x + 2 && c <= x + 4);
}

static void qrBuild() {
  if (qrPronto) return;
  uint64_t seed = 20260824ULL;
  const int cor[3][2] = { {0,0}, {QR_N-7,0}, {0,QR_N-7} };
  memset(QR, 0, sizeof(QR));
  for (int r = 0; r < QR_N; r++) {
    for (int c = 0; c < QR_N; c++) {
      int fx = -1, fy = -1;
      for (int k = 0; k < 3; k++)
        if (c >= cor[k][0] && c < cor[k][0] + 7 && r >= cor[k][1] && r < cor[k][1] + 7) {
          fx = cor[k][0]; fy = cor[k][1];
        }
      bool on;
      if (fx >= 0) on = qrAnel(fx, fy, r, c);
      else {
        seed = (seed * 1103515245ULL + 12345ULL) & 0x7fffffffULL;
        on = ((double)seed / 2147483647.0) > 0.52;
      }
      if (on) QR[r][c >> 3] |= (uint8_t)(1 << (c & 7));
    }
  }
  qrPronto = true;
}

static void bQR(int x, int y, int modulo) {
  int lado = QR_N * modulo, quiet = modulo * 2;
  bFill(x, y, lado + quiet * 2, lado + quiet * 2, C_TEXT);   // fundo claro
  for (int r = 0; r < QR_N; r++)
    for (int c = 0; c < QR_N; c++)
      if (QR[r][c >> 3] & (1 << (c & 7)))
        bFill(x + quiet + c * modulo, y + quiet + r * modulo, modulo, modulo, C_BG);
}

/* ---------------------------------------------------------------- */
/* 12. SIMULACAO / INGESTAO SERIAL                                   */
/* ---------------------------------------------------------------- */
static const float SHARE[3] = { 0.42f, 0.31f, 0.27f };

static void computeBays() {
  const char* c0 = CARS[S.hero].name;
  const char* c1 = CARS[(S.hero + 1) % 3].name;

  BAYS[0] = { "C01", "CARREGANDO", C_RED,  "", S.kw * SHARE[0], S.soc, true };
  snprintf(BAYS[0].car, sizeof(BAYS[0].car), "%s", c0);

  BAYS[1] = { "C02", "CARREGANDO", C_RED,  "", S.kw * SHARE[1], 78, true };
  snprintf(BAYS[1].car, sizeof(BAYS[1].car), "%s", c1);

  BAYS[2] = { "C03", "LIMITADO",   C_WARN, "", S.kw * SHARE[2], 41, true };
  snprintf(BAYS[2].car, sizeof(BAYS[2].car), "%s", "Chevrolet Bolt EV");

  BAYS[3] = { "C04", "LIVRE",      C_DIM2, "", 0.0f, -1, false };
  snprintf(BAYS[3].car, sizeof(BAYS[3].car), "%s", "Disponivel - CCS2");
}

/* valores derivados da sessao do motorista */
static float sesFaltamKwh() { return (100 - S.sSoc) / 100.0f * (float)CAPACIDADE; }
static int   sesMinutos()   { return max(1, (int)lroundf(sesFaltamKwh() / 44.0f * 60.0f)); }
static float sesSubtotal()  { return S.sEntregue * PRECO_KWH; }
static float sesTotal()     { return sesSubtotal() + TAXA_REDE; }

static void tick() {
  S.t++;
  S.ago = (int)(S.t % 5);

  if (S.serialLive && millis() - S.lastPkt > 15000) {
    S.serialLive = false;
    Serial.println("INFO ponte serial silenciosa, retomando simulacao local");
  }

  /* sessao do motorista (mesma logica do Sessao.dc.html) */
  if (S.sSoc < 100 && !S.payDone) {
    uint32_t seg = S.sSeg;
    S.sSeg++;
    if (seg % 4 == 3) S.sSoc++;
    S.sEntregue += 0.012f;
  }
  /* contagem de expiracao do PIX */
  if (!S.payDone && S.payExpira > 0 && S.mode == MODE_MOTORISTA && S.scr == SCR_PAGAMENTO)
    S.payExpira--;

  if (!S.playing) return;

  if (!S.serialLive) {
    S.kw   = constrain(S.kw   + randRange(-8.0f, 8.0f),   22.0f, 105.0f);
    S.temp = constrain(S.temp + randRange(-1.0f, 1.1f),   30.0f,  56.0f);
    S.eff  = constrain(S.eff  + randRange(-0.35f, 0.35f), 92.0f,  99.0f);
    S.kwh += S.kw / 900.0f;
  }
  int socAnt = S.soc;
  S.soc  = (socAnt >= 99) ? 36 : socAnt + 1;
  S.mins = (socAnt >= 99) ? 1  : S.mins + ((S.t % 4 == 0) ? 1 : 0);
  if (socAnt >= 99) S.hero = (S.hero + 1) % 3;

  computeBays();
}

static void aplicarJson(const char* txt) {
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, txt);
  if (e) { Serial.printf("ERRO json: %s\n", e.c_str()); return; }

  if (!doc["potencia_total_kw"].isNull())          S.kw   = doc["potencia_total_kw"].as<float>();
  if (!doc["temperatura_c"].isNull())              S.temp = doc["temperatura_c"].as<float>();
  if (!doc["energia_entregue_hoje_kwh"].isNull())  S.kwh  = doc["energia_entregue_hoje_kwh"].as<float>();
  if (!doc["eficiencia_pct"].isNull())             S.eff  = doc["eficiencia_pct"].as<float>();
  if (!doc["tempo_medio_sessao_min"].isNull())     S.mins = doc["tempo_medio_sessao_min"].as<int>();
  if (!doc["veiculos_carregando"].isNull())        S.veicCarregando = doc["veiculos_carregando"].as<int>();
  if (!doc["status_sistema"].isNull())
    snprintf(S.statusSis, sizeof(S.statusSis), "%s", doc["status_sistema"].as<const char*>());

  computeBays();
  JsonObject vd = doc["veiculo_destaque"];
  if (!vd.isNull()) {
    const char* marca  = vd["marca"]  | "";
    const char* modelo = vd["modelo"] | "";
    if (marca[0] || modelo[0])
      snprintf(BAYS[0].car, sizeof(BAYS[0].car), "%s %s", marca, modelo);
  }

  S.serialLive = true;
  S.lastPkt = millis();
  dirty = true; chromeDirty = true;

  Serial.printf("OK desenhado: %.1f kW | %.1f C | %.1f kWh\n", S.kw, S.temp, S.kwh);
}

/* ---------------------------------------------------------------- */
/* 13. SECOES DO DASHBOARD DO OPERADOR                               */
/* ---------------------------------------------------------------- */
static inline bool secSkip(int y, int h) {
  return !g_measure && ((y - g_bandTop >= BAND_H) || (y + h - g_bandTop <= 0));
}

static void tituloSecao(const char* t, const char* dir, int y) {
  bTxt(t, PAD, y, C_TEXT, F9);
  if (dir) bTxt(dir, RGT, y + 5, C_DIM2, FS1, TR_DATUM);
}

/* --- HERO: anel de potencia + 3 vitais ----------------------------- */
static int secHero(int y) {
  const int H = 124;
  if (secSkip(y, H + 10)) return y + H + 10;
  bRound(PAD, y, CW, H, 8, C_PANEL);

  int cx = PAD + 52, cy = y + 60, r = 38;
  float pct = constrain(S.kw / 105.0f, 0.0f, 1.0f);
  bArc(cx, cy, r, 7, 0, 360, C_LINE);
  bArc(cx, cy, r, 7, 0, 360.0f * pct, C_RED);

  bTxt("POTENCIA", cx, cy - 26, C_DIM2, FS1, MC_DATUM);
  bTxt(nf(S.kw).c_str(), cx, cy - 3, C_TEXT, FB12, MC_DATUM);
  bTxt("kW", cx, cy + 18, C_DIM, FS1, MC_DATUM);

  int x0 = PAD + 100;
  bTxt("VILA OLIMPIA - HCA-G2 - SP", x0, y + 12, C_DIM2);

  bool ok = (strcmp(S.statusSis, "OPERACIONAL") == 0);
  bDot(x0 + 4, y + 32, 4, ok ? C_OK : C_RED2);
  bTxt(ok ? "Operacional" : S.statusSis, x0 + 14, y + 23, C_TEXT, FB12);
  bHLine(x0, y + 46, RGT - x0, C_LINE);

  uint16_t tc = (S.temp > LIMITE_TERM + 4) ? C_RED : (S.temp > LIMITE_TERM) ? C_WARN : C_OK;
  const char* tl = (S.temp > LIMITE_TERM + 4) ? "critica" : (S.temp > LIMITE_TERM) ? "elevada" : "nominal";

  const char* L[3]  = { "TEMPERATURA", "EFICIENCIA", "ENERGIA HOJE" };
  String      V[3]  = { nf(S.temp) + " C", nf(S.eff) + " %", nf(S.kwh) + " kWh" };
  String      N[3]  = { String(tl), String("AC->DC"), brl(S.kwh * PRECO_KWH) };
  uint16_t    VC[3] = { tc, C_TEXT, C_TEXT };

  for (int i = 0; i < 3; i++) {
    int vx = x0 + (i % 2) * 100, vy = y + 52 + (i / 2) * 34;
    bTxt(L[i], vx, vy, C_DIM2);
    bTxt(V[i].c_str(), vx, vy + 9, VC[i], FB9);
    bTxt(fitTxt(N[i].c_str(), FS1, 94).c_str(), vx, vy + 25, C_DIM);
  }
  return y + H + 10;
}

/* --- CURVA DE CARGA 24 H -------------------------------------------- */
static int secCurva(int y) {
  const int H = 92;
  if (secSkip(y, H)) return y + H;
  float media = 0; for (int i = 0; i < 24; i++) media += DAY[i]; media /= 24.0f;
  char sub[32]; snprintf(sub, sizeof(sub), "media %s kW", nf(media).c_str());
  tituloSecao("Curva de carga - 24 h", sub, y);

  int top = y + 20, h = 56, base = top + h, prev = base;
  bHLine(PAD, top + 14, CW, C_LINE);
  bHLine(PAD, top + 38, CW, C_LINE);

  for (int i = 0; i < CW; i++) {
    float p = (float)i / (float)(CW - 1) * 23.0f;
    int i0 = (int)p, i1 = min(23, i0 + 1);
    float f = p - i0;
    float v = DAY[i0] * (1 - f) + DAY[i1] * f;
    int yy = base - (int)(v / 110.0f * h);
    bFill(PAD + i, yy, 1, base - yy, C_AREA);
    if (i > 0) bLine(PAD + i - 1, prev, PAD + i, yy, C_RED2);
    prev = yy;
  }
  const char* hl[5] = { "00h", "06h", "12h", "18h", "23h" };
  for (int i = 0; i < 5; i++)
    bTxt(hl[i], PAD + i * (CW - 18) / 4, base + 6, C_DIM2);
  return y + H;
}

/* --- CONECTORES C01..C04 (tocaveis, sem o SOC) ---------------------- */
static int secBays(int y) {
  const int H = 58, W = 72;
  if (secSkip(y, H + 10)) return y + H + 10;
  for (int i = 0; i < 4; i++) {
    int x = PAD + i * 76;
    addHit(HIT_BAY + i, x, y, W, H);
    bool sel = (S.selBay == i);
    if (sel) bRound(x, y, W, H, 5, C_REDSOFT);
    bFill(x, y, W, 2, BAYS[i].color);
    if (sel) bFrame(x, y, W, H, 5, C_RED2);

    bDot(x + 8, y + 13, 3, BAYS[i].color);
    bTxt(BAYS[i].id, x + 16, y + 9, C_TEXT, FS1);
    bTxt(BAYS[i].state, x + 6, y + 22, BAYS[i].color);
    bTxt(fitTxt(BAYS[i].car, FS1, W - 10).c_str(), x + 6, y + 32, C_DIM);
    bTxt(nf(BAYS[i].kw).c_str(), x + 6, y + 43, C_TEXT, FB9);
    bTxt("kW", x + 6 + txtW(nf(BAYS[i].kw).c_str(), FB9) + 4, y + 48, C_DIM2);
  }
  return y + H + 10;
}

/* --- SESSAO EM CURSO (sem "decorrido") ------------------------------ */
static int secAgora(int y) {
  const int H = 96;
  if (secSkip(y, H + 10)) return y + H + 10;
  bRound(PAD, y, CW, H, 8, C_PANEL);
  BayInfo& b = BAYS[S.selBay];

  char t[56];
  snprintf(t, sizeof(t), "%s - %s - CCS2",
           b.live ? "CARREGANDO AGORA" : "CONECTOR LIVRE", b.id);
  bTxt(t, PAD + 12, y + 10, b.live ? C_RED2 : C_DIM2);
  bTxt(fitTxt(b.car, FB12, CW - 26).c_str(), PAD + 12, y + 21, C_TEXT, FB12);

  int bx = PAD + 12, bw = CW - 24 - 46, by = y + 46;
  bRound(bx, by, bw, 7, 3, C_LINE);
  if (b.soc > 0) bRound(bx, by, bw * b.soc / 100, 7, 3, C_RED2);
  if (b.soc >= 0) { snprintf(t, sizeof(t), "%d%%", b.soc); bTxt(t, RGT - 12, by - 5, C_TEXT, FB9, TR_DATUM); }
  else            bTxt("--", RGT - 12, by - 5, C_DIM2, FB9, TR_DATUM);

  const Car& c = CARS[S.hero];
  int eta = max(2, (int)lroundf((100 - (b.soc < 0 ? 100 : b.soc)) * 0.55f));
  const char* L[3] = { "RESTAM", "ENTREGUE", "VALOR" };
  String V[3] = { String(eta) + " min", nf(c.kwh) + " kWh", brl(c.kwh * PRECO_KWH) };
  for (int i = 0; i < 3; i++) {
    int x = PAD + 12 + i * 92;
    bTxt(L[i], x, y + 64, C_DIM2);
    bTxt(V[i].c_str(), x, y + 74, C_TEXT, FB9);
  }
  return y + H + 10;
}

/* --- KPIs FINANCEIROS ----------------------------------------------- */
static int secKpis(int y) {
  const int H = 168;                     // = 18 + 3*48 + 6
  if (secSkip(y, H)) return y + H;
  float receita = S.kwh * PRECO_KWH;
  float custo   = S.kwh * TARIFA_KWH;
  float margem  = receita > 0 ? (receita - custo) / receita * 100.0f : 0;

  const char* L[6] = { "RECEITA HOJE", "TICKET MEDIO", "CUSTO DE ENERGIA",
                       "MARGEM BRUTA", "REPASSE FRANQUEADO", "OCUPACAO MEDIA" };
  String V[6] = { brl(receita), brl(receita / 24.0f), brl(custo),
                  nf(margem) + " %", brl(receita * COMISSAO / 100.0f), "68 %" };
  String N[6] = { "+12,4% vs. ontem", "24 sessoes concluidas", "tarifa R$ 0,92/kWh",
                  brl(receita - custo), String(COMISSAO) + "% - liquidacao D+1", "pico 94% as 19h" };
  uint16_t C[6] = { C_OK, C_DIM, C_DIM, C_OK, C_DIM, C_RED2 };

  tituloSecao("Indicadores do dia", nullptr, y);
  int top = y + 18;
  for (int i = 0; i < 6; i++) {
    int x = PAD + (i % 2) * 152, ky = top + (i / 2) * 48;
    bRound(x, ky, 148, 44, 5, C_PANEL);
    bTxt(L[i], x + 8, ky + 6, C_DIM2);
    bTxt(V[i].c_str(), x + 8, ky + 15, C_TEXT, FB12);
    bTxt(fitTxt(N[i].c_str(), FS1, 134).c_str(), x + 8, ky + 34, C[i]);
  }
  return y + H;
}

/* --- SESSOES RECENTES (3 linhas, sem CX) ---------------------------- */
static int secSessoes(int y) {
  const int H = 22 + 16 + N_SESSOES * 19 + 12;
  if (secSkip(y, H)) return y + H;
  tituloSecao("Sessoes recentes", "24 hoje - 3 exibidas", y);
  int hy = y + 22;
  bTxt("HORA",    PAD,       hy, C_DIM2);
  bTxt("VEICULO", PAD + 42,  hy, C_DIM2);
  bTxt("kWh",     PAD + 200, hy, C_DIM2, FS1, TR_DATUM);
  bTxt("R$",      PAD + 250, hy, C_DIM2, FS1, TR_DATUM);
  bTxt("PG",      PAD + 262, hy, C_DIM2);
  bHLine(PAD, hy + 11, CW, C_LINE);

  for (int i = 0; i < N_SESSOES; i++) {
    const Sess& r = SESSOES[i];
    int ry = hy + 16 + i * 19;
    bTxt(r.hora, PAD, ry + 3, C_TEXT);
    bTxt(fitTxt(r.car, F9, 150).c_str(), PAD + 42, ry, C_TEXT, F9);
    bTxt(nf(r.kwh).c_str(), PAD + 200, ry + 3, C_TEXT, FS1, TR_DATUM);
    bTxt(brl(r.kwh * PRECO_KWH, false).c_str(), PAD + 250, ry + 3, C_TEXT, FS1, TR_DATUM);
    bDot(PAD + 264, ry + 6, 3, PG_COLOR[r.pg]);
    bTxt(PG_NAME[r.pg], PAD + 271, ry + 3, PG_COLOR[r.pg]);
    bHLine(PAD, ry + 16, CW, C_LINE);
  }
  return y + H;
}

/* --- SESSOES POR HORA ------------------------------------------------ */
static int secHoras(int y) {
  const int H = 100;                     // = 20 + 58 + 22
  if (secSkip(y, H)) return y + H;
  tituloSecao("Sessoes por hora - rede", nullptr, y);
  int top = y + 20, h = 58, base = top + h;
  int maxv = 1; for (int i = 0; i < 24; i++) maxv = max(maxv, (int)SESS_H[i]);
  for (int i = 0; i < 24; i++) {
    int bh = max(3, SESS_H[i] * h / maxv);
    uint16_t c = SESS_H[i] >= 8 ? C_RED : SESS_H[i] >= 5 ? 0x8001 : C_LINE;
    bFill(PAD + 6 + i * 12, base - bh, 10, bh, c);
  }
  bTxt("00h", PAD + 6, base + 6, C_DIM2);
  bTxt("12h", PAD + 138, base + 6, C_DIM2);
  bTxt("23h", RGT, base + 6, C_DIM2, FS1, TR_DATUM);
  return y + H;
}

/* --- METODOS DE PAGAMENTO -------------------------------------------- */
static int secPagamentos(int y) {
  const int H = 148;                     // = 22 + 4*30 + 6
  if (secSkip(y, H)) return y + H;
  tituloSecao("Metodos de pagamento", "83 sessoes", y);
  int top = y + 22;
  for (int i = 0; i < 4; i++) {
    const Metodo& m = METODOS[i];
    int ry = top + i * 30;
    bTxt(m.label, PAD, ry, C_TEXT, F9);
    bTxt(m.n, PAD + 230, ry + 4, C_DIM, FS1, TR_DATUM);
    bTxt((nf(m.pct) + "%").c_str(), RGT, ry + 1, C_TEXT, FB9, TR_DATUM);
    bRound(PAD, ry + 17, CW, 5, 2, C_LINE);
    bRound(PAD, ry + 17, (int)(CW * m.pct / 100.0f), 5, 2, PG_COLOR[m.pg]);
  }
  return y + H;
}

/* --- TABELA DA REDE --------------------------------------------------- */
static int secRede(int y) {
  const int H = 252;
  if (secSkip(y, H)) return y + H;
  char sub[32];
  snprintf(sub, sizeof(sub), "comissao %d%%", COMISSAO);
  tituloSecao("Rede - por eletroposto", sub, y);
  int top = y + 22;
  bHLine(PAD, top - 4, CW, C_LINE);

  float tKw = 0, tKwh = 0, tUp = 0; int tSess = 0, nUp = 0;
  for (int i = 0; i < 6; i++) {
    const Node& n = REDE[i];
    float kw   = (i == 0) ? S.kw   : n.kw;
    float kwh  = (i == 0) ? S.kwh  : n.kwh;
    float temp = (i == 0) ? S.temp : n.temp;
    tKw += kw; tKwh += kwh; tSess += n.sess;
    if (n.up > 0) { tUp += n.up; nUp++; }

    int ry = top + i * 32;
    bTxt(n.nome, PAD, ry, C_TEXT, F9);
    bTxt(brl(kwh * PRECO_KWH).c_str(), RGT, ry + 1, C_TEXT, FB9, TR_DATUM);
    bDot(PAD + 3, ry + 20, 3, n.color);
    bTxt(n.estado, PAD + 10, ry + 17, n.color);

    char d[80];
    if (n.up > 0)
      snprintf(d, sizeof(d), "%skW %ds %skWh %s%% %sC",
               nf(kw).c_str(), n.sess, nf(kwh).c_str(), nf(n.up).c_str(), nf(temp).c_str());
    else
      snprintf(d, sizeof(d), "fora de operacao - OS-4471");
    bTxt(fitTxt(d, FS1, CW - 92).c_str(), RGT, ry + 17, C_DIM, FS1, TR_DATUM);
    bHLine(PAD, ry + 28, CW, C_LINE);
  }

  int ty = top + 6 * 32 + 4;
  bTxt("TOTAL DA REDE", PAD, ty, C_TEXT, FB9);
  bTxt(brl(tKwh * PRECO_KWH).c_str(), RGT, ty + 1, C_RED2, FB9, TR_DATUM);
  char d[128];
  snprintf(d, sizeof(d), "%s kW - %d sessoes - %s kWh - uptime %s%% - repasse %s",
           nf(tKw).c_str(), tSess, nf(tKwh).c_str(),
           nf(nUp ? tUp / nUp : 0.0f).c_str(),
           brl(tKwh * PRECO_KWH * COMISSAO / 100.0f).c_str());
  bTxt(fitTxt(d, FS1, CW).c_str(), PAD, ty + 18, C_DIM2);
  return y + H;
}

static int secRodape(int y) {
  bHLine(PAD, y, CW, C_LINE);
  char b[72];
  snprintf(b, sizeof(b), "ESP32 CYD - %s - t+%ds",
           S.serialLive ? "ponte USB COM7 @115200" : "simulacao local", S.ago);
  bTxt(b, PAD, y + 8, C_DIM2);
  bTxt("GOODWE ENERGIA - CHARGEGRID INTELLIGENCE v1.1", PAD, y + 20, C_DIM2);
  return y + 40;
}

/* Pagina unica do operador: tudo numa rolagem so, sem abas.
   A ordem vai do operacional (agora) para o gerencial (fechamento). */
static int renderDash() {
  int y = 8;
  y = secHero(y);
  y = secCurva(y);
  y = secBays(y);
  y = secAgora(y);
  y = secSessoes(y);
  y = secHoras(y);
  return secRodape(y);
  /* Removidos do painel a pedido: secKpis ("Indicadores do dia"),
     secPagamentos ("Metodos de pagamento") e secRede ("Rede - por
     eletroposto"). As funcoes continuam no arquivo, logo acima; para
     trazer qualquer uma de volta basta reinserir a chamada aqui, na
     posicao desejada. Sem chamada, o compilador as descarta e elas nao
     ocupam flash. */
}

/* ---------------------------------------------------------------- */
/* 14. TELAS DO MOTORISTA (tela cheia 320x240, sem rolagem)          */
/* ---------------------------------------------------------------- */

/* cabecalho comum das telas do motorista */
static void drvHeader(const char* titulo, const char* direita, bool comVoltar, int hitVoltar) {
  int x = PAD;
  if (comVoltar) {
    addHit(hitVoltar, 0, 0, 54, 28);
    bChevron(6, 6, 12, C_DIM);
    x = 24;
  }
  bTxt("GOODWE", x, 6, C_TEXT, FB9);
  int lw = txtW("GOODWE", FB9);
  bVLine(x + lw + 8, 7, 12, C_LINE);
  bTxt(titulo, x + lw + 16, 10, C_DIM);
  if (direita) bTxt(direita, RGT, 10, C_DIM2, FS1, TR_DATUM);
  bHLine(0, 23, SCR_W, C_LINE);
}

/* --- TELA 1: SESSAO EM CARREGAMENTO --------------------------------- */
static void scrSessao() {
  drvHeader("CARREGANDO", "VILA OLIMPIA - C01", true, HIT_SES_OPER);

  /* anel: tempo restante */
  int cx = 76, cy = 92, r = 46;
  float pct = constrain(S.sSoc / 100.0f, 0.0f, 1.0f);
  bArc(cx, cy, r, 6, 0, 360, C_LINE);
  bArc(cx, cy, r, 6, 0, 360.0f * pct, C_RED);

  bTxt("RESTANTE", cx, cy - 28, C_DIM2, FS1, MC_DATUM);
  String mins = String(sesMinutos());
  bTxt(mins.c_str(), cx, cy + 2, C_TEXT, FB18, MC_DATUM);
  bTxt("min", cx, cy + 26, C_DIM, FS1, MC_DATUM);

  /* pilula de bateria */
  char bat[24]; snprintf(bat, sizeof(bat), "%d%% DA BATERIA", S.sSoc);
  int pw = txtW(bat, FS1) + 26, px = cx - pw / 2, py = 146;
  bRound(px, py, pw, 18, 9, C_OKSOFT);
  bFrame(px, py, pw, 18, 9, C_OK);
  bDot(px + 11, py + 9, 3, C_OK);
  bTxt(bat, px + 18, py + 6, C_OK);

  /* coluna direita: faltam / custo */
  int x0 = 152;
  bTxt("FALTAM", x0, 32, C_DIM2);
  String falt = nf(sesFaltamKwh());
  bTxt(falt.c_str(), x0, 42, C_TEXT, FB18);
  bTxt("kWh", x0 + txtW(falt.c_str(), FB18) + 6, 58, C_DIM);
  char ja[40]; snprintf(ja, sizeof(ja), "%s kWh ja carregados", nf(S.sEntregue).c_str());
  bTxt(ja, x0, 72, C_DIM);

  bHLine(x0, 86, RGT - x0, C_LINE);

  bTxt("CUSTO ATUAL", x0, 94, C_DIM2);
  bTxt("R$", x0, 118, C_DIM);
  String cst = nf(sesSubtotal(), 2);
  bTxt(cst.c_str(), x0 + 20, 104, C_RED2, FB18);
  char pk[32]; snprintf(pk, sizeof(pk), "R$ %s por kWh", nf(PRECO_KWH, 2).c_str());
  bTxt(pk, x0, 134, C_DIM);

  /* barra de progresso + horarios */
  bRound(PAD, 172, CW, 5, 2, C_LINE);
  bRound(PAD, 172, (int)(CW * pct), 5, 2, C_RED2);
  char ini[24], fim[32];
  snprintf(ini, sizeof(ini), "Inicio %s", hhmm(0).c_str());
  snprintf(fim, sizeof(fim), "Conclusao prevista %s",
           hhmm(sesMinutos() + (int)(S.sSeg / 60)).c_str());
  bTxt(ini, PAD, 182, C_DIM2);
  bTxt(fim, RGT, 182, C_DIM2, FS1, TR_DATUM);

  /* botao principal */
  addHit(HIT_SES_PAGAR, PAD, 194, CW, 34);
  bBotao("ENCERRAR E PAGAR", PAD, 194, CW, 34, C_RED, C_REDSOFT, C_TEXT);
  bTxt("O cabo destrava automaticamente apos o pagamento.",
       SCR_W / 2, 231, C_DIM2, FS1, TC_DATUM);
}

/* --- TELA 2: PAGAMENTO ---------------------------------------------- */

/* 2a. escolha do metodo */
static void payEscolha() {
  drvHeader("PAGAMENTO", "C01 - CCS2", true, HIT_PAY_BACK);

  /* total */
  bRound(PAD, 28, CW, 46, 8, C_PANEL);
  bTxt("TOTAL A PAGAR", PAD + 12, 34, C_DIM2);
  bTxt("R$", PAD + 12, 58, C_DIM);
  String tot = nf(sesTotal(), 2);
  bTxt(tot.c_str(), PAD + 32, 44, C_TEXT, FB18);

  char l1[48], l2[40], l3[40];
  snprintf(l1, sizeof(l1), "%s kWh x R$ %s", nf(S.sEntregue).c_str(), nf(PRECO_KWH, 2).c_str());
  snprintf(l2, sizeof(l2), "+ taxa de rede R$ %s", nf(TAXA_REDE, 2).c_str());
  snprintf(l3, sizeof(l3), "34 min - VLO-2608-1418");
  bTxt(l1, RGT - 12, 36, C_DIM,  FS1, TR_DATUM);
  bTxt(l2, RGT - 12, 48, C_DIM2, FS1, TR_DATUM);
  bTxt(l3, RGT - 12, 60, C_DIM2, FS1, TR_DATUM);

  bTxt("ESCOLHA O METODO", PAD, 82, C_DIM2);

  /* 4 botoes grandes 2x2 */
  for (int i = 0; i < 4; i++) {
    int bx = PAD + (i % 2) * 152, by = 94 + (i / 2) * 54;
    addHit(HIT_PAY_MET + i, bx, by, 148, 48);
    bRound(bx, by, 148, 48, 8, C_PANEL);
    bFrame(bx, by, 148, 48, 8, C_LINE);
    bFill(bx + 8, by + 8, 3, 32, PG_COLOR[i]);
    bTxt(PG_FULL[i], bx + 18, by + 10, C_TEXT, FB9);
    bTxt(fitTxt(PG_SUB[i], FS1, 118).c_str(), bx + 18, by + 30, C_DIM2);
  }
  bTxt("Conector travado ate a quitacao", SCR_W / 2, 208, C_DIM2, FS1, TC_DATUM);
}

/* 2b. PIX */
static void payPix() {
  char tot[24]; snprintf(tot, sizeof(tot), "R$ %s", nf(sesTotal(), 2).c_str());
  drvHeader("PIX", tot, true, HIT_PAY_BACK);

  bQR(PAD, 30, 4);                       // 29*4 + 2*8 = 132 px
  int x0 = 152;
  bTxt("Aponte a camera", x0, 32, C_TEXT, F9);
  bTxt("QR dinamico do PSP.", x0, 54, C_DIM);
  bTxt("Baixa em ate 3 s.", x0, 64, C_DIM);

  char exp[32];
  snprintf(exp, sizeof(exp), "Expira em %d:%02d", max(0, S.payExpira) / 60, max(0, S.payExpira) % 60);
  bRound(x0, 76, 130, 20, 6, C_WARNSOFT);
  bFrame(x0, 76, 130, 20, 6, C_WARN);
  bTxt(exp, x0 + 10, 82, C_WARN, FS1);

  addHit(HIT_PAY_COPIA, x0, 104, 150, 26);
  bRound(x0, 104, 150, 26, 6, C_PANEL);
  bFrame(x0, 104, 150, 26, 6, C_LINE);
  bTxt(S.payCopiado ? "Codigo copiado" : "Copiar codigo PIX",
       x0 + 75, 117, S.payCopiado ? C_OK : C_TEXT, FS1, MC_DATUM);

  bDot(x0 + 4, 145, 3, (S.t % 2) ? C_RED : C_REDSOFT);
  bTxt("Aguardando o banco...", x0 + 12, 141, C_DIM);

  addHit(HIT_PAY_OK, PAD, 172, CW, 34);
  bBotao("SIMULAR CONFIRMACAO DO PIX", PAD, 172, CW, 34, C_RED, C_REDSOFT, C_TEXT);
  bTxt(fitTxt("00020126580014br.gov.bcb.pix0136goodwe.chargegrid@psp.com.br",
              FS1, CW).c_str(), SCR_W / 2, 213, C_DIM2, FS1, TC_DATUM);
}

/* 2c. Apple Pay / Google Pay */
static void payCarteira() {
  char tot[24]; snprintf(tot, sizeof(tot), "R$ %s", nf(sesTotal(), 2).c_str());
  drvHeader(S.payMet == PG_GPAY ? "GOOGLE PAY" : "APPLE PAY", tot, true, HIT_PAY_BACK);

  int cx = SCR_W / 2, cy = 104;
  uint16_t pulso = (S.t % 2) ? C_RED2 : C_REDGLOW;
  bRing(cx, cy, 54, C_REDGLOW);
  bRing(cx, cy, 40, pulso);
  bDot(cx, cy, 26, C_REDSOFT);
  /* glifo de celular */
  bRound(cx - 9, cy - 16, 18, 32, 4, C_RED2);
  bFill(cx - 6, cy - 12, 12, 22, C_BG);

  bTxt("Aproxime o dispositivo do leitor", cx, 168, C_TEXT, F9, TC_DATUM);
  char sub[64];
  snprintf(sub, sizeof(sub), "%s - autenticacao no proprio aparelho", PG_FULL[S.payMet]);
  bTxt(sub, cx, 190, C_DIM, FS1, TC_DATUM);

  addHit(HIT_PAY_OK, PAD, 202, CW, 32);
  bBotao("SIMULAR APROXIMACAO", PAD, 202, CW, 32, C_RED, C_REDSOFT, C_TEXT);
}

/* 2d. PayPal */
static void payPaypal() {
  char tot[24]; snprintf(tot, sizeof(tot), "R$ %s", nf(sesTotal(), 2).c_str());
  drvHeader("PAYPAL", tot, true, HIT_PAY_BACK);

  bTxt("Conta PayPal", PAD, 32, C_TEXT, FB12);
  char sub[64];
  snprintf(sub, sizeof(sub), "Cobranca unica de R$ %s.", nf(sesTotal(), 2).c_str());
  bTxt(sub, PAD, 52, C_DIM);
  bTxt("Nada e salvo no eletroposto.", PAD, 62, C_DIM2);

  bTxt("E-MAIL DA CONTA", PAD, 74, C_DIM2);
  bRound(PAD, 86, CW, 30, 6, C_PANEL);
  bFrame(PAD, 86, CW, 30, 6, C_LINE);
  bTxt("motorista@exemplo.com.br", PAD + 12, 95, C_TEXT, F9);

  bRound(PAD, 124, CW, 38, 6, C_PANEL);
  bTxt("Voce sera redirecionado ao app do PayPal.", PAD + 10, 132, C_DIM);
  bTxt("A sessao fica reservada por 5 minutos.", PAD + 10, 145, C_DIM2);

  addHit(HIT_PAY_OK, PAD, 174, CW, 34);
  bBotao("CONTINUAR PARA O PAYPAL", PAD, 174, CW, 34, C_RED, C_REDSOFT, C_TEXT);
}

/* 2e. aprovado */
static void payAprovado() {
  drvHeader("PAGAMENTO", "C01 - CCS2", false, 0);

  bCheck(SCR_W / 2, 58, 22, C_OK);
  bTxt("Pagamento aprovado", SCR_W / 2, 88, C_TEXT, FB12, TC_DATUM);
  char sub[64];
  snprintf(sub, sizeof(sub), "R$ %s via %s - conector liberado",
           nf(sesTotal(), 2).c_str(), PG_FULL[S.payMet < 0 ? 0 : S.payMet]);
  bTxt(sub, SCR_W / 2, 110, C_DIM, FS1, TC_DATUM);

  bHLine(PAD + 30, 126, CW - 60, C_LINE);
  const char* L[3] = { "Autorizacao", "Recibo enviado a", "Repasse ao franqueado - D+1" };
  char autz[32]; snprintf(autz, sizeof(autz), "AUT-20260824-%d", 1418 + (int)(S.t % 7));
  String V[3] = { String(autz), "motorista@exemplo.com.br",
                  brl(sesSubtotal() * COMISSAO / 100.0f) };
  for (int i = 0; i < 3; i++) {
    bTxt(L[i], PAD + 30, 136 + i * 16, C_DIM);
    bTxt(V[i].c_str(), RGT - 30, 136 + i * 16, C_TEXT, FS1, TR_DATUM);
  }

  addHit(HIT_PAY_NOVA, PAD, 194, CW, 34);
  bBotao("NOVA SESSAO", PAD, 194, CW, 34, C_OK, C_OKSOFT, C_TEXT);
}

static void scrPagamento() {
  if (S.payDone)          payAprovado();
  else if (S.payMet < 0)  payEscolha();
  else if (S.payMet == PG_PIX)  payPix();
  else if (S.payMet == PG_PPAL) payPaypal();
  else                          payCarteira();
}

static void renderDriver() {
  if (S.scr == SCR_SESSAO) scrSessao();
  else                     scrPagamento();
}

/* ---------------------------------------------------------------- */
/* 15. LAYOUT / HIT-TEST                                             */
/* ---------------------------------------------------------------- */
static void relayout() {
  g_measure = true; hitN = 0;
  if (S.mode == MODE_OPERADOR) {
    contentH = renderDash();
    int maxS = max(0, contentH - CONTENT_H);
    scrollY = constrain(scrollY, 0, maxS);
  } else {
    contentH = SCR_H;
    scrollY = 0;
    renderDriver();
  }
  g_measure = false;
}

/* ---------------------------------------------------------------- */
/* 16. PINTURA                                                       */
/* ---------------------------------------------------------------- */
static void drawChrome() {
  spr.fillSprite(C_BG);
  spr.setTextWrap(false);

  useFont(FB9); spr.setTextDatum(TL_DATUM); spr.setTextColor(C_TEXT);
  spr.drawString("GOODWE", 8, 8);
  int lw = spr.textWidth("GOODWE");
  spr.drawFastVLine(8 + lw + 8, 9, 13, C_LINE);
  useFont(FS1); spr.setTextColor(C_DIM);
  spr.drawString("TORRE DE CONTROLE", 8 + lw + 16, 13);

  char b[16]; snprintf(b, sizeof(b), "t+%ds", S.ago);
  spr.setTextColor(C_DIM2);
  spr.setTextDatum(TR_DATUM);
  spr.drawString(b, playX - 8, 13);
  spr.setTextDatum(TL_DATUM);

  uint16_t pc = S.playing ? C_RED : C_DIM2;
  spr.drawRoundRect(playX, playY, playW, playH, 10, pc);
  if (S.playing) {
    spr.fillRect(playX + 12, playY + 6, 3, 9, C_RED2);
    spr.fillRect(playX + 18, playY + 6, 3, 9, C_RED2);
  } else {
    spr.fillTriangle(playX + 12, playY + 6, playX + 12, playY + 15, playX + 21, playY + 10, C_TEXT);
  }
  spr.setTextColor(S.playing ? C_RED2 : C_TEXT);
  spr.drawString(S.playing ? "PAUSAR" : "SEGUIR", playX + 26, playY + 7);

  spr.drawFastHLine(0, HEADER_H - 1, SCR_W, C_LINE);

  /* pilula da pagina atual: toque nela volta ao topo da rolagem */
  spr.fillRoundRect(dashX, 34, dashW, 22, 11, C_REDSOFT);
  spr.drawRoundRect(dashX, 34, dashW, 22, 11, C_RED);
  useFont(F9);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(C_TEXT);
  spr.drawString("Dashboard", dashX + dashW / 2, 45);

  /* pilula que devolve o aparelho ao motorista */
  spr.drawRoundRect(motoX, 34, motoW, 22, 11, C_LINE);
  spr.setTextColor(C_DIM);
  spr.drawString("Motorista", motoX + motoW / 2, 45);

  spr.setTextDatum(TL_DATUM);
  spr.drawFastHLine(0, CHROME_H - 1, SCR_W, C_LINE);
  spr.pushSprite(0, 0);
}

static void drawScrollbar() {
  int maxS = max(0, contentH - CONTENT_H);
  if (maxS == 0) { tft.fillRect(SBAR_X, CONTENT_Y, 3, CONTENT_H, C_BG); return; }
  int th = max(20, CONTENT_H * CONTENT_H / contentH);
  int ty = CONTENT_Y + (CONTENT_H - th) * scrollY / maxS;
  tft.fillRect(SBAR_X, CONTENT_Y, 3, ty - CONTENT_Y, C_LINE);
  tft.fillRect(SBAR_X, ty, 3, th, C_RED2);
  tft.fillRect(SBAR_X, ty + th, 3, CONTENT_Y + CONTENT_H - (ty + th), C_LINE);
}

static void drawContent() {
  if (S.mode == MODE_OPERADOR) {
    for (int i = 0; i < N_BANDS; i++) {
      g_bandTop = scrollY + i * BAND_H;
      spr.fillSprite(C_BG);
      spr.setTextWrap(false);
      renderDash();
      spr.pushSprite(0, CONTENT_Y + i * BAND_H);
    }
    drawScrollbar();
  } else {
    for (int i = 0; i < N_BANDS_FULL; i++) {
      g_bandTop = i * BAND_H;
      spr.fillSprite(C_BG);
      spr.setTextWrap(false);
      renderDriver();
      spr.pushSprite(0, i * BAND_H);
    }
  }
}

/* ---------------------------------------------------------------- */
/* 17. DRIVER DE TOQUE (XPT2046 por software) + CALIBRACAO           */
/* ---------------------------------------------------------------- */
/* Bump de CAL_MAGIC invalida a calibracao gravada e forca uma nova.
   Bumpado ao trocar para ILI9342/rot=2 e de novo ao trocar o modelo de
   deteccao de toque. */
#define CAL_MAGIC 0xC8

struct Cal { bool swap; int xMin, xMax, yMin, yMax; };
static Cal cal = { true, 300, 3800, 300, 3800 };

static uint16_t xptRead(uint8_t cmd) {
  digitalWrite(T_CS, LOW);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(T_DIN, (cmd >> i) & 1);
    digitalWrite(T_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(2);
  }
  digitalWrite(T_CLK, HIGH); delayMicroseconds(2);
  digitalWrite(T_CLK, LOW);  delayMicroseconds(2);
  uint16_t v = 0;
  for (int i = 0; i < 12; i++) {
    digitalWrite(T_CLK, HIGH); delayMicroseconds(2);
    digitalWrite(T_CLK, LOW);  delayMicroseconds(2);
    v = (v << 1) | (digitalRead(T_DO) ? 1 : 0);
  }
  digitalWrite(T_CS, HIGH);
  return v;
}

static bool xptRaw(int& rx, int& ry) {
  if (digitalRead(T_IRQ) == HIGH) return false;
  int xs[3], ys[3];
  for (int i = 0; i < 3; i++) { ys[i] = xptRead(0x90); xs[i] = xptRead(0xD0); }
  auto med = [](int a, int b, int c) { return max(min(a, b), min(max(a, b), c)); };
  rx = med(xs[0], xs[1], xs[2]);
  ry = med(ys[0], ys[1], ys[2]);
  int z1 = xptRead(0xB0) >> 3;
  return (rx > 80 && ry > 80 && rx < 4000 && ry < 4000 && z1 > 8);
}

static bool touchRead(int& sx, int& sy) {
  int rx, ry;
  if (!xptRaw(rx, ry)) return false;
  int ax = cal.swap ? ry : rx;
  int ay = cal.swap ? rx : ry;
  sx = constrain((int)map(ax, cal.xMin, cal.xMax, 0, SCR_W - 1), 0, SCR_W - 1);
  sy = constrain((int)map(ay, cal.yMin, cal.yMax, 0, SCR_H - 1), 0, SCR_H - 1);
  return true;
}

static void calSave() {
  prefs.begin("cgcal", false);
  prefs.putUChar("magic", CAL_MAGIC);
  prefs.putBool("swap", cal.swap);
  prefs.putInt("xmin", cal.xMin); prefs.putInt("xmax", cal.xMax);
  prefs.putInt("ymin", cal.yMin); prefs.putInt("ymax", cal.yMax);
  prefs.end();
}
static bool calLoad() {
  prefs.begin("cgcal", true);
  bool ok = prefs.getUChar("magic", 0) == CAL_MAGIC;
  if (ok) {
    cal.swap = prefs.getBool("swap", true);
    cal.xMin = prefs.getInt("xmin", 300); cal.xMax = prefs.getInt("xmax", 3800);
    cal.yMin = prefs.getInt("ymin", 300); cal.yMax = prefs.getInt("ymax", 3800);
    /* o XPT2046 cobre ~3400 contagens de ponta a ponta; amplitude muito
       menor significa calibracao ruim - melhor refazer que ficar com
       metade da tela inalcancavel */
    if (abs(cal.xMax - cal.xMin) < 800 || abs(cal.yMax - cal.yMin) < 800) {
      Serial.printf("AVISO calibracao suspeita x[%d..%d] y[%d..%d], refazendo\n",
                    cal.xMin, cal.xMax, cal.yMin, cal.yMax);
      ok = false;
    }
  }
  prefs.end();
  if (ok) Serial.printf("CAL carregada swap=%d x[%d..%d] y[%d..%d]\n",
                        cal.swap, cal.xMin, cal.xMax, cal.yMin, cal.yMax);
  return ok;
}

static void calAlvo(int x, int y, uint16_t c) {
  tft.drawCircle(x, y, 10, c);
  tft.drawFastHLine(x - 14, y, 29, c);
  tft.drawFastVLine(x, y - 14, 29, c);
}

static void calibrar() {
  const int PX[4] = { 22, SCR_W - 22, SCR_W - 22, 22 };
  const int PY[4] = { 22, 22, SCR_H - 22, SCR_H - 22 };
  int rawx[4], rawy[4];

  for (int i = 0; i < 4; i++) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(C_TEXT, C_BG);
    tft.setFreeFont(F9);
    tft.drawString("Calibracao do toque", SCR_W / 2, SCR_H / 2 - 14);
    tft.setTextFont(1); tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
    char b[40]; snprintf(b, sizeof(b), "toque no alvo  %d/4", i + 1);
    tft.drawString(b, SCR_W / 2, SCR_H / 2 + 10);
    calAlvo(PX[i], PY[i], C_RED2);

    while (digitalRead(T_IRQ) == LOW) delay(10);
    delay(120);
    int rx = 0, ry = 0, n = 0; long ax = 0, ay = 0;
    while (n < 12) {
      if (xptRaw(rx, ry)) { ax += rx; ay += ry; n++; }
      delay(12);
    }
    rawx[i] = (int)(ax / n); rawy[i] = (int)(ay / n);
    calAlvo(PX[i], PY[i], C_OK);
    delay(280);
  }

  cal.swap = abs(rawy[1] - rawy[0]) > abs(rawx[1] - rawx[0]);
  auto AX = [&](int i) { return cal.swap ? rawy[i] : rawx[i]; };
  auto AY = [&](int i) { return cal.swap ? rawx[i] : rawy[i]; };

  float sx = (float)(AX(1) - AX(0)) / (float)(PX[1] - PX[0]);
  cal.xMin = (int)lroundf(AX(0) - sx * PX[0]);
  cal.xMax = (int)lroundf(AX(0) + sx * (SCR_W - 1 - PX[0]));
  float sy = (float)(AY(3) - AY(0)) / (float)(PY[3] - PY[0]);
  cal.yMin = (int)lroundf(AY(0) - sy * PY[0]);
  cal.yMax = (int)lroundf(AY(0) + sy * (SCR_H - 1 - PY[0]));

  calSave();
  Serial.printf("CAL swap=%d x[%d..%d] y[%d..%d]\n", cal.swap, cal.xMin, cal.xMax, cal.yMin, cal.yMax);

  tft.fillScreen(C_BG);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(C_OK, C_BG); tft.setFreeFont(F9);
  tft.drawString("Toque calibrado", SCR_W / 2, SCR_H / 2);
  delay(700);
  chromeDirty = dirty = true;
}

/* ---------------------------------------------------------------- */
/* 18. INTERACAO                                                     */
/* ---------------------------------------------------------------- */
/* Painel resistivo: as primeiras amostras depois do encosto sao lixo, e o
   dedo sempre escorrega alguns pixels. Estes numeros vieram de teste na
   placa - apertados demais, so os alvos encostados nos cantos respondiam,
   porque o constrain() do touchRead() gruda o ruido no proprio canto. */
#define TAP_MOVE_MAX  14     // px de folga antes de virar arraste
#define TAP_TIME_MAX  1600   // ms de duracao maxima de um toque
#define TOUCH_SETTLE  2      // amostras descartadas no inicio
#define TOUCH_UP_DEB  2      // amostras seguidas sem toque = soltou

static bool     tDown = false, tMoved = false, tSettled = false;
static int      tX0, tY0, tLastX, tLastY;
static uint32_t tT0 = 0;
static int      tSamples = 0, tUpCount = 0;
static long     tSumX = 0, tSumY = 0;   // media das amostras estaveis do toque
static int      tSumN = 0;
static bool     logoHold = false;

static void irParaOperador() {
  S.mode = MODE_OPERADOR;
  scrollY = 0; scrollVel = 0;
  relayout();
  chromeDirty = dirty = true;
  Serial.println("OK modo=operador");
}

static void irParaMotorista(uint8_t tela) {
  S.mode = MODE_MOTORISTA;
  S.scr  = tela;
  scrollY = 0; scrollVel = 0;
  relayout();
  tft.fillScreen(C_BG);
  dirty = true; chromeDirty = false;
  Serial.printf("OK modo=motorista tela=%s\n", tela == SCR_SESSAO ? "sessao" : "pagamento");
}

static void novaSessao() {
  S.sSoc = 62; S.sEntregue = 24.8f; S.sSeg = 0;
  S.payMet = -1; S.payDone = false; S.payExpira = 298; S.payCopiado = false;
  irParaMotorista(SCR_SESSAO);
}

static void voltarAoTopo() {
  if (scrollY == 0) return;
  scrollY = 0; scrollVel = 0; dirty = true;
}

static void handleTapConteudo(int x, int y) {
  int cy = (S.mode == MODE_OPERADOR) ? scrollY + (y - CONTENT_Y) : y;
  for (int i = 0; i < hitN; i++) {
    Hit& h = hits[i];
    if (x < h.x || x >= h.x + h.w || cy < h.y || cy >= h.y + h.h) continue;
    int id = h.id;
    if (touchDbg)
      Serial.printf("   alvo id=%d rect=[%d,%d %dx%d]\n", id, h.x, h.y, h.w, h.h);

    if (id >= HIT_BAY && id < HIT_BAY + 4) {
      S.selBay = id - HIT_BAY; dirty = true;
      Serial.printf("OK conector %s\n", BAYS[S.selBay].id);
    } else if (id == HIT_SES_PAGAR) {
      S.payMet = -1; S.payDone = false; S.payExpira = 298; S.payCopiado = false;
      irParaMotorista(SCR_PAGAMENTO);
    } else if (id == HIT_SES_OPER) {
      irParaOperador();
    } else if (id == HIT_PAY_BACK) {
      if (S.payMet >= 0) { S.payMet = -1; relayout(); dirty = true; }  // volta a escolha
      else irParaMotorista(SCR_SESSAO);
    } else if (id >= HIT_PAY_MET && id < HIT_PAY_MET + 4) {
      S.payMet = id - HIT_PAY_MET; S.payCopiado = false;
      relayout(); dirty = true;
      Serial.printf("OK metodo=%s\n", PG_FULL[S.payMet]);
    } else if (id == HIT_PAY_COPIA) {
      S.payCopiado = true; dirty = true;
    } else if (id == HIT_PAY_OK) {
      S.payDone = true; relayout(); dirty = true;
      Serial.println("OK pagamento aprovado");
    } else if (id == HIT_PAY_NOVA) {
      novaSessao();
    }
    return;
  }
  if (touchDbg) {
    Serial.printf("   nenhum alvo em %d,%d - alvos registrados:\n", x, cy);
    for (int i = 0; i < hitN; i++)
      Serial.printf("     id=%d [%d,%d %dx%d]\n",
                    hits[i].id, hits[i].x, hits[i].y, hits[i].w, hits[i].h);
  }
}

static void handleTap(int x, int y) {
  if (S.mode == MODE_MOTORISTA) { handleTapConteudo(x, y); return; }

  if (y < HEADER_H) {
    if (x >= playX && x <= playX + playW && y >= playY && y <= playY + playH) {
      S.playing = !S.playing;
      chromeDirty = true;
      Serial.printf("OK demo %s\n", S.playing ? "rodando" : "pausada");
    }
    return;
  }
  if (y < CHROME_H) {
    if (x >= motoX - 6 && x <= motoX + motoW + 6) { irParaMotorista(SCR_SESSAO); return; }
    if (x >= dashX - 6 && x <= dashX + dashW + 6) { voltarAoTopo(); return; }
    return;
  }
  handleTapConteudo(x, y);
}

static void handleTouch() {
  int x, y;
  bool down = touchRead(x, y);

  if (down) {
    tUpCount = 0;
    if (!tDown) {                        // encostou agora
      tDown = true; tMoved = false; tSettled = false; logoHold = false;
      tSamples = 0; tT0 = millis(); scrollVel = 0;
    }
    if (!tSettled) {                     // descarta as amostras de assentamento
      if (++tSamples > TOUCH_SETTLE) {
        tSettled = true;
        tX0 = tLastX = x; tY0 = tLastY = y;
        tSumX = x; tSumY = y; tSumN = 1;
      }
      return;
    }
    tSumX += x; tSumY += y; tSumN++;
    if (abs(x - tX0) > TAP_MOVE_MAX || abs(y - tY0) > TAP_MOVE_MAX) tMoved = true;

    /* rolagem so existe no dashboard do operador */
    if (S.mode == MODE_OPERADOR && tMoved && tY0 >= CONTENT_Y) {
      int dy = tLastY - y;
      if (dy != 0) {
        scrollY = constrain(scrollY + dy, 0, max(0, contentH - CONTENT_H));
        scrollVel = dy * 0.9f;
        dirty = true;
      }
    }
    tLastX = x; tLastY = y;

    if (S.mode == MODE_OPERADOR && !tMoved && !logoHold &&
        tY0 < HEADER_H && tX0 < 80 && millis() - tT0 > 2000) {
      logoHold = true;
      calibrar();
      tDown = false; tSettled = false;
    }
    return;
  }

  if (tDown && ++tUpCount >= TOUCH_UP_DEB) {   // soltou de verdade
    tDown = false;
    uint32_t dur = millis() - tT0;
    if (tSettled && !tMoved && dur < TAP_TIME_MAX) {
      /* media das amostras estaveis: mais preciso que a ultima leitura */
      int tx = tSumN ? (int)(tSumX / tSumN) : tLastX;
      int ty = tSumN ? (int)(tSumY / tSumN) : tLastY;
      if (touchDbg) Serial.printf("TAP %d,%d (%d amostras, %lu ms)\n",
                                  tx, ty, tSumN, (unsigned long)dur);
      handleTap(tx, ty);
      scrollVel = 0;
    } else if (touchDbg) {
      Serial.printf("descartado: settled=%d moved=%d dur=%lu\n",
                    tSettled, tMoved, (unsigned long)dur);
    }
    tSettled = false; tSumN = 0;
  }
}

static void applyInertia() {
  if (S.mode != MODE_OPERADOR || tDown || fabsf(scrollVel) < 0.7f) { scrollVel = 0; return; }
  int maxS = max(0, contentH - CONTENT_H);
  int ns = constrain(scrollY + (int)scrollVel, 0, maxS);
  if (ns != scrollY) { scrollY = ns; dirty = true; }
  else scrollVel = 0;
  scrollVel *= 0.88f;
}

/* ---------------------------------------------------------------- */
/* 19. SERIAL                                                        */
/* ---------------------------------------------------------------- */
static char lineBuf[640];
static int  lineLen = 0;

static void processLine(char* s) {
  while (*s == ' ') s++;
  if (*s == '{') { aplicarJson(s); return; }
  if (!strncasecmp(s, "CAL", 3))   { calibrar(); return; }
  if (!strncasecmp(s, "PAUSE", 5)) { S.playing = false; chromeDirty = true; return; }
  if (!strncasecmp(s, "PLAY", 4))  { S.playing = true;  chromeDirty = true; return; }
  if (!strncasecmp(s, "TELA ", 5)) {
    const char* a = s + 5;
    if      (!strncasecmp(a, "ses", 3)) irParaMotorista(SCR_SESSAO);
    else if (!strncasecmp(a, "pag", 3)) irParaMotorista(SCR_PAGAMENTO);
    else if (!strncasecmp(a, "das", 3) || !strncasecmp(a, "ope", 3)) irParaOperador();
    else Serial.println("ERRO use TELA <sessao|pagamento|dashboard>");
    return;
  }
  if (!strncasecmp(s, "DEBUG", 5)) {
    touchDbg = !touchDbg;
    Serial.printf("OK debug de toque %s\n", touchDbg ? "ligado" : "desligado");
    return;
  }
  Serial.println("ERRO comandos: JSON, CAL, PLAY, PAUSE, DEBUG, TELA <sessao|pagamento|dashboard>");
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen) { lineBuf[lineLen] = 0; processLine(lineBuf); lineLen = 0; }
    } else if (lineLen < (int)sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = c;
    }
  }
}

/* ---------------------------------------------------------------- */
/* 20. SETUP / LOOP                                                  */
/* ---------------------------------------------------------------- */
void setup() {
  Serial.setRxBufferSize(2048);          // o JSON tem ~260 B; o padrao (256) trunca
  Serial.begin(115200);
  delay(120);
  Serial.println();
  Serial.println("ChargeGrid CYD v1.1 - pronto");

  pinMode(TFT_BL_PIN, OUTPUT); digitalWrite(TFT_BL_PIN, HIGH);
  randomSeed(esp_random());

  tft.init();
  tft.setRotation(TFT_ROT);
  tft.fillScreen(C_BG);

  /* depois do tft.init(): o User_Setup declara TOUCH_CS 33 e a lib deixa
     esse pino em OUTPUT/HIGH; aqui assumimos o controle dele por software */
  pinMode(T_CLK, OUTPUT); digitalWrite(T_CLK, LOW);
  pinMode(T_DIN, OUTPUT);
  pinMode(T_CS, OUTPUT);  digitalWrite(T_CS, HIGH);
  pinMode(T_DO, INPUT);   pinMode(T_IRQ, INPUT);

  if (!spr.createSprite(SCR_W, BAND_H)) {
    tft.setTextColor(C_RED2, C_BG);
    tft.drawString("Sem RAM para o sprite", 10, 10, 2);
    while (true) delay(1000);
  }
  spr.setTextWrap(false);

  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(FB12); tft.setTextColor(C_TEXT, C_BG);
  tft.drawString("CHARGEGRID", SCR_W / 2, SCR_H / 2 - 12);
  tft.setTextFont(1); tft.setTextSize(1); tft.setTextColor(C_DIM, C_BG);
  tft.drawString("GOODWE ENERGIA", SCR_W / 2, SCR_H / 2 + 12);
  tft.setTextDatum(TL_DATUM);
  delay(600);

  if (!calLoad()) {
    tft.fillScreen(C_BG);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(C_DIM, C_BG); tft.setTextFont(1);
    tft.drawString("primeiro boot: calibrando o toque", SCR_W / 2, SCR_H / 2);
    tft.setTextDatum(TL_DATUM);
    delay(900);
    calibrar();
  }

  qrBuild();
  computeBays();
  relayout();
  tft.fillScreen(C_BG);
  if (S.mode == MODE_OPERADOR) drawChrome();
  drawContent();
}

void loop() {
  static uint32_t tTick = 0, tTouch = 0, tDraw = 0;
  uint32_t now = millis();

  handleSerial();

  if (now - tTouch >= 16) { tTouch = now; handleTouch(); applyInertia(); }

  if (now - tTick >= 1000) {
    tTick = now;
    tick();
    if (S.mode == MODE_OPERADOR) { chromeDirty = true; if (S.playing || S.serialLive) dirty = true; }
    else                         { dirty = true; }
  }

  if (S.mode == MODE_OPERADOR && chromeDirty) { chromeDirty = false; drawChrome(); }

  if (dirty && now - tDraw >= 40) {        // teto de ~25 fps
    tDraw = now; dirty = false;
    drawContent();
  }
}