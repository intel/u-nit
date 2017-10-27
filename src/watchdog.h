#ifndef WATCHDOG_HEADER_
#define WATCHDOG_HEADER_

#include <stdbool.h>

void start_watchdog(void);
void close_watchdog(bool disarm);

#endif
