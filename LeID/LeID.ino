/* Le o ID do controlador do painel pela SPI (MISO 12) para descobrir
   se e ILI9341, ST7789 ou outro. Nao desenha nada. */
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();

void dump(const char* nome, uint8_t cmd, int n) {
  Serial.printf("%-26s 0x%02X ->", nome, cmd);
  for (int i = 0; i < n; i++) Serial.printf(" %02X", tft.readcommand8(cmd, i));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(400);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  tft.init();
  delay(150);
  Serial.println("\n============ ID DO CONTROLADOR ============");
  dump("RDDID   (0x04)",        0x04, 4);
  dump("RDID4   (0xD3) ILI93xx",0xD3, 4);
  dump("RDDST   (0x09)",        0x09, 5);
  dump("RDDPM   (0x0A) power",  0x0A, 2);
  dump("RDDMADCTL (0x0B)",      0x0B, 2);
  dump("RDDCOLMOD (0x0C)",      0x0C, 2);
  dump("RDDIM   (0x0D)",        0x0D, 2);
  Serial.println("-------------------------------------------");
  Serial.println("ILI9341 -> RDID4 = xx 00 93 41");
  Serial.println("ST7789  -> RDDID = xx 85 85 52  (e RDID4 costuma vir 00/FF)");
  Serial.println("===========================================");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Leitura de ID - veja a serial", 6, 6, 2);
}
void loop() { delay(1000); }
