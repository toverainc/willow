#include "sdkconfig.h"

#ifdef CONFIG_WILLOW_DEBUG_RUNTIME_STATS
void task_debug_runtime_stats(void *data);
#endif

void task_timer(void *data);