#ifndef APP_INDEX_H
#define APP_INDEX_H

#include <glib.h>
#include <gio/gio.h>

typedef struct {
    char *name;
    char *exec;
    char *icon;
    GAppInfo *app_info;
} AppEntry;

typedef struct {
    GHashTable *apps;  /* name -> AppEntry */
    GList *app_list;   /* List of all AppEntry* for iteration */
    gboolean loaded;   /* TRUE once loading is complete */
    gboolean loading;  /* TRUE while async load in progress */
} AppIndex;

/* Callback for async load completion */
typedef void (*AppIndexLoadCallback)(AppIndex *index, gpointer user_data);

AppIndex *app_index_new(void);
void app_index_load(AppIndex *index);
void app_index_load_async(AppIndex *index, AppIndexLoadCallback callback, gpointer user_data);
void app_index_free(AppIndex *index);
int app_index_get_count(AppIndex *index);
gboolean app_index_is_ready(AppIndex *index);

#endif /* APP_INDEX_H */
