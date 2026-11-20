// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "hw.h"
#include "em_device.h"
#include "em_emu.h"
#include "em_gpio.h"
#include "em_iadc.h"
#include "em_prs.h"
#include "em_wdog.h"
#include "gpiointerrupt.h"
#include "app_assert.h"
#include "sl_clock_manager.h"

// Pins (README, hardware map).
#define DOOR_PORT gpioPortA
#define DOOR_PIN 3
#define DOOR_INT 3  // series 2: interrupt numbers 0-3 serve pins 0-3
#define BUTTON_PORT gpioPortB
#define BUTTON_PIN 0
#define BUTTON_INT 0
#define LED_PORT gpioPortC
#define LED_PIN 0
#define LIGHT_PORT gpioPortC
#define LIGHT_PIN 1
#define LIGHT_SUPPLY_PORT gpioPortD
#define LIGHT_SUPPLY_PIN 4

#define IADC_SRC_HZ 10000000u
#define IADC_ADC_HZ 10000000u

static void (*door_cb)(void);
static void (*button_cb)(void);
static void (*battery_cb)(uint16_t code);
static volatile bool battery_armed;  // set by hw_battery_arm, cleared by the IADC interrupt

static void door_irq(uint8_t int_no)
{
  (void)int_no;
  if (door_cb) {
    door_cb();
  }
}

static void button_irq(uint8_t int_no)
{
  (void)int_no;
  if (button_cb) {
    button_cb();
  }
}

void hw_init(void)
{
#ifndef BUILD_RELEASE
  EMU->CTRL_SET = EMU_CTRL_EM2DBGEN;  // keep SWD usable in EM2; the release build measures power
#endif
  // Out = 1 on an input enables the glitch filter, as stock does.
  GPIO_PinModeSet(DOOR_PORT, DOOR_PIN, gpioModeInput, 1);
  GPIO_PinModeSet(BUTTON_PORT, BUTTON_PIN, gpioModeInput, 1);
  GPIO_PinModeSet(LED_PORT, LED_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(LIGHT_SUPPLY_PORT, LIGHT_SUPPLY_PIN, gpioModeDisabled, 0);
  GPIO_PinModeSet(LIGHT_PORT, LIGHT_PIN, gpioModeDisabled, 0);
  GPIO->CDBUSALLOC |= GPIO_CDBUSALLOC_CDODD0_ADC0;  // PC1 to IADC0

  GPIOINT_Init();
  GPIOINT_CallbackRegister(DOOR_INT, door_irq);
  GPIO_ExtIntConfig(DOOR_PORT, DOOR_PIN, DOOR_INT, true, true, true);
  GPIOINT_CallbackRegister(BUTTON_INT, button_irq);
  GPIO_ExtIntConfig(BUTTON_PORT, BUTTON_PIN, BUTTON_INT, true, true, true);

  sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_IADC0);
  // Radio TX active -> IADC single trigger. The PRS async channel and the IADC on FSRCO
  // (slcp) keep the armed conversion waiting through EM2.
  sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_PRS);
  int ch = PRS_GetFreeChannel(prsTypeAsync);
  app_assert(ch >= 0, "no free PRS channel");
  PRS_SourceAsyncSignalSet((unsigned)ch, PRS_ASYNC_CH_CTRL_SOURCESEL_RAC, PRS_ASYNC_CH_CTRL_SIGSEL_RACTX);
  PRS_ConnectConsumer((unsigned)ch, prsTypeAsync, prsConsumerIADC0_SINGLETRIGGER);
  NVIC_ClearPendingIRQ(IADC_IRQn);
  NVIC_EnableIRQ(IADC_IRQn);
  sl_clock_manager_enable_bus_clock(SL_BUS_CLOCK_WDOG0);
  CMU->WDOG0CLKCTRL = CMU_WDOG0CLKCTRL_CLKSEL_ULFRCO;
  WDOG_Init_TypeDef wdog = WDOG_INIT_DEFAULT;
  wdog.perSel = wdogPeriod_256k;  // ~256 s on the 1 kHz ULFRCO; fed by the 60 s light timer
  wdog.em2Run = true;
  wdog.debugRun = false;
  WDOGn_Init(WDOG0, &wdog);
}

uint8_t hw_door_level(void)
{
  return (uint8_t)GPIO_PinInGet(DOOR_PORT, DOOR_PIN);
}

void hw_door_irq_register(void (*cb)(void))
{
  door_cb = cb;
}

uint8_t hw_button_level(void)
{
  return (uint8_t)GPIO_PinInGet(BUTTON_PORT, BUTTON_PIN);
}

void hw_button_irq_register(void (*cb)(void))
{
  button_cb = cb;
}

