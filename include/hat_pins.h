#pragma once
/**
 * hat_pins.h
 * ESP32-P4-Nano Vehicle Interface HAT  v0.5
 * GPIO assignments verified against Waveshare ESP32-P4-Nano schematic
 * and current dashboard firmware (evj55 repo).
 *
 * Board: Waveshare ESP32-P4-Nano
 * Hat:   P4-Nano Vehicle Interface HAT
 */

// ── RESERVED — DO NOT USE ────────────────────────────────────────────────────
// GPIO7  = I2C SDA for Goodix GT911 touch controller
// GPIO8  = I2C SCL for Goodix GT911 touch controller
// These are hardwired on the Waveshare 12.3" DSI FPC connector.
// They appear on P1 pins 3 and 5 but must not be driven by the HAT.
// GPIO9  = NOT broken out on any expansion header — do not use.
// GPIO10 = NOT broken out on any expansion header — do not use.
// GPIO54 = C6 Reset/EN — do not configure or drive.

// ── CAN / TWAI ────────────────────────────────────────────────────────────────
#define HAT_TWAI_TX         GPIO_NUM_53   // P2 pin14  -> SN65HVD230 TXD
#define HAT_TWAI_RX         GPIO_NUM_48   // P2 pin16  <- SN65HVD230 RXD

// ── Electronic Parking Brake (EPB) ───────────────────────────────────────────
// Output: open-drain via Q_EPB (2N7002 SOT-23).
//   Normally HIGH (FET off).  Pulse LOW ~200ms to press EPB button.
//   GPIO2/3 confirmed free in current firmware — no conflict with GT911.
//   (Earlier firmware notes referencing GPIO2/3 for GT911 INT/RST were stale;
//    GT911 I2C is on GPIO7/8 via the DSI FPC cable, not the HAT headers.)
#define HAT_EPB_OUT         GPIO_NUM_6    // P2 pin12  -> Q_EPB gate (1k series)
// Inputs: active-low, internal pull-up, 33R series + BZX84C5V1 5.1V zener clamp.
#define HAT_EPB_GRN         GPIO_NUM_2    // P2 pin11  <- EPB green LED (RELEASED)
#define HAT_EPB_RED         GPIO_NUM_3    // P2 pin9   <- EPB red LED  (APPLIED)

// ── Vehicle Speed Sensor (VSS) ────────────────────────────────────────────────
// Toyota HF2A transfer case Hall effect sensor (3-wire: 12V, GND, signal).
// 12V supply from +12V_INT via J_VSS pin3.
// Signal: 10k series + BZX84C5V1 5.1V zener clamp on GPIO5.
// Internal pull-up enabled in firmware.
// Calibration: pulses/meter TBD — verify at known speed after installation.
#define HAT_VSS             GPIO_NUM_5    // P1 pin11  <- VSS signal

// ── Display Backlight Dimmer (ADC) ────────────────────────────────────────────
// ADC1_CHANNEL7.  10k series + BZX84C5V1 5.1V zener clamp.
#define HAT_DIMMER          GPIO_NUM_20   // P1 pin13  <- dimmer pot/signal

// ── MagneRide Suspension Control (LEDC PWM, 25 kHz) ──────────────────────────
// AOD4184A TO-252 N-ch MOSFET per channel.
// 10R gate series resistor, 10k gate pull-down (safe-off at power-up).
// UF5408 DO-201AD flyback diode, 100uF/25V bulk cap per channel.
// ~35% duty = soft/comfort.  Increase = firm/sport.  Ramp gradually in firmware.
// CH3 and CH4: footprints populated, components DNP (unpopulated by default).
#define HAT_MR_CH1_PWM      GPIO_NUM_45   // P2 pin19  -> Q_MR1 gate
#define HAT_MR_CH2_PWM      GPIO_NUM_46   // P2 pin17  -> Q_MR2 gate
#define HAT_MR_CH3_PWM      GPIO_NUM_47   // P2 pin15  -> Q_MR3 gate (DNP)
#define HAT_MR_CH4_PWM      GPIO_NUM_32   // P1 pin23  -> Q_MR4 gate (DNP)

