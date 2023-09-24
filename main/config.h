#include "cJSON.h"

extern bool config_valid;
extern cJSON *wc;
bool config_get_bool(char *key, const bool default_value);
char *config_get_char(const char *key, const char *default_value);
int config_get_int(char *key, const int default_value);
void config_parse(void);
void config_write(const char *data);
