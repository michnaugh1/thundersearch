#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>
#include "app_index.h"
#include "config.h"

typedef struct {
    GtkWidget *entry;
    GtkWidget *listbox;
    GtkWidget *window;
    AppIndex *index;
    Config *config;
    GList *current_matches;
    gboolean is_visible;
} WindowData;

GtkWidget *create_launcher_window(GtkApplication *app, AppIndex *index, Config *config, WindowData **out_data);
void toggle_window(WindowData *data);

#endif /* WINDOW_H */
