#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

typedef struct {
    GHashTable *nicknames;    /* nickname -> real app name */
    GHashTable *usage_counts; /* app name -> launch count */
    char *config_path;
    char *history_path;
} Config;

Config *config_new(void);
void config_load(Config *config);
void config_save_history(Config *config);
void config_free(Config *config);

const char *config_resolve_nickname(Config *config, const char *query);
void config_increment_usage(Config *config, const char *app_name);
int config_get_usage_count(Config *config, const char *app_name);

#endif /* CONFIG_H */
