#include "word_clock.h"
#include <stdio.h>
#include <string.h>

static const char *const HOUR_WORDS[] = {
    "twelve", "one", "two", "three", "four", "five",
    "six", "seven", "eight", "nine", "ten", "eleven", "twelve"
};

void word_clock_get_text(int hour, int minute, char *out_text, size_t max_len)
{
    if (!out_text || max_len == 0) return;

    // Normalize hour to 12-hour format (1 to 12)
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;

    int next_h12 = (h12 % 12) + 1;

    // Colloquial 5-minute block
    int block_min = (minute / 5) * 5;

    switch (block_min) {
        case 0:
            if (hour == 12 && minute < 5) {
                snprintf(out_text, max_len, "It's\ntwelve o'clock");
            } else if (hour == 0 && minute < 5) {
                snprintf(out_text, max_len, "It's\nmidnight");
            } else {
                snprintf(out_text, max_len, "It's\n%s o'clock", HOUR_WORDS[h12]);
            }
            break;
        case 5:
            snprintf(out_text, max_len, "It's five past\n%s", HOUR_WORDS[h12]);
            break;
        case 10:
            snprintf(out_text, max_len, "It's ten past\n%s", HOUR_WORDS[h12]);
            break;
        case 15:
            snprintf(out_text, max_len, "It's a quarter\npast %s", HOUR_WORDS[h12]);
            break;
        case 20:
            snprintf(out_text, max_len, "It's twenty past\n%s", HOUR_WORDS[h12]);
            break;
        case 25:
            snprintf(out_text, max_len, "It's twenty-five\npast %s", HOUR_WORDS[h12]);
            break;
        case 30:
            snprintf(out_text, max_len, "It's half past\n%s", HOUR_WORDS[h12]);
            break;
        case 35:
            snprintf(out_text, max_len, "It's twenty-five\nto %s", HOUR_WORDS[next_h12]);
            break;
        case 40:
            snprintf(out_text, max_len, "It's twenty to\n%s", HOUR_WORDS[next_h12]);
            break;
        case 45:
            snprintf(out_text, max_len, "It's a quarter\nto %s", HOUR_WORDS[next_h12]);
            break;
        case 50:
            snprintf(out_text, max_len, "It's ten to\n%s", HOUR_WORDS[next_h12]);
            break;
        case 55:
            snprintf(out_text, max_len, "It's five to\n%s", HOUR_WORDS[next_h12]);
            break;
        default:
            snprintf(out_text, max_len, "It's\n%s o'clock", HOUR_WORDS[h12]);
            break;
    }

    // Remainder minutes (0-4) are represented by the number of trailing periods
    int rem = minute % 5;
    for (int i = 0; i < rem; i++) {
        size_t len = strlen(out_text);
        if (len + 1 < max_len) {
            out_text[len] = '.';
            out_text[len + 1] = '\0';
        }
    }
}
