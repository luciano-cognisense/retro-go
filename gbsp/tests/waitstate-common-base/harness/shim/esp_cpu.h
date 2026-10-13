/* Host shim: the core uses this only for the profile counters. */
#ifndef ESP_CPU_H_HOST_SHIM
#define ESP_CPU_H_HOST_SHIM
#include <stdint.h>
static inline uint32_t esp_cpu_get_cycle_count(void) { return 0; }
#endif