// Sets up config 0 and the single queue. The IADC is set up again for every conversion:
// light and battery need different references, and conversions are seconds to minutes apart.
static void iadc_setup(IADC_CfgReference_t ref, IADC_PosInput_t pos, IADC_TriggerSel_t trigger)
{
  IADC_Init_t init = IADC_INIT_DEFAULT;
  IADC_AllConfigs_t cfg = IADC_ALLCONFIGS_DEFAULT;
  IADC_InitSingle_t single = IADC_INITSINGLE_DEFAULT;
  IADC_SingleInput_t input = IADC_SINGLEINPUT_DEFAULT;

  init.srcClkPrescale = IADC_calcSrcClkPrescale(IADC0, IADC_SRC_HZ, 0);
  cfg.configs[0].reference = ref;
  cfg.configs[0].analogGain = iadcCfgAnalogGain1x;
  cfg.configs[0].osrHighSpeed = iadcCfgOsrHighSpeed2x;
  cfg.configs[0].adcClkPrescale = IADC_calcAdcClkPrescale(IADC0, IADC_ADC_HZ, 0,
                                                          iadcCfgModeNormal, init.srcClkPrescale);
  input.posInput = pos;
  input.negInput = iadcNegInputGnd;
  single.triggerSelect = trigger;

  // IADC_reset waits for a running conversion and clears IEN only at its end, so the IRQ handler,
  // which also resets the IADC, could run inside it. Mask the IRQ: a sample that completes here is
  // lost and a pending arm ends; the callers re-arm.
  NVIC_DisableIRQ(IADC_IRQn);
  IADC_reset(IADC0);
  NVIC_ClearPendingIRQ(IADC_IRQn);
  NVIC_EnableIRQ(IADC_IRQn);
  IADC_init(IADC0, &init, &cfg);
  IADC_initSingle(IADC0, &single, &input);
}

// One polled single conversion.
static uint16_t iadc_read(IADC_CfgReference_t ref, IADC_PosInput_t pos)
{
  iadc_setup(ref, pos, iadcTriggerSelImmediate);
  IADC_command(IADC0, iadcCmdStartSingle);
  while (!(IADC_getStatus(IADC0) & IADC_STATUS_SINGLEFIFODV)) {
  }
  uint16_t code = (uint16_t)IADC_pullSingleFifoResult(IADC0).data;
  IADC_reset(IADC0);  // leaves the IADC disabled between reads
  return code;
}

static void light_supply(bool on)
{
  GPIO_PinModeSet(LIGHT_SUPPLY_PORT, LIGHT_SUPPLY_PIN, on ? gpioModePushPull : gpioModeDisabled, on);
}

void hw_light_on(void)
{
  light_supply(true);
}

uint16_t hw_light_read(void)
{
  uint16_t code = iadc_read(iadcCfgReferenceVddx, iadcPosInputPortCPin1);
  light_supply(false);
  if (battery_armed) {
    hw_battery_arm(battery_cb);  // the light read reset the IADC before a TX sampled the battery
  }
  return code;
}

void hw_battery_arm(void (*cb)(uint16_t code))
{
  iadc_setup(iadcCfgReferenceInt1V2, iadcPosInputAvdd, iadcTriggerSelPrs0PosEdge);
  battery_cb = cb;
  battery_armed = true;
  IADC_enableInt(IADC0, IADC_IEN_SINGLEDONE);
  IADC_command(IADC0, iadcCmdStartSingle);  // with a PRS trigger this waits for the edge
}

void IADC_IRQHandler(void)  // cppcheck-suppress unusedFunction ; called by the SDK (vector table)
{
  uint16_t code = (uint16_t)IADC_pullSingleFifoResult(IADC0).data;
  IADC_reset(IADC0);  // one sample per arm; later TX edges find the IADC off
  battery_armed = false;
  if (battery_cb) {
    battery_cb(code);
  }
}

void hw_led(bool on)
{
  if (on) {
    GPIO_PinOutSet(LED_PORT, LED_PIN);
  } else {
    GPIO_PinOutClear(LED_PORT, LED_PIN);
  }
}

void hw_wdog_feed(void)
{
  WDOGn_Feed(WDOG0);
}

// Overrides the SDK's weak handler (an EM0 busy loop). app_assert's BKPT ends here when no
// debugger is attached, and so do CPU faults. Wait in EM2 until WDOG0 (em2Run, ~256 s) resets
// the chip: a fault that persists across boots then costs little battery instead of days of EM0.
// HardFault runs at priority -1, so no interrupt wakes it; only the watchdog reset ends the wait.
void HardFault_Handler(void)  // cppcheck-suppress unusedFunction ; called by the SDK (vector table)
{
  for (;;) {
    EMU_EnterEM2(false);
  }
}
