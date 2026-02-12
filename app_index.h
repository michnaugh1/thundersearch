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
} AppIndex;

AppIndex *app_index_new(void);
void app_index_load(AppIndex *index);
void app_index_free(AppIndex *index);
int app_index_get_count(AppIndex *index);

#endif /* APP_INDEX_H */
