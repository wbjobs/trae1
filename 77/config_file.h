#ifndef VSHAPER_CONFIG_FILE_H
#define VSHAPER_CONFIG_FILE_H

#include "common.h"

int config_file_load(const char *path, app_config_t *config);
int config_file_validate(const app_config_t *config);

#endif
