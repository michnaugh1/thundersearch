#include "config.h"
#include <stdio.h>
#include <string.h>

Config *
config_new(void)
{
    Config *config = g_new0(Config, 1);
    const char *config_dir = g_get_user_config_dir();
    const char *data_dir = g_get_user_data_dir();
    
    config->nicknames = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    config->usage_counts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    /* Config file: ~/.config/thundersearch/config */
    config->config_path = g_build_filename(config_dir, "thundersearch", "config", NULL);
    
    /* History file: ~/.local/share/thundersearch/history */
    config->history_path = g_build_filename(data_dir, "thundersearch", "history", NULL);
    
    return config;
}

void
config_load(Config *config)
{
    FILE *fp;
    char line[512];
    
    /* Create config directory if it doesn't exist */
    char *config_dir = g_path_get_dirname(config->config_path);
    g_mkdir_with_parents(config_dir, 0755);
    g_free(config_dir);
    
    /* Create history directory if it doesn't exist */
    char *history_dir = g_path_get_dirname(config->history_path);
    g_mkdir_with_parents(history_dir, 0755);
    g_free(history_dir);
    
    /* Load nicknames from config file */
    fp = fopen(config->config_path, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            /* Remove newline */
            line[strcspn(line, "\n")] = 0;
            
            /* Skip empty lines and comments */
            if (line[0] == '\0' || line[0] == '#') {
                continue;
            }
            
            /* Parse "nickname = real name" */
            char *equals = strchr(line, '=');
            if (equals) {
                *equals = '\0';
                char *nickname = g_strstrip(line);
                char *real_name = g_strstrip(equals + 1);
                
                if (nickname[0] && real_name[0]) {
                    g_hash_table_insert(config->nicknames, 
                                      g_strdup(nickname), 
                                      g_strdup(real_name));
                }
            }
        }
        fclose(fp);
        g_print("Loaded %d nicknames from config\n", 
                g_hash_table_size(config->nicknames));
    } else {
        /* Create example config file */
        fp = fopen(config->config_path, "w");
        if (fp) {
            fprintf(fp, "# ThunderSearch Configuration\n");
            fprintf(fp, "# Format: nickname = Real Application Name\n");
            fprintf(fp, "# Example:\n");
            fprintf(fp, "# ff = Firefox\n");
            fprintf(fp, "# spot = Spotify\n");
            fprintf(fp, "# term = Terminal\n");
            fprintf(fp, "\n");
            fclose(fp);
            g_print("Created example config at: %s\n", config->config_path);
        }
    }
    
    /* Load usage history */
    fp = fopen(config->history_path, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            /* Remove newline */
            line[strcspn(line, "\n")] = 0;
            
            /* Parse "app_name:count" */
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *app_name = line;
                int count = atoi(colon + 1);
                
                if (app_name[0] && count > 0) {
                    g_hash_table_insert(config->usage_counts,
                                      g_strdup(app_name),
                                      GINT_TO_POINTER(count));
                }
            }
        }
        fclose(fp);
        g_print("Loaded usage history for %d apps\n", 
                g_hash_table_size(config->usage_counts));
    }
}

void
config_save_history(Config *config)
{
    FILE *fp;
    GHashTableIter iter;
    gpointer key, value;
    
    fp = fopen(config->history_path, "w");
    if (!fp) {
        g_warning("Failed to save history to: %s", config->history_path);
        return;
    }
    
    /* Write all usage counts */
    g_hash_table_iter_init(&iter, config->usage_counts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        fprintf(fp, "%s:%d\n", (char *)key, GPOINTER_TO_INT(value));
    }
    
    fclose(fp);
}

void
config_free(Config *config)
{
    if (!config) return;
    
    /* Save history before freeing */
    config_save_history(config);
    
    g_hash_table_destroy(config->nicknames);
    g_hash_table_destroy(config->usage_counts);
    g_free(config->config_path);
    g_free(config->history_path);
    g_free(config);
}

const char *
config_resolve_nickname(Config *config, const char *query)
{
    const char *real_name = g_hash_table_lookup(config->nicknames, query);
    return real_name ? real_name : query;
}

void
config_increment_usage(Config *config, const char *app_name)
{
    int current_count = GPOINTER_TO_INT(g_hash_table_lookup(config->usage_counts, app_name));
    g_hash_table_insert(config->usage_counts, 
                       g_strdup(app_name), 
                       GINT_TO_POINTER(current_count + 1));
}

int
config_get_usage_count(Config *config, const char *app_name)
{
    return GPOINTER_TO_INT(g_hash_table_lookup(config->usage_counts, app_name));
}
