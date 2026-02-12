#include "app_index.h"
#include <string.h>

AppIndex *
app_index_new(void)
{
    AppIndex *index = g_new0(AppIndex, 1);
    index->apps = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    index->app_list = NULL;
    return index;
}

static void
app_entry_free(AppEntry *entry)
{
    if (!entry) return;
    
    g_free(entry->name);
    g_free(entry->exec);
    g_free(entry->icon);
    if (entry->app_info) {
        g_object_unref(entry->app_info);
    }
    g_free(entry);
}

void
app_index_load(AppIndex *index)
{
    GList *all_apps;
    GList *l;

    /* Get all installed applications using GIO */
    all_apps = g_app_info_get_all();

    for (l = all_apps; l != NULL; l = l->next) {
        GAppInfo *app_info = G_APP_INFO(l->data);
        const char *name;
        const char *exec;
        AppEntry *entry;

        /* Skip apps that should not be shown */
        if (!g_app_info_should_show(app_info)) {
            g_object_unref(app_info);
            continue;
        }

        name = g_app_info_get_name(app_info);
        exec = g_app_info_get_executable(app_info);

        if (!name || !exec) {
            g_object_unref(app_info);
            continue;
        }

        /* Create app entry */
        entry = g_new0(AppEntry, 1);
        entry->name = g_strdup(name);
        entry->exec = g_strdup(exec);
        entry->app_info = g_object_ref(app_info);
        entry->icon = NULL;  /* Skip icon loading for speed */

        /* Store in hash table and list */
        g_hash_table_insert(index->apps, entry->name, entry);
        index->app_list = g_list_prepend(index->app_list, entry);

        g_object_unref(app_info);
    }

    g_list_free(all_apps);
}

void
app_index_free(AppIndex *index)
{
    if (!index) return;

    /* Free all app entries */
    g_list_free_full(index->app_list, (GDestroyNotify)app_entry_free);
    
    /* Free hash table (entries already freed) */
    g_hash_table_destroy(index->apps);
    
    g_free(index);
}

int
app_index_get_count(AppIndex *index)
{
    if (!index) return 0;
    return g_list_length(index->app_list);
}
