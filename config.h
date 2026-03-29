#ifndef CONFIG_H
#define CONFIG_H

#include <glib.h>

typedef struct {
    GHashTable *nicknames;      /* nickname -> real app name */
    GHashTable *usage_counts;   /* app name -> launch count */
    GHashTable *file_openers;   /* ".ext" -> app command */
    char *config_path;
    char *history_path;
    char *default_dir;          /* default directory for /fd command */
} Config;

Config *config_new(void);
void config_load(Config *config);
void config_save_history(Config *config);
void config_free(Config *config);

const char *config_resolve_nickname(Config *config, const char *query);
void config_increment_usage(Config *config, const char *app_name);
int config_get_usage_count(Config *config, const char *app_name);
const char *config_get_default_dir(Config *config);
const char *config_get_opener(Config *config, const char *filename);

#endif /* CONFIG_H */
