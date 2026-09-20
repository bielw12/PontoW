// =====================================================================
//  User_Setup.h de referencia para o ESP32-2432S028R (Cheap Yellow Display)
//  usado pelo firmware ChargeGridCYD.
//
//  Destino:  Documentos\Arduino\libraries\TFT_eSPI\User_Setup.h
//
//  Esta config foi validada na placa em 24/08/2026. Duas linhas dela sao
//  o resultado do diagnostico e NAO sao opcionais - ver README secao 6.
// =====================================================================

// ---- Driver ----------------------------------------------------------
// O painel desta placa (TPM408-2.8) e NATIVO 320x240 (paisagem).
// ILI9342_DRIVER usa a MESMA sequencia de init e a MESMA tabela de
// rotacao do ILI9341 - muda so TFT_WIDTH/TFT_HEIGHT para 320/240.
// Com ILI9341_DRIVER a lib pinta uma janela de 240 px de largura e sobra
// uma faixa de 80 px (25%) exibindo o frame anterior.
#define ILI9342_DRIVER

// Painel e RGB, nao BGR (padrao do driver). Sem isto o azul sai vermelho.
#define TFT_RGB_ORDER TFT_RGB

// ---- Barramento SPI do display (VSPI remapeado pela matriz de GPIO) ----
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1

#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH

// Nota: com ILI9342_DRIVER as rotacoes trocam de significado.
//   0 e 2 = paisagem 320x240   |   1 e 3 = retrato 240x320
// O firmware usa setRotation(2). Se a tela ficar de cabeca para baixo,
// troque TFT_ROT para 0 no ChargeGridCYD.ino.

// ---- Toque -----------------------------------------------------------
// O firmware NAO usa o driver de toque do TFT_eSPI: no CYD o XPT2046 esta
// num barramento proprio (CLK 25 / MOSI 32 / MISO 39 / CS 33 / IRQ 36),
// e nao nos pinos do display. O ChargeGridCYD.ino fala com ele por SPI
// via software. Esta linha pode ficar aqui sem efeito colateral.
#define TOUCH_CS 33

// ---- Fontes ----------------------------------------------------------
#define LOAD_GLCD    // fonte 1 (6x8) - usada nos rotulos pequenos
#define LOAD_FONT2   // fonte 2 (16 px) - telas de erro
#define LOAD_FONT4
#define LOAD_GFXFF   // OBRIGATORIO: FreeSans / FreeSansBold do dashboard
#define SMOOTH_FONT

// ---- Velocidade ------------------------------------------------------
#define SPI_FREQUENCY  55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
