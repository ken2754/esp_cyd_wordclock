#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the colloquial English word clock text for given hour & minute.
 * Rounded to 5-minute intervals. Formatted with newline for display.
 * Remaining minutes (0-4) are represented by trailing periods (e.g. 0: none, 1-4: '.' to '....').
 * 
 * @param hour 0-23
 * @param minute 0-59
 * @param out_text Output buffer
 * @param max_len Size of output buffer
 */
void word_clock_get_text(int hour, int minute, char *out_text, size_t max_len);

#ifdef __cplusplus
}
#endif
