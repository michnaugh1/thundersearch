#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>
#include "app_index.h"
#include "config.h"
#include "file_nav.h"
#include "win_nav.h"

typedef struct {
    GtkWidget *entry;
    GtkWidget *listbox;
    GtkWidget *window;
    AppIndex *index;
    Config *config;
    GList *current_matches;
    gboolean is_visible;
    GList *current_file_results;    /* Current file search results */
    GList *current_win_results;     /* Current window search results */
    gboolean suppress_entry_change; /* Suppress on_entry_changed re-trigger */
    guint file_auto_timeout;        /* Debounce timeout for auto-fill */
    guint win_auto_timeout;         /* Debounce timeout for window auto-focus */
} WindowData;

GtkWidget *create_launcher_window(GtkApplication *app, AppIndex *index, Config *config, WindowData **out_data);
void toggle_window(WindowData *data);

#endif /* WINDOW_H */
