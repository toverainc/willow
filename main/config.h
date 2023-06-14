#include "cJSON.h"

bool config_valid;
cJSON *wc;
bool config_get_bool(char *key);
char *config_get_char(char *key);
int config_get_int(char *key);
void config_parse(void);
void config_write(const char *data);