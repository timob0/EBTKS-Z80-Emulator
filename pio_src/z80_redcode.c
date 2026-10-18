/* Compile upstream redcode/Z80 as C (not inside the Arduino C++ sketch).
 * Upstream source vendored verbatim at a pinned commit. */
#define Z80_STATIC
#define Z80_WITH_LOCAL_HEADER
#define Z80_EXTERNAL_HEADER "ebtks_zeta_compat.h"
#define Z80_WITH_Q
#define Z80_WITH_ZILOG_NMOS_LD_A_IR_BUG
#include "../ESP32_Z80_Emulator/vendor/redcode_z80/Z80.c"
