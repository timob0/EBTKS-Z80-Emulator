// Low-priority, framebuffer-free CP/M UART mirror for ST7796.
// Enabled only in esp32dev-tft-terminal, not standard or color-bar builds.
#pragma once
#ifndef EBTKS_TFT_TERMINAL_MIRROR
#define EBTKS_TFT_TERMINAL_MIRROR 0
#endif
#ifndef EBTKS_TFT_VT100
#define EBTKS_TFT_VT100 0
#endif
#if EBTKS_TFT_TERMINAL_MIRROR
#if !EBTKS_TFT_ENABLE
#error "The TFT terminal requires EBTKS_TFT_ENABLE=1"
#endif
#if EBTKS_TFT_VT100
#include "tft_terminal_vt100.h"
#else
#include "tft_terminal_model.h"
#endif
#include "ebtks_tft_test.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>

namespace EbtksTft {
#if EBTKS_TFT_VT100
using MirrorScreen = EbtksVt100::Screen;
#else
using MirrorScreen = EbtksTerminal::Screen;
#endif
// Allocate only for the opt-in terminal build, after Wi-Fi/disk startup.
// Static .dram0.bss has only ~2KB headroom on the ESP32-WROOM target.
static MirrorScreen* mirrorModel = nullptr;
static portMUX_TYPE mirrorMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t mirrorTaskHandle = nullptr;

// Called by the existing console router, once for each UART chunk. This
// updates just 1,920 bytes of character state; NEVER performs SPI transfers.
inline void mirrorFeed(const uint8_t* data,size_t n) {
  if (!data || !n || !mirrorModel) return;
  portENTER_CRITICAL(&mirrorMux);
  mirrorModel->feed(data,n);
  portEXIT_CRITICAL(&mirrorMux);
}
inline void mirrorWorker(void *) {
  Panel& tft = panel();
  for(;;) {
    bool rendered=false;
    // At most 3 changed text rows per scheduler period so the worker doesn't
    // monopolize core 0, which also runs the SPI disk and web workers.
    int refreshed=0;
    for(int row=0;row<24 && refreshed<3;++row) {
      uint8_t line[80] = {};
#if EBTKS_TFT_VT100
      uint16_t style[80] = {};
#endif
      bool changed=false;
      portENTER_CRITICAL(&mirrorMux);
      if(mirrorModel->dirty[row]) {
        memcpy(line,mirrorModel->cells[row],80);
#if EBTKS_TFT_VT100
        memcpy(style,mirrorModel->styles[row],sizeof(style));
#endif
        mirrorModel->dirty[row]=false;
        changed=true;
      }
      portEXIT_CRITICAL(&mirrorMux);
      if(!changed)continue;
      const int y=row*13;
      tft.fillRect(0,y,480,13,0x0000);
#if EBTKS_TFT_VT100
      // Foreground/background/underline are per-cell; repaint only changed
      // rows, batching consecutive equal styles into runs, no pixel buffer.
      static constexpr uint16_t colors[16] = {
        0x0000,0xA800,0x0540,0xAD40,0x0015,0xA815,0x0555,0xAD55,
        0x52AA,0xF800,0x07E0,0xFFE0,0x001F,0xF81F,0x07FF,0xFFFF
      };
      for(int x=0;x<80;) {
        const uint16_t st=style[x];
        int end=x+1;
        while(end<80 && style[end]==st)++end;
        uint8_t f=st&EbtksVt100::kFgMask;
        uint8_t b=(st>>5)&31;
        if(st&EbtksVt100::kBold) {
          if(f<8)f+=8; // compact 6x8 font: bold as bright rendition
          else if(f==EbtksVt100::kDefaultColor)f=10;
        }
        uint16_t fg=f==EbtksVt100::kDefaultColor?0xA7E6:colors[f&15];
        uint16_t bg=b==EbtksVt100::kDefaultColor?0x0000:colors[b&15];
        if(st&EbtksVt100::kInverse){uint16_t tmp=fg;fg=bg;bg=tmp;}
        if(bg)tft.fillRect(x*6,y,(end-x)*6,13,bg);
        bool nonBlank=false;
        for(int c=x;c<end;++c)if(line[c]!=' '){nonBlank=true;break;}
        if(nonBlank) {
          tft.setTextColor(fg,bg);
          tft.setCursor(x*6,y+2);
          tft.write(line+x,(size_t)(end-x)); // raw CP437 bytes, UTF8 off
        }
        if(st&EbtksVt100::kUnderline)
          tft.drawFastHLine(x*6,y+11,(end-x)*6,fg);
        x=end;
      }
#else
      int n=80;
      while(n>0 && line[n-1]==' ')--n;
      char text[81];
      memcpy(text,line,n);
      text[n]=0;
      if(n) {
        tft.setCursor(0,y+2);
        tft.print(text);
      }
#endif
      ++refreshed;
      rendered=true;
      vTaskDelay(1); // give disk-read worker and other core-0 tasks CPU time
    }
    vTaskDelay(pdMS_TO_TICKS(rendered?35:80));
  }
}
inline void startTerminalMirror() {
  if (mirrorModel) return;
  // Runtime allocation recovers static .dram0.bss budget. Should this fail,
  // leave CP/M, UART output, Web UI and the fast disk transport undisturbed.
  mirrorModel = new (std::nothrow) MirrorScreen();
  if(!mirrorModel) {
    EbtksDebugSerial.println("TFT mirror disabled: not enough runtime heap for text model");
    return;
  }
  Panel& tft = panel();
  tft.init();
  tft.setRotation(1);              // 480×320: 80 characters × 6 px
  tft.fillScreen(0x0000);         // clear once before CP/M starts
  tft.setTextColor(0xA7E6,0x0000); // muted green on black
  tft.setTextSize(1);             // built-in 6×8 font, 13 px row pitch
#if EBTKS_TFT_VT100
  tft.setFont(&lgfx::fonts::Font0); // Full 0..255 CP437 bitmap table
  tft.cp437(true);                 // Disable historical >175 offset
  tft.setAttribute(lgfx::utf8_switch,false); // raw eight-bit glyph stream
  tft.setTextWrap(false);         // fixed 80-column layout
#endif
  BaseType_t result=xTaskCreatePinnedToCore(
    mirrorWorker,"TFTMirror",4096,nullptr,1,&mirrorTaskHandle,0);
  if(result != pdPASS) {
    delete mirrorModel;
    mirrorModel=nullptr;
  }
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
