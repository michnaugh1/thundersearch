#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <string.h>
#include "window.h"
#include "matcher.h"
#include "launcher.h"
#include "file_nav.h"

static void
clear_listbox(GtkListBox *listbox)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(listbox))) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(listbox), child);
    }
}

static void
clear_file_results(WindowData *data)
{
    if (data->current_file_results) {
        g_list_free_full(data->current_file_results, (GDestroyNotify)file_entry_free);
        data->current_file_results = NULL;
    }
}

static void
cancel_file_timeout(WindowData *data)
{
    if (data->file_auto_timeout > 0) {
        g_source_remove(data->file_auto_timeout);
        data->file_auto_timeout = 0;
    }
}

static void
hide_window(WindowData *data)
{
    if (!data->is_visible) return;

    data->is_visible = FALSE;

    cancel_file_timeout(data);

    data->suppress_entry_change = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(data->entry), "");
    data->suppress_entry_change = FALSE;

    if (data->current_matches) {
        g_list_free(data->current_matches);
        data->current_matches = NULL;
    }
    clear_file_results(data);
    clear_listbox(GTK_LIST_BOX(data->listbox));

    gtk_widget_set_visible(data->window, FALSE);
}

static void
show_window(WindowData *data)
{
    if (data->is_visible) return;

    data->is_visible = TRUE;
    gtk_widget_set_visible(data->window, TRUE);
    gtk_widget_grab_focus(data->entry);
}

/* --- App search (unchanged logic) --- */

static void
update_app_results(WindowData *data, const char *query)
{
    GList *matches;
    int match_count;

    if (data->current_matches) {
        g_list_free(data->current_matches);
        data->current_matches = NULL;
    }
    clear_listbox(GTK_LIST_BOX(data->listbox));

    if (!query || strlen(query) == 0)
        return;

    matches = match_apps(data->index, data->config, query, 10);
    match_count = g_list_length(matches);
    data->current_matches = matches;

    if (match_count == 1) {
        AppEntry *app = (AppEntry *)matches->data;
        config_increment_usage(data->config, app->name);
        launch_app(app);
        hide_window(data);
        return;
    }

    for (GList *l = matches; l != NULL; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;
        GtkWidget *label = gtk_label_new(app->name);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(data->listbox), row);
    }
}

/* --- File navigation --- */

/*
 * Detect command prefix. Returns pointer to after-prefix text, or NULL.
 * Sets out_prefix (allocated), out_cmd.
 */
static const char *
detect_file_prefix(const char *text, char **out_prefix, FileCommand *out_cmd,
                   Config *config)
{
    (void)config;

    if (g_str_has_prefix(text, "/f/o ")) {
        *out_prefix = g_strdup("/f/o ");
        *out_cmd = FILE_CMD_OPEN;
        return text + 5;
    }
    if (g_str_has_prefix(text, "/fd ")) {
        *out_prefix = g_strdup("/fd ");
        *out_cmd = FILE_CMD_DEFAULT;
        return text + 4;
    }
    if (g_str_has_prefix(text, "/f ")) {
        *out_prefix = g_strdup("/f ");
        *out_cmd = FILE_CMD_BROWSE;
        return text + 3;
    }

    return NULL;
}

/*
 * Parse the after-prefix text into a search directory and query.
 *
 * Path mode ("/Documents/tmp1/query"):
 *   search_dir = base_dir/Documents/tmp1
 *   query = "query"
 *
 * Simple mode ("Documents"):
 *   search_dir = base_dir
 *   query = "Documents"
 */
static void
parse_file_query(const char *after_prefix, const char *base_dir,
                 char **out_dir, char **out_query, gboolean *out_path_mode)
{
    if (after_prefix[0] == '/') {
        *out_path_mode = TRUE;

        const char *last_slash = strrchr(after_prefix, '/');

        /* Query is everything after the last slash */
        *out_query = g_strdup(last_slash + 1);

        if (last_slash == after_prefix) {
            /* Only one slash: just "/query" - search in base_dir */
            *out_dir = g_strdup(base_dir);
        } else {
            /* Multiple slashes: /dir1/dir2/.../query */
            /* Extract relative directory (between first slash and last slash) */
            size_t rel_len = last_slash - after_prefix - 1;
            char *rel_dir = g_strndup(after_prefix + 1, rel_len);
            *out_dir = g_build_filename(base_dir, rel_dir, NULL);
            g_free(rel_dir);
        }
    } else {
        *out_path_mode = FALSE;
        *out_dir = g_strdup(base_dir);
        *out_query = g_strdup(after_prefix);
    }
}

/* Display a list of FileEntry results in the listbox */
static void
display_file_results(WindowData *data)
{
    for (GList *l = data->current_file_results; l != NULL; l = l->next) {
        FileEntry *entry = (FileEntry *)l->data;
        char *display_name;

        if (entry->is_dir) {
            display_name = g_strdup_printf("(%s)", entry->name);
        } else {
            display_name = g_strdup(entry->name);
        }

        GtkWidget *label = gtk_label_new(display_name);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        g_free(display_name);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), label);
        gtk_list_box_append(GTK_LIST_BOX(data->listbox), row);
    }
}

