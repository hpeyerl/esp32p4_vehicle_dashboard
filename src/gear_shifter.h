// =============================================================
//  gear_shifter.h — CAN PRND gear shift transmitter
//
//  Sends CAN 0x312 at 20 ms while a gear is selected.
//  Format mirrors the JLR G1 M5Dial shifter:
//    byte[3] upper nibble = gear (0=P 1=R 2=N 3=D)
//    byte[7] lower nibble = rolling counter
// =============================================================
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start the 20 ms TX task. Call once after twai_init().
void   gear_shifter_start(void);

// Request a gear: 0=P 1=R 2=N 3=D.  -1 stops transmitting.
void   gear_shifter_request(int8_t gear);

// Returns the currently requested gear (-1 if none).
int8_t gear_shifter_current(void);

#ifdef __cplusplus
}
#endif
