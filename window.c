#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include "window.h"
#include "matcher.h"
#include "launcher.h"

static void
clear_listbox(GtkListBox *listbox)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(listbox))) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(listbox), child);
    }
}

static void
hide_window(WindowData *data)
{
    if (!data->is_visible) return;
    
    data->is_visible = FALSE;
    
    /* Clear the entry */
    gtk_editable_set_text(GTK_EDITABLE(data->entry), "");
    
    /* Clear results */
    if (data->current_matches) {
        g_list_free(data->current_matches);
        data->current_matches = NULL;
    }
    clear_listbox(GTK_LIST_BOX(data->listbox));
    
    /* Hide window */
    gtk_widget_set_visible(data->window, FALSE);
}

static void
show_window(WindowData *data)
{
    if (data->is_visible) return;
    
    data->is_visible = TRUE;
    
    /* Show window */
    gtk_widget_set_visible(data->window, TRUE);
    
    /* Focus the entry */
    gtk_widget_grab_focus(data->entry);
}

static void
update_results(WindowData *data, const char *query)
{
    GList *matches;
    int match_count;

    /* Clear previous matches */
    if (data->current_matches) {
        g_list_free(data->current_matches);
        data->current_matches = NULL;
    }

    clear_listbox(GTK_LIST_BOX(data->listbox));

    if (!query || strlen(query) == 0) {
        return;
    }

    /* Get matching apps (with config for nicknames and usage) */
    matches = match_apps(data->index, data->config, query, 10);
    match_count = g_list_length(matches);

    /* Store matches */
    data->current_matches = matches;

    /* If exactly one match, launch it immediately */
    if (match_count == 1) {
        AppEntry *app = (AppEntry *)matches->data;
        g_print("Auto-launching: %s\n", app->name);
        
        /* Increment usage count */
        config_increment_usage(data->config, app->name);
        
        launch_app(app);
        
        /* Hide window after launch (daemon mode) */
        hide_window(data);
        return;
    }

    /* Display results */
    for (GList *l = matches; l != NULL; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;
        GtkWidget *row;
        GtkWidget *label;

        label = gtk_label_new(app->name);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        
        row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(data->listbox), row);
    }
}

static void
on_entry_changed(GtkEditable *editable, gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;
    const char *text = gtk_editable_get_text(editable);
    update_results(data, text);
}

static gboolean
on_key_pressed(GtkEventControllerKey *controller,
               guint keyval,
               guint keycode,
               GdkModifierType state,
               gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;

    (void)controller;
    (void)keycode;
    (void)state;

    /* ESC to hide */
    if (keyval == GDK_KEY_Escape) {
        hide_window(data);
        return TRUE;
    }

    /* Enter to launch first result */
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        if (data->current_matches && g_list_length(data->current_matches) > 0) {
            AppEntry *app = (AppEntry *)data->current_matches->data;
            g_print("Launching: %s\n", app->name);
            
            /* Increment usage count */
            config_increment_usage(data->config, app->name);
            
            launch_app(app);
            
            hide_window(data);
        }
        return TRUE;
    }

    return FALSE;
}

static void
on_window_destroy(GtkWidget *widget, gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;
    
    (void)widget;
    
    if (data->current_matches) {
        g_list_free(data->current_matches);
    }
    g_free(data);
}

void
toggle_window(WindowData *data)
{
    if (data->is_visible) {
        hide_window(data);
    } else {
        show_window(data);
    }
}

GtkWidget *
create_launcher_window(GtkApplication *app, AppIndex *index, Config *config, WindowData **out_data)
{
    GtkWidget *window;
    GtkWidget *box;
    GtkWidget *entry;
    GtkWidget *scrolled;
    GtkWidget *listbox;
    GtkEventController *key_controller;
    GtkCssProvider *css_provider;
    WindowData *data;

    /* Create window */
    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "ThunderSearch");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

    /* Apply CSS for rounded corners */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        "window {"
        "  border-radius: 12px;"
        "}"
        "entry {"
        "  border-radius: 8px;"
        "  padding: 8px;"
        "  font-size: 14pt;"
        "}"
        "listbox {"
        "  border-radius: 8px;"
        "}"
        "listboxrow {"
        "  padding: 8px;"
        "}");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    /* Setup layer shell */
    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window), 
                                 GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    
    /* Center the window */
    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 100);

    /* Create main container */
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_window_set_child(GTK_WINDOW(window), box);

    /* Create search entry */
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Type to search applications...");
    gtk_box_append(GTK_BOX(box), entry);

    /* Create scrolled window for results */
    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled);

    /* Create listbox for results */
    listbox = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), listbox);

    /* Setup window data */
    data = g_new0(WindowData, 1);
    data->entry = entry;
    data->listbox = listbox;
    data->window = window;
    data->index = index;
    data->config = config;
    data->current_matches = NULL;
    data->is_visible = TRUE;

    /* Connect signals */
    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), data);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), data);

    /* Setup key event controller */
    key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed", 
                     G_CALLBACK(on_key_pressed), data);
    gtk_widget_add_controller(window, key_controller);

    /* Focus the entry */
    gtk_widget_grab_focus(entry);

    /* Return window data to caller */
    if (out_data) {
        *out_data = data;
    }

    return window;
}
