// Independent display backends for the common CP/M core.
#pragma once
#ifndef EBTKS_WEB_TERMINAL_ENABLE
#define EBTKS_WEB_TERMINAL_ENABLE 1
#endif
#ifndef EBTKS_TFT_ENABLE
#define EBTKS_TFT_ENABLE 0
#endif
#ifndef EBTKS_TFT_TERMINAL_MIRROR
#define EBTKS_TFT_TERMINAL_MIRROR 0
#endif
#if EBTKS_WEB_TERMINAL_ENABLE != 0 && EBTKS_WEB_TERMINAL_ENABLE != 1
#error "EBTKS_WEB_TERMINAL_ENABLE must be 0 or 1"
#endif
#if EBTKS_TFT_TERMINAL_MIRROR && !EBTKS_TFT_ENABLE
#error "TFT mirror requires TFT enabled"
#endif
#if !EBTKS_WEB_TERMINAL_ENABLE && !EBTKS_TFT_TERMINAL_MIRROR
#error "A TFT or Web Terminal backend must be enabled"
#endif
