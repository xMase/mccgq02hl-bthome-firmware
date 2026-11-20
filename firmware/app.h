// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef APP_H
#define APP_H

#include <stdbool.h>

// Required by app_os_helper (called from the generated sl_event_handler.c). Bare metal: no OS.
void app_init_bt(void);
void app_proceed(void);
bool app_is_process_required(void);
bool app_mutex_acquire(void);
void app_mutex_release(void);

#endif  // APP_H
