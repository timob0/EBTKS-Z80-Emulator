// ST7796 (320x480) ESP32 VSPI bus wiring + optional legacy boot-pattern helper.
// This header is inert in the default headless build. Current
// esp32dev-tft invokes startTerminalMirror(), not the one-shot color bars.
// Pin numbers refer to ESP32 GPIOs verified by the user, NOT original CN3 labels.
#pragma once
#ifndef EBTKS_TFT_ENABLE
#define EBTKS_TFT_ENABLE 0
#endif
#if EBTKS_TFT_ENABLE != 0 && EBTKS_TFT_ENABLE != 1
#error "EBTKS_TFT_ENABLE must be 0 or 1"
#endif
#if EBTKS_TFT_ENABLE
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <driver/spi_master.h>
// The independent VSPI TFT controller was validated at 4 MHz. Try 12 MHz,
// with 8 MHz as a compile-time fallback on long/Dupont display wiring.
// Leave the Teensy/ESP32 HSPI disk link unchanged.
#ifndef EBTKS_TFT_SPI_HZ
#define EBTKS_TFT_SPI_HZ 12000000
#endif
static_assert(EBTKS_TFT_SPI_HZ >= 4000000 && EBTKS_TFT_SPI_HZ <= 20000000,
              "TFT SPI speed must be 4..20 MHz");

namespace EbtksTft {
constexpr int kTftSclk = 14; // TFT SCK, remapped ESP32 VSPI master
constexpr int kTftMosi = 23; // TFT SDI(MOSI)
constexpr int kTftCs   = 13; // TFT LCD_CS
constexpr int kTftDc   = 33; // TFT LCD_RS / D-C
constexpr int kTftRst  = 19; // TFT LCD_RST

// The live Teensy/CP/M HSPI slave uses GPIO22,17,16,32,27.
// Neither that bus nor the UART GPIO1/3 may be repurposed for the TFT.
constexpr bool safePin(int p) {
  return p != 22 && p != 17 && p != 16 && p != 32 && p != 27
      && p != 1 && p != 3 && p != 0 && p != 2 && p != 5
      && p != 12 && p != 15;
}
static_assert(safePin(kTftSclk) && safePin(kTftMosi) && safePin(kTftCs)
              && safePin(kTftDc) && safePin(kTftRst),
              "TFT GPIO overlaps the CP/M SPI/UART bus or an ESP32 boot strap");

class Panel : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796 panel_;
  lgfx::Bus_SPI bus_;
public:
  Panel() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = VSPI_HOST; // SPI3 master, not the SPI2/HSPI disk slave
      cfg.spi_mode = 0;
      cfg.freq_write = EBTKS_TFT_SPI_HZ; // 12 MHz experiment; 8 MHz fallback
      cfg.freq_read = EBTKS_TFT_SPI_HZ; // no LCD readback wiring
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = 0;     // avoid competing for DMA with the disk link
      cfg.pin_sclk = kTftSclk;
      cfg.pin_mosi = kTftMosi;
      cfg.pin_miso = -1;       // no LCD readback wiring in the first test
      cfg.pin_dc = kTftDc;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = kTftCs;
      cfg.pin_rst = kTftRst;
      cfg.pin_busy = -1;
      cfg.memory_width = 320;
      cfg.memory_height = 480;
      cfg.panel_width = 320;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    setPanel(&panel_);
  }
};

// Shared single controller instance for the initial color bars or later
// terminal mirror. Only the designated TFT task writes once it has started.
inline Panel& panel() {
  static Panel display;
  return display;
}
// Call once from setup(), after Wi-Fi and Z80 worker-task initialization,
// before CP/M bootstrap. Nothing redraws during CP/M disk operations.
inline void drawBootPattern() {
  Panel& display = panel();
  const uint32_t heapBefore = ESP.getFreeHeap();
  display.init();
  display.setRotation(1);     // landscape orientation: 480 x 320
  display.fillScreen(0x0000);
  constexpr uint16_t colors[] = {
    0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F
  };                         // red green blue yellow cyan magenta
  for (unsigned i = 0; i < 6; ++i) {
    display.fillRect(static_cast<int32_t>(i * 80), 52, 80, 145, colors[i]);
    delay(1);                // remain scheduler-friendly during the test
  }
  display.setTextColor(0xFFFF, 0x0000);
  display.setTextSize(2);
  display.setCursor(12, 12);
  display.print("EBTKS - ST7796 SPI TEST");
  display.setTextSize(1);
  display.setCursor(12, 215);
  display.printf("320 x 480 / %lu MHz / VSPI / GPIO14 SCK",
                 (unsigned long)(EBTKS_TFT_SPI_HZ/1000000));
  display.setCursor(12, 235);
  display.print("SPI2 disk service and CP/M remain separate.");
  display.setCursor(12, 255);
  display.print("Touch and SD are not enabled.");
  display.drawRect(0, 0, 480, 320, 0xFFFF);
  // Write-only TFT: this reports completion of drawing commands, NOT that
  // the screen is electrically working. Confirm visually on hardware.
  const uint32_t heapAfter = ESP.getFreeHeap();
  EbtksDebugSerial.printf("TFT ST7796 test commands sent; heap %lu -> %lu bytes\n",
                          (unsigned long)heapBefore, (unsigned long)heapAfter);
}
} // namespace EbtksTft
#endif  // EBTKS_TFT_ENABLE
