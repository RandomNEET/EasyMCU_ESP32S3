#pragma once

#include "esp_err.h"

// Starts a low-priority display task. Failure leaves controller operation intact.
esp_err_t oled_status_init(void);
