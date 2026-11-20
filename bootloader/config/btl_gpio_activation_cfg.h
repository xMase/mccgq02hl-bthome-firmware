// Copyright (C) 2026 xMase
// SPDX-License-Identifier: Zlib
// See bootloader/LICENSE.

// GPIO activation for this board (replaces the SDK template, whose pin is left unset).
// PB0 held low at reset enters the AppLoader; the component reads it with the internal pull-up.
#ifndef BTL_GPIO_ACTIVATION_CONFIG_H
#define BTL_GPIO_ACTIVATION_CONFIG_H

#define SL_GPIO_ACTIVATION_POLARITY LOW
#define SL_BTL_BUTTON_PORT gpioPortB
#define SL_BTL_BUTTON_PIN 0

#endif  // BTL_GPIO_ACTIVATION_CONFIG_H
