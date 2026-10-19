#pragma once
#include <stddef.h>
void vTaskDelay(unsigned ticks);
void vTaskDelete(void *task);
int xTaskCreate(void (*entry)(void *), const char *name, unsigned stack,
                void *arg, unsigned priority, void *handle);