/*
 * Debounce callback: fires 200ms after last keystroke.
 * Auto-fills the matched name and performs the appropriate action.
 */
static gboolean
file_auto_action_cb(gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;
    data->file_auto_timeout = 0;

    if (!data->current_file_results)
        return G_SOURCE_REMOVE;
    if (g_list_length(data->current_file_results) != 1)
        return G_SOURCE_REMOVE;

    FileEntry *match = (FileEntry *)data->current_file_results->data;

    /* Re-parse current text to figure out how to auto-fill */
    const char *text = gtk_editable_get_text(GTK_EDITABLE(data->entry));
    char *prefix = NULL;
    FileCommand cmd;
    const char *after_prefix = detect_file_prefix(text, &prefix, &cmd, data->config);

    if (!after_prefix) {
        g_free(prefix);
        return G_SOURCE_REMOVE;
    }

    gboolean path_mode = (after_prefix[0] == '/');

    if (path_mode) {
        /* Find the last slash to know where the query starts */
        const char *last_slash = strrchr(after_prefix, '/');
        /* Length of text up to and including the last slash */
        size_t stable_len = strlen(prefix) + (size_t)(last_slash - after_prefix) + 1;

        /* Save match data before clearing results (avoids use-after-free) */
        char *saved_path = g_strdup(match->full_path);
        char *saved_name = g_strdup(match->name);
        gboolean saved_is_dir = match->is_dir;

        if (saved_is_dir) {
            /* Auto-fill: text_before_query + matched_name + "/" */
            char *new_text = g_strdup_printf("%.*s%s/",
                                             (int)stable_len, text, saved_name);

            data->suppress_entry_change = TRUE;
            gtk_editable_set_text(GTK_EDITABLE(data->entry), new_text);
            gtk_editable_set_position(GTK_EDITABLE(data->entry), -1);
            data->suppress_entry_change = FALSE;
            g_free(new_text);

            /* Show contents of the new directory */
            clear_file_results(data);
            clear_listbox(GTK_LIST_BOX(data->listbox));

            GList *results = file_nav_search(saved_path, "", 10);
            data->current_file_results = results;
            display_file_results(data);

        } else {
            /* Auto-fill file name, then open */
            char *new_text = g_strdup_printf("%.*s%s",
                                             (int)stable_len, text, saved_name);

            data->suppress_entry_change = TRUE;
            gtk_editable_set_text(GTK_EDITABLE(data->entry), new_text);
            data->suppress_entry_change = FALSE;
            g_free(new_text);

            if (cmd == FILE_CMD_OPEN) {
                file_nav_open_default(saved_path);
            } else {
                char *parent = g_path_get_dirname(saved_path);
                file_nav_open_file_manager(parent);
                g_free(parent);
            }
            hide_window(data);
        }

        g_free(saved_path);
        g_free(saved_name);
    } else {
        /* Simple mode: save match data before any clearing */
        char *saved_path = g_strdup(match->full_path);
        char *saved_name = g_strdup(match->name);
        gboolean saved_is_dir = match->is_dir;

        /* Auto-fill the name and open */
        char *new_text = g_strdup_printf("%s%s", prefix, saved_name);

        data->suppress_entry_change = TRUE;
        gtk_editable_set_text(GTK_EDITABLE(data->entry), new_text);
        data->suppress_entry_change = FALSE;
        g_free(new_text);

        if (saved_is_dir) {
            file_nav_open_file_manager(saved_path);
        } else {
            if (cmd == FILE_CMD_OPEN) {
                file_nav_open_default(saved_path);
            } else {
                char *parent = g_path_get_dirname(saved_path);
                file_nav_open_file_manager(parent);
                g_free(parent);
            }
        }

        g_free(saved_path);
        g_free(saved_name);
        hide_window(data);
    }

    g_free(prefix);
    return G_SOURCE_REMOVE;
}

static void
update_file_results(WindowData *data, const char *after_prefix,
                    FileCommand cmd)
{
    char *search_dir = NULL;
    char *query = NULL;
    gboolean path_mode = FALSE;
    const char *base_dir;

    cancel_file_timeout(data);
    clear_file_results(data);
    clear_listbox(GTK_LIST_BOX(data->listbox));

    /* Determine base directory */
    if (cmd == FILE_CMD_DEFAULT)
        base_dir = config_get_default_dir(data->config);
    else
        base_dir = g_get_home_dir();

    /* Parse the path/query from the text */
    parse_file_query(after_prefix, base_dir, &search_dir, &query, &path_mode);

    /* Only search if the directory exists */
    GList *results = NULL;
    if (g_file_test(search_dir, G_FILE_TEST_IS_DIR)) {
        results = file_nav_search(search_dir, query, 10);
    }

    data->current_file_results = results;
    int result_count = g_list_length(results);

    /* Schedule debounced auto-fill if exactly one result */
    if (result_count == 1) {
        data->file_auto_timeout = g_timeout_add(200, file_auto_action_cb, data);
    }

    /* Always display results */
    display_file_results(data);

    g_free(search_dir);
    g_free(query);
}

/* --- Signal handlers --- */

