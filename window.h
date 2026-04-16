#ifndef WINDOW_H
#define WINDOW_H

#include <gtk/gtk.h>
#include "app_index.h"
#include "config.h"
#include "file_nav.h"
#include "animation.h"

typedef struct {
    GtkWidget *entry;
    GtkWidget *listbox;
    GtkWidget *window;
    GtkWidget *main_container;      /* Main vertical box */
    GtkWidget *search_pill;         /* Pill-shaped search container */
    GtkWidget *results_revealer;    /* GtkRevealer for results */
    GtkWidget *results_container;   /* Box inside revealer */
    GtkWidget *scrolled_window;     /* Scrolled window for results */
    AppIndex *index;
    Config *config;
    GList *current_matches;
    gboolean is_visible;
    GList *current_file_results;    /* Current file search results */
    gboolean suppress_entry_change; /* Suppress on_entry_changed re-trigger */
    guint file_auto_timeout;        /* Debounce timeout for auto-fill */
    AnimationState *show_anim;      /* Window show animation */
    guint anim_tick_id;             /* Animation tick source ID */
    gboolean hiding;                /* TRUE during hide animation */
    gint base_margin_top;           /* Base top margin for animation */
    /* AI query state */
    char *ai_result;                /* Last response from claude -p, or NULL */
    gboolean ai_waiting;            /* TRUE while subprocess is running */
    GCancellable *ai_cancel;        /* Cancels in-flight ai query */
} WindowData;

GtkWidget *create_launcher_window(GtkApplication *app, AppIndex *index, Config *config, WindowData **out_data);
void toggle_window(WindowData *data);

#endif /* WINDOW_H */