// ── MagneRide PWM parameters ──────────────────────────────────────────────────
#define HAT_MR_FREQ_HZ      25000
#define HAT_MR_RESOLUTION   LEDC_TIMER_10_BIT   // 0-1023
#define HAT_MR_DUTY_SOFT    358                  // ~35% = comfort
#define HAT_MR_DUTY_FIRM    700                  // ~68% = sport
#define HAT_MR_DUTY_OFF     0

// ── Power ─────────────────────────────────────────────────────────────────────
// 3.3V logic rail: LMR14030SDDAR buck (LCSC C182078), +12V_INT -> 3.3V/300mA.
//   Feeds P4-Nano via P1-pin1 and P2-pin5.
//   Pull inline fuse on J_PWR_IN before connecting USB for firmware updates.
//
// 5V display rail: LM2596S-5.0 buck, +12V_INT -> 5V/3A.
//   Feeds Waveshare 12.3-DSI-TOUCH-A via J_DISP_PWR (requires 5V/1A min).
//   ON/OFF pin now under firmware control — see HAT_DISP_PWR_EN below.
//
// 12V MagneRide rail: +12V_INT from J_12V_IN via D_REVP (SS34 Schottky).
//   Feeds MOSFET drains and VSS sensor supply only.
//   Fuse externally (10A blade fuse recommended).

// ── Display 5V Power Enable ──────────────────────────────────────────────────
// LM2596S-5 ON/OFF pin: active-HIGH = disabled/shutdown, LOW/GND = enabled
// (per datasheet). Previously hard-tied to GND (always on); now routed to
// GPIO4 so firmware can power-cycle the display rail for recovery from a
// hung DSI link, and so the display's onboard 3.3V touch/logic regulator
// loses its source (and stops backfeeding ESP_3V3 via the DSI cable) when
// intentionally powered off.
// 10k pull-up to 3V3 (R_5V_EN1, net 5V0_ENABLE) sets the default/unconfigured state to
// HIGH = OFF — display stays unpowered (fail-safe) until firmware explicitly
// drives this pin LOW.
#define HAT_DISP_PWR_EN     GPIO_NUM_4    // P1 pin12  -> LM2596S-5 ON/OFF
// gpio_set_level(HAT_DISP_PWR_EN, 0);  // enable display 5V rail
// gpio_set_level(HAT_DISP_PWR_EN, 1);  // disable display 5V rail (or let float high via pull-up)

// ── EPB helper (implement in application code) ────────────────────────────────
// void epb_press(void) {
//     gpio_set_level(HAT_EPB_OUT, 0);
//     vTaskDelay(pdMS_TO_TICKS(200));
//     gpio_set_level(HAT_EPB_OUT, 1);
// }

// ── TWAI config snippet ───────────────────────────────────────────────────────
// twai_general_config_t g_config =
//     TWAI_GENERAL_CONFIG_DEFAULT(HAT_TWAI_TX, HAT_TWAI_RX, TWAI_MODE_NORMAL);

// ── MagneRide LEDC config snippet ────────────────────────────────────────────
// ledc_timer_config_t mr_timer = {
//     .speed_mode      = LEDC_LOW_SPEED_MODE,
//     .timer_num       = LEDC_TIMER_0,
//     .duty_resolution = HAT_MR_RESOLUTION,
//     .freq_hz         = HAT_MR_FREQ_HZ,
//     .clk_cfg         = LEDC_AUTO_CLK,
// };
// ledc_channel_config_t mr_ch1 = {
//     .gpio_num   = HAT_MR_CH1_PWM,
//     .speed_mode = LEDC_LOW_SPEED_MODE,
//     .channel    = LEDC_CHANNEL_0,
//     .timer_sel  = LEDC_TIMER_0,
//     .duty       = HAT_MR_DUTY_SOFT,
//     .hpoint     = 0,
// };
