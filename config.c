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
    config->file_openers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    
    /* Config file: ~/.config/thundersearch/config */
    config->config_path = g_build_filename(config_dir, "thundersearch", "config", NULL);
    
    /* History file: ~/.local/share/thundersearch/history */
    config->history_path = g_build_filename(data_dir, "thundersearch", "history", NULL);

    config->default_dir = NULL;
    config->terminal_cmd = NULL;
    config->win_width = 680;
    config->top_offset = 120;
    config->max_app_results = 10;
    config->max_file_results = 50;
    config->max_win_results = 50;

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

            /* Parse "open .ext1 .ext2 = app_command" */
            if (g_str_has_prefix(line, "open ")) {
                char *equals = strchr(line, '=');
                if (equals) {
                    *equals = '\0';
                    char *exts_part = g_strstrip(line + 5);  /* after "open " */
                    char *app_cmd = g_strstrip(equals + 1);

                    if (exts_part[0] && app_cmd[0]) {
                        /* Split extensions by whitespace */
                        char **exts = g_strsplit_set(exts_part, " \t", -1);
                        for (int j = 0; exts[j] != NULL; j++) {
                            char *ext = g_strstrip(exts[j]);
                            if (ext[0] == '.') {
                                /* Store as lowercase for case-insensitive lookup */
                                char *ext_lower = g_ascii_strdown(ext, -1);
                                g_hash_table_insert(config->file_openers,
                                                    ext_lower,
                                                    g_strdup(app_cmd));
                            }
                        }
                        g_strfreev(exts);
                    }
                }
                continue;
            }

            /* Parse "set key = value" (numeric/path settings) */
            if (g_str_has_prefix(line, "set ")) {
                char *equals = strchr(line, '=');
                if (equals) {
                    *equals = '\0';
                    char *key = g_strstrip(line + 4);
                    char *val = g_strstrip(equals + 1);
                    if (strcmp(key, "win_width") == 0)
                        config->win_width = atoi(val);
                    else if (strcmp(key, "top_offset") == 0)
                        config->top_offset = atoi(val);
                    else if (strcmp(key, "max_app_results") == 0)
                        config->max_app_results = atoi(val);
                    else if (strcmp(key, "max_file_results") == 0)
                        config->max_file_results = atoi(val);
                    else if (strcmp(key, "max_win_results") == 0)
                        config->max_win_results = atoi(val);
                    else if (strcmp(key, "default_dir") == 0) {
                        g_free(config->default_dir);
                        if (val[0] == '~')
                            config->default_dir = g_build_filename(g_get_home_dir(), val + 1, NULL);
                        else
                            config->default_dir = g_strdup(val);
                    } else if (strcmp(key, "terminal") == 0) {
                        g_free(config->terminal_cmd);
                        config->terminal_cmd = g_strdup(val);
                    }
                }
                continue;
            }

            /* Parse "nickname = real name" */
            char *equals = strchr(line, '=');
            if (equals) {
                *equals = '\0';
                char *nickname = g_strstrip(line);
                char *real_name = g_strstrip(equals + 1);

                if (nickname[0] && real_name[0]) {
                    /* Handle special config keys */
                    if (strcmp(nickname, "default_dir") == 0) {
                        /* Expand ~ to home directory */
                        if (real_name[0] == '~') {
                            config->default_dir = g_build_filename(
                                g_get_home_dir(), real_name + 1, NULL);
                        } else {
                            config->default_dir = g_strdup(real_name);
                        }
                    } else {
                        g_hash_table_insert(config->nicknames,
                                          g_strdup(nickname),
                                          g_strdup(real_name));
                    }
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
            fprintf(fp, "\n");
            fprintf(fp, "# --- Appearance ---\n");
            fprintf(fp, "# set win_width = 680        # window width in pixels\n");
            fprintf(fp, "# set top_offset = 120       # distance from top of monitor in pixels\n");
            fprintf(fp, "\n");
            fprintf(fp, "# --- Claude integration ---\n");
            fprintf(fp, "# set terminal = kitty       # terminal for 'cc' sessions (default: autodetect)\n");
            fprintf(fp, "\n");
            fprintf(fp, "# --- Result limits ---\n");
            fprintf(fp, "# set max_app_results = 10\n");
            fprintf(fp, "# set max_file_results = 50\n");
            fprintf(fp, "# set max_win_results = 50\n");
            fprintf(fp, "\n");
            fprintf(fp, "# --- Default directory for /fd command ---\n");
            fprintf(fp, "# set default_dir = ~/Projects\n");
            fprintf(fp, "\n");
            fprintf(fp, "# --- App nicknames (nickname = Real Application Name) ---\n");
            fprintf(fp, "# ff = Firefox\n");
            fprintf(fp, "# spot = Spotify\n");
            fprintf(fp, "# term = Terminal\n");
            fprintf(fp, "\n");
            fprintf(fp, "# --- File openers for /f/o command ---\n");
            fprintf(fp, "# open .pdf .epub = zathura\n");
            fprintf(fp, "# open .png .jpg .jpeg .gif .webp = imv\n");
            fprintf(fp, "# open .mp4 .mkv .avi .webm = mpv\n");
            fprintf(fp, "# open .txt .md .cfg .conf = gedit\n");
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
    g_hash_table_destroy(config->file_openers);
    g_free(config->config_path);
    g_free(config->history_path);
    g_free(config->default_dir);
    g_free(config->terminal_cmd);
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

const char *
config_get_default_dir(Config *config)
{
    if (config->default_dir)
        return config->default_dir;
    return g_get_home_dir();
}

const char *
config_get_opener(Config *config, const char *filename)
{
    if (!filename)
        return NULL;

    /* Find the last dot for extension */
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
        return NULL;

    char *ext_lower = g_ascii_strdown(dot, -1);
    const char *app = g_hash_table_lookup(config->file_openers, ext_lower);
    g_free(ext_lower);

    return app;  /* NULL means use xdg-open */
}
