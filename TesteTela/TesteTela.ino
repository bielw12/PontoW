/* =====================================================================
   TesteTela - diagnostico do display do CYD

   Responde tres perguntas:
     1) o desenho DIRETO na tft funciona? (se nao, o problema e driver/painel)
     2) a rotacao esta sendo aplicada?
     3) o caminho de SPRITE funciona? (e o que o ChargeGridCYD usa)

   Cicla rot=0,1,2,3 a cada 5 s e reporta tudo pela serial em 115200.
   ===================================================================== */

#include <TFT_eSPI.h>

TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);

static const char* driverNome() {
#if   defined(ILI9341_DRIVER)
  return "ILI9341_DRIVER";
#elif defined(ILI9341_2_DRIVER)
  return "ILI9341_2_DRIVER";
#elif defined(ILI9342_DRIVER)
  return "ILI9342_DRIVER";
#elif defined(ST7789_DRIVER)
  return "ST7789_DRIVER";
#elif defined(ST7789_2_DRIVER)
  return "ST7789_2_DRIVER";
#elif defined(ILI9488_DRIVER)
  return "ILI9488_DRIVER";
#elif defined(ST7796_DRIVER)
  return "ST7796_DRIVER";
#else
  return "??? desconhecido";
#endif
}

void relatorio() {
  Serial.println();
  Serial.println("================ TesteTela ================");
  Serial.printf("driver .......... %s\n", driverNome());
  Serial.printf("TFT_WIDTH ....... %d\n", (int)TFT_WIDTH);
  Serial.printf("TFT_HEIGHT ...... %d\n", (int)TFT_HEIGHT);
  Serial.printf("SPI_FREQUENCY ... %ld Hz\n", (long)SPI_FREQUENCY);
#ifdef TFT_INVERSION_ON
  Serial.println("TFT_INVERSION ... ON");
#endif
#ifdef TFT_RGB_ORDER
  Serial.printf("TFT_RGB_ORDER ... %d\n", (int)TFT_RGB_ORDER);
#endif
#ifdef USE_HSPI_PORT
  Serial.println("porta SPI ....... HSPI");
#else
  Serial.println("porta SPI ....... VSPI (padrao)");
#endif
  Serial.printf("pinos ........... MISO %d MOSI %d SCLK %d CS %d DC %d RST %d BL %d\n",
                (int)TFT_MISO, (int)TFT_MOSI, (int)TFT_SCLK,
                (int)TFT_CS, (int)TFT_DC, (int)TFT_RST, (int)TFT_BL);
  Serial.println("===========================================");
}

/* --- teste 1 e 2: desenho direto + rotacao ------------------------- */
void telaDireta(int rot) {
  tft.setRotation(rot);
  int W = tft.width(), H = tft.height();

  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, W, H, TFT_WHITE);

  /* cantos: cada um com uma cor e uma sigla */
  tft.fillRect(2, 2, 44, 16, TFT_RED);
  tft.fillRect(W - 46, 2, 44, 16, TFT_GREEN);
  tft.fillRect(2, H - 18, 44, 16, TFT_BLUE);
  tft.fillRect(W - 46, H - 18, 44, 16, TFT_YELLOW);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, TFT_RED);    tft.drawString("SE",  6,  3);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);  tft.drawString("SD",  W - 42, 3);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);   tft.drawString("IE",  6,  H - 17);
  tft.setTextColor(TFT_BLACK, TFT_YELLOW); tft.drawString("ID",  W - 42, H - 17);

  /* faixa de cores puras: confere ordem RGB e inversao */
  int fx = 6, fw = (W - 12) / 6;
  uint16_t cores[6] = { TFT_RED, TFT_GREEN, TFT_BLUE, TFT_CYAN, TFT_MAGENTA, TFT_WHITE };
  for (int i = 0; i < 6; i++) tft.fillRect(fx + i * fw, 44, fw - 2, 18, cores[i]);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4);
  char b[40];
  snprintf(b, sizeof(b), "rot=%d  %dx%d", rot, W, H);
  tft.drawString(b, 8, 72);

  tft.setTextFont(2);
  tft.drawString("Texto direto na TFT", 8, 102);
  tft.drawString(driverNome(), 8, 120);
}

/* --- teste 3: caminho de sprite ------------------------------------ */
void telaSprite() {
  int W = tft.width();
  int y0 = tft.height() - 66;
  if (!spr.createSprite(W, 60)) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("FALHA ao criar sprite!", 8, y0);
    return;
  }
  spr.fillSprite(TFT_NAVY);
  spr.drawRect(0, 0, W, 60, TFT_CYAN);
  spr.setTextFont(2);
  spr.setTextColor(TFT_WHITE, TFT_NAVY);
  spr.drawString("SPRITE - se este bloco azul estiver", 5, 4);
  spr.drawString("limpo e o texto legivel, o caminho", 5, 21);
  spr.drawString("de sprite esta OK.", 5, 38);
  spr.pushSprite(0, y0);
  spr.deleteSprite();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  relatorio();
}

void loop() {
  for (int rot = 0; rot < 4; rot++) {
    telaDireta(rot);
    telaSprite();
    Serial.printf("[rot %d] tft.width()=%d tft.height()=%d\n",
                  rot, tft.width(), tft.height());
    delay(5000);
  }
}