static void
on_entry_changed(GtkEditable *editable, gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;
    const char *text;
    char *prefix = NULL;
    FileCommand cmd;
    const char *after_prefix;

    if (data->suppress_entry_change)
        return;

    text = gtk_editable_get_text(editable);

    after_prefix = detect_file_prefix(text, &prefix, &cmd, data->config);
    if (after_prefix) {
        g_free(prefix);
        update_file_results(data, after_prefix, cmd);
        return;
    }

    g_free(prefix);

    /* Normal app search */
    cancel_file_timeout(data);
    clear_file_results(data);
    update_app_results(data, text);
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

    if (keyval == GDK_KEY_Escape) {
        hide_window(data);
        return TRUE;
    }

    /* Enter: perform action */
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        const char *text = gtk_editable_get_text(GTK_EDITABLE(data->entry));
        char *prefix = NULL;
        FileCommand cmd;
        const char *after_prefix = detect_file_prefix(text, &prefix, &cmd,
                                                       data->config);

        if (after_prefix) {
            /* File mode: Enter confirms action */
            cancel_file_timeout(data);

            if (data->current_file_results) {
                FileEntry *first = (FileEntry *)data->current_file_results->data;
                char *saved_path = g_strdup(first->full_path);
                char *saved_name = g_strdup(first->name);
                gboolean saved_is_dir = first->is_dir;
                gboolean path_mode = (after_prefix[0] == '/');

                if (path_mode && saved_is_dir) {
                    /* In path mode, Enter on a directory = navigate into it */
                    const char *last_slash = strrchr(after_prefix, '/');
                    size_t stable_len = strlen(prefix) +
                                        (size_t)(last_slash - after_prefix) + 1;
                    char *new_text = g_strdup_printf("%.*s%s/",
                                                     (int)stable_len, text,
                                                     saved_name);
                    data->suppress_entry_change = TRUE;
                    gtk_editable_set_text(GTK_EDITABLE(data->entry), new_text);
                    gtk_editable_set_position(GTK_EDITABLE(data->entry), -1);
                    data->suppress_entry_change = FALSE;
                    g_free(new_text);

                    clear_file_results(data);
                    clear_listbox(GTK_LIST_BOX(data->listbox));
                    GList *results = file_nav_search(saved_path, "", 10);
                    data->current_file_results = results;
                    display_file_results(data);
                } else if (saved_is_dir) {
                    /* Simple mode directory: open file manager */
                    file_nav_open_file_manager(saved_path);
                    hide_window(data);
                } else {
                    /* File: open based on command */
                    if (cmd == FILE_CMD_OPEN) {
                        file_nav_open_default(saved_path);
                    } else {
                        char *parent = g_path_get_dirname(saved_path);
                        file_nav_open_file_manager(parent);
                        g_free(parent);
                    }
                    hide_window(data);
                }

                g_free(saved_path);
                g_free(saved_name);
            } else {
                /* No results but in path mode: open file manager at current dir */
                gboolean path_mode = (after_prefix[0] == '/');
                if (path_mode) {
                    const char *base_dir;
                    if (cmd == FILE_CMD_DEFAULT)
                        base_dir = config_get_default_dir(data->config);
                    else
                        base_dir = g_get_home_dir();

                    char *search_dir = NULL;
                    char *query = NULL;
                    gboolean pm = FALSE;
                    parse_file_query(after_prefix, base_dir,
                                     &search_dir, &query, &pm);
                    if (g_file_test(search_dir, G_FILE_TEST_IS_DIR)) {
                        file_nav_open_file_manager(search_dir);
                    }
                    g_free(search_dir);
                    g_free(query);
                    hide_window(data);
                }
            }

            g_free(prefix);
            return TRUE;
        }

        g_free(prefix);

        /* Normal app mode: Enter to launch first result */
        if (data->current_matches && g_list_length(data->current_matches) > 0) {
            AppEntry *app = (AppEntry *)data->current_matches->data;
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

    cancel_file_timeout(data);
    if (data->current_matches)
        g_list_free(data->current_matches);
    clear_file_results(data);
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

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "ThunderSearch");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);

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

    gtk_layer_init_for_window(GTK_WINDOW(window));
    gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
                                 GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);

    gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, 100);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_window_set_child(GTK_WINDOW(window), box);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Type to search applications...");
    gtk_box_append(GTK_BOX(box), entry);

    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_append(GTK_BOX(box), scrolled);

    listbox = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), listbox);

    data = g_new0(WindowData, 1);
    data->entry = entry;
    data->listbox = listbox;
    data->window = window;
    data->index = index;
    data->config = config;
    data->current_matches = NULL;
    data->is_visible = TRUE;
    data->current_file_results = NULL;
    data->suppress_entry_change = FALSE;
    data->file_auto_timeout = 0;

    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), data);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), data);

    key_controller = gtk_event_controller_key_new();
    g_signal_connect(key_controller, "key-pressed",
                     G_CALLBACK(on_key_pressed), data);
    gtk_widget_add_controller(window, key_controller);

    gtk_widget_grab_focus(entry);

    if (out_data) {
        *out_data = data;
    }

    return window;
}
