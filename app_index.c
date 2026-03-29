#include "app_index.h"
#include <string.h>

/* Data passed to async loading thread */
typedef struct {
    AppIndex *index;
    AppIndexLoadCallback callback;
    gpointer user_data;
    GList *loaded_apps;  /* Loaded in thread, transferred to main thread */
} LoadAsyncData;

AppIndex *
app_index_new(void)
{
    AppIndex *index = g_new0(AppIndex, 1);
    index->apps = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
    index->app_list = NULL;
    index->loaded = FALSE;
    index->loading = FALSE;
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
    index->loaded = TRUE;
}

/* Thread function for async loading */
static void
load_apps_thread_func(GTask *task, gpointer source_object,
                      gpointer task_data, GCancellable *cancellable)
{
    (void)source_object;
    (void)cancellable;

    LoadAsyncData *data = (LoadAsyncData *)task_data;
    GList *all_apps;
    GList *l;
    GList *loaded = NULL;

    /* Get all installed applications using GIO (blocking I/O) */
    all_apps = g_app_info_get_all();

    for (l = all_apps; l != NULL; l = l->next) {
        GAppInfo *app_info = G_APP_INFO(l->data);
        const char *name;
        const char *exec;
        AppEntry *entry;

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

        entry = g_new0(AppEntry, 1);
        entry->name = g_strdup(name);
        entry->exec = g_strdup(exec);
        entry->app_info = g_object_ref(app_info);
        entry->icon = NULL;

        loaded = g_list_prepend(loaded, entry);

        g_object_unref(app_info);
    }

    g_list_free(all_apps);
    data->loaded_apps = loaded;

    g_task_return_pointer(task, data, NULL);
}

/* Callback when async load completes (runs on main thread) */
static void
load_apps_ready_cb(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
    (void)source_object;
    (void)user_data;

    GTask *task = G_TASK(res);
    LoadAsyncData *data = g_task_propagate_pointer(task, NULL);

    if (!data) return;

    AppIndex *index = data->index;

    /* Transfer loaded apps to the index */
    for (GList *l = data->loaded_apps; l != NULL; l = l->next) {
        AppEntry *entry = (AppEntry *)l->data;
        g_hash_table_insert(index->apps, entry->name, entry);
        index->app_list = g_list_prepend(index->app_list, entry);
    }

    /* Free the temporary list (entries now owned by index) */
    g_list_free(data->loaded_apps);

    index->loaded = TRUE;
    index->loading = FALSE;

    /* Call the completion callback */
    if (data->callback) {
        data->callback(index, data->user_data);
    }

    g_free(data);
}

void
app_index_load_async(AppIndex *index, AppIndexLoadCallback callback, gpointer user_data)
{
    if (index->loading || index->loaded) {
        /* Already loading or loaded */
        if (index->loaded && callback) {
            callback(index, user_data);
        }
        return;
    }

    index->loading = TRUE;

    LoadAsyncData *data = g_new0(LoadAsyncData, 1);
    data->index = index;
    data->callback = callback;
    data->user_data = user_data;
    data->loaded_apps = NULL;

    GTask *task = g_task_new(NULL, NULL, load_apps_ready_cb, NULL);
    g_task_set_task_data(task, data, NULL);
    g_task_run_in_thread(task, load_apps_thread_func);
    g_object_unref(task);
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

gboolean
app_index_is_ready(AppIndex *index)
{
    if (!index) return FALSE;
    return index->loaded;
}
