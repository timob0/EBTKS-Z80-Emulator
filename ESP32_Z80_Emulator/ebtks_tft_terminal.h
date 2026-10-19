// Low-priority, framebuffer-free CP/M UART mirror for ST7796.
// Enabled only in esp32dev-tft-terminal, not standard or color-bar builds.
#pragma once
#ifndef EBTKS_TFT_TERMINAL_MIRROR
#define EBTKS_TFT_TERMINAL_MIRROR 0
#endif
#if EBTKS_TFT_TERMINAL_MIRROR
#if !EBTKS_TFT_ENABLE
#error "The TFT terminal requires EBTKS_TFT_ENABLE=1"
#endif
#include "tft_terminal_model.h"
#include "ebtks_tft_test.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace EbtksTft {
static EbtksTerminal::Screen mirrorModel;
static portMUX_TYPE mirrorMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t mirrorTaskHandle = nullptr;

// Called by the existing console router, once for each UART chunk. This
// updates just 1,920 bytes of character state; NEVER performs SPI transfers.
inline void mirrorFeed(const uint8_t* data,size_t n) {
  if (!data || !n) return;
  portENTER_CRITICAL(&mirrorMux);
  mirrorModel.feed(data,n);
  portEXIT_CRITICAL(&mirrorMux);
}
inline void mirrorWorker(void *) {
  Panel& tft = panel();
  for(;;) {
    bool rendered=false;
    // At most 3 changed text rows per scheduler period so the worker doesn't
    // monopolize core 0, which also runs the SPI disk and web workers.
    int refreshed=0;
    for(int row=0;row<EbtksTerminal::kRows && refreshed<3;++row) {
      char line[EbtksTerminal::kCols+1] = {};
      bool changed=false;
      portENTER_CRITICAL(&mirrorMux);
      if(mirrorModel.dirty[row]) {
        memcpy(line,mirrorModel.cells[row],EbtksTerminal::kCols);
        mirrorModel.dirty[row]=false;
        changed=true;
      }
      portEXIT_CRITICAL(&mirrorMux);
      if(!changed)continue;
      int n=EbtksTerminal::kCols;
      while(n>0 && line[n-1]==' ')--n;
      line[n]=0;
      const int y=row*13;
      tft.fillRect(0,y,480,13,0x0000);
      if(n) {
        tft.setCursor(0,y+2);
        tft.print(line);
      }
      ++refreshed;
      rendered=true;
      vTaskDelay(1); // give disk-read worker and other core-0 tasks CPU time
    }
    vTaskDelay(pdMS_TO_TICKS(rendered?35:80));
  }
}
inline void startTerminalMirror() {
  Panel& tft = panel();
  tft.init();
  tft.setRotation(1);              // 480×320: 80 characters × 6 px
  tft.fillScreen(0x0000);         // clear once before CP/M starts
  tft.setTextColor(0xA7E6,0x0000); // muted green on black
  tft.setTextSize(1);             // built-in 6×8 font, 13 px row pitch
  BaseType_t result=xTaskCreatePinnedToCore(
    mirrorWorker,"TFTMirror",4096,nullptr,1,&mirrorTaskHandle,0);
  EbtksDebugSerial.printf("TFT 80x24 console mirror: %s, heap %lu bytes\n",
                          result==pdPASS?"started":"task create failed",
                          (unsigned long)ESP.getFreeHeap());
}
} // namespace EbtksTft
#else
namespace EbtksTft {
inline void mirrorFeed(const uint8_t*,size_t) {}
}
#endif
