#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <string.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#endif
#include "window.h"
#include "matcher.h"
#include "launcher.h"
#include "file_nav.h"
#include "animation.h"
#include "calc.h"

/* Forward declarations */
static void cancel_ai_query(WindowData *data);

/* Return the monitor the cursor is currently on, falling back to monitor 0.
 * Caller must g_object_unref() the result. */
static GdkMonitor *
get_monitor_at_cursor(GdkDisplay *display)
{
    GListModel *monitors = gdk_display_get_monitors(display);
    guint n = g_list_model_get_n_items(monitors);

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(display)) {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        Display *xdisplay = gdk_x11_display_get_xdisplay(display);
        G_GNUC_END_IGNORE_DEPRECATIONS

        Window root_ret, child_ret;
        int root_x, root_y, win_x, win_y;
        unsigned int mask;
        XQueryPointer(xdisplay, DefaultRootWindow(xdisplay),
                      &root_ret, &child_ret,
                      &root_x, &root_y, &win_x, &win_y, &mask);

        for (guint i = 0; i < n; i++) {
            GdkMonitor *mon = g_list_model_get_item(monitors, i);
            GdkRectangle geom;
            gdk_monitor_get_geometry(mon, &geom);
            /* GDK geometry is in logical pixels; XQueryPointer returns the
             * same logical coords under XWayland, so compare directly. */
            if (root_x >= geom.x && root_x < geom.x + geom.width &&
                root_y >= geom.y && root_y < geom.y + geom.height)
                return mon;   /* caller owns the ref */
            g_object_unref(mon);
        }
    }
#endif

    /* Fallback: first monitor */
    return g_list_model_get_item(monitors, 0);
}

static void
center_window_x11(WindowData *data)
{
    GtkWidget *window = data->window;
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(display))
        return;

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    if (!surface)
        return;

    GdkMonitor *monitor = get_monitor_at_cursor(display);
    if (!monitor)
        return;

    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    gint scale = gdk_monitor_get_scale_factor(monitor);
    g_object_unref(monitor);

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *xdisplay = gdk_x11_display_get_xdisplay(display);
    Window xwindow = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS

    /* Geometry is in logical pixels; XMoveWindow needs physical pixels */
    int win_width = data->config->win_width;
    int x = (geometry.x + (geometry.width - win_width) / 2) * scale;
    int y = (geometry.y + data->config->top_offset) * scale;

    XMoveWindow(xdisplay, xwindow, x, y);
    XFlush(xdisplay);
#else
    (void)window;
#endif
}

/* Animation tick callback for show/hide */
static gboolean
window_anim_tick(gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;

    if (!animation_tick(data->show_anim)) {
        /* Animation complete */
        data->anim_tick_id = 0;

        if (data->hiding) {
            /* Hide complete - actually hide the window */
            data->hiding = FALSE;
            gtk_widget_set_visible(data->window, FALSE);
        }

        return G_SOURCE_REMOVE;
    }

    /* Apply animated values */
    gdouble t = animation_value(data->show_anim);

    /* Opacity: 0 -> 1 (or 1 -> 0 for hide) */
    gtk_widget_set_opacity(data->main_container, t);

    /* Slide up: start 30px below, end at 0 */
    gint y_offset = (gint)((1.0 - t) * 30.0);
    gtk_widget_set_margin_top(data->main_container, data->base_margin_top + y_offset);

    return G_SOURCE_CONTINUE;
}

static void
clear_listbox(GtkListBox *listbox)
{
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(listbox))) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(listbox), child);
    }
}

static void
select_first_row(GtkListBox *listbox)
{
    GtkListBoxRow *row = gtk_list_box_get_row_at_index(listbox, 0);
    if (row)
        gtk_list_box_select_row(listbox, row);
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
    if (!data->is_visible || data->hiding) return;

    data->is_visible = FALSE;
    data->hiding = TRUE;

#ifdef GDK_WINDOWING_X11
    {
        GdkDisplay *display = gdk_display_get_default();
        if (GDK_IS_X11_DISPLAY(display)) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            Display *xdisplay = gdk_x11_display_get_xdisplay(display);
            G_GNUC_END_IGNORE_DEPRECATIONS
            XUngrabKeyboard(xdisplay, CurrentTime);
            XFlush(xdisplay);
        }
    }
#endif

    cancel_file_timeout(data);
    cancel_ai_query(data);

    data->suppress_entry_change = TRUE;
    gtk_editable_set_text(GTK_EDITABLE(data->entry), "");
    data->suppress_entry_change = FALSE;

    if (data->current_matches) {
        g_list_free(data->current_matches);
        data->current_matches = NULL;
    }
    clear_file_results(data);
    clear_listbox(GTK_LIST_BOX(data->listbox));

    /* Hide results revealer */
    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);

    /* Start hide animation */
    if (data->anim_tick_id > 0) {
        g_source_remove(data->anim_tick_id);
    }
    animation_start(data->show_anim, 150, TRUE);  /* Reverse animation */
    data->anim_tick_id = g_timeout_add(16, window_anim_tick, data);
}

static void
show_window(WindowData *data)
{
    if (data->is_visible) return;

    data->is_visible = TRUE;
    data->hiding = FALSE;

    /* Set initial animation state */
    gtk_widget_set_opacity(data->main_container, 0.0);
    gtk_widget_set_margin_top(data->main_container, data->base_margin_top + 30);

    /* With override_redirect the WM cannot touch us — position before map */
    center_window_x11(data);

    gtk_widget_set_visible(data->window, TRUE);
    gtk_widget_grab_focus(data->entry);

#ifdef GDK_WINDOWING_X11
    /* Flush so XMapWindow reaches the server, then grab keyboard */
    GdkDisplay *display = gdk_display_get_default();
    if (GDK_IS_X11_DISPLAY(display)) {
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(data->window));
        if (surface) {
            gdk_display_flush(display);
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            Display *xdisplay = gdk_x11_display_get_xdisplay(display);
            Window xid = gdk_x11_surface_get_xid(surface);
            G_GNUC_END_IGNORE_DEPRECATIONS
            XSetInputFocus(xdisplay, xid, RevertToPointerRoot, CurrentTime);
            XGrabKeyboard(xdisplay, xid, True,
                          GrabModeAsync, GrabModeAsync, CurrentTime);
            XFlush(xdisplay);
        }
    }
#endif

    if (data->anim_tick_id > 0)
        g_source_remove(data->anim_tick_id);
    animation_start(data->show_anim, 200, FALSE);
    data->anim_tick_id = g_timeout_add(16, window_anim_tick, data);
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

    if (!query || strlen(query) == 0) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    matches = match_apps(data->index, data->config, query, data->config->max_app_results);
    match_count = g_list_length(matches);
    data->current_matches = matches;

    if (match_count == 0) {
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    if (match_count == 1) {
        AppEntry *app = (AppEntry *)matches->data;
        config_increment_usage(data->config, app->name);
        launch_app(app);
        hide_window(data);
        return;
    }

    for (GList *l = matches; l != NULL; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;

        /* Create row with icon and text */
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(row_box, "result-row");

        /* App icon */
        GIcon *icon = g_app_info_get_icon(app->app_info);
        GtkWidget *icon_widget;
        if (icon) {
            icon_widget = gtk_image_new_from_gicon(icon);
        } else {
            icon_widget = gtk_image_new_from_icon_name("application-x-executable");
        }
        gtk_image_set_pixel_size(GTK_IMAGE(icon_widget), 32);
        gtk_box_append(GTK_BOX(row_box), icon_widget);

        /* Text container */
        GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

        /* App name */
        GtkWidget *title = gtk_label_new(app->name);
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_widget_add_css_class(title, "result-title");
        gtk_box_append(GTK_BOX(text_box), title);

        /* App executable as subtitle */
        GtkWidget *subtitle = gtk_label_new(app->exec);
        gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_add_css_class(subtitle, "result-subtitle");
        gtk_box_append(GTK_BOX(text_box), subtitle);

        gtk_box_append(GTK_BOX(row_box), text_box);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
        gtk_list_box_append(GTK_LIST_BOX(data->listbox), row);
    }

    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), TRUE);
    select_first_row(GTK_LIST_BOX(data->listbox));
}

/* --- File navigation --- */

/* Open a file using configured opener, or fall back to xdg-open */
static void
open_file_with_config(WindowData *data, const char *path)
{
    const char *app = config_get_opener(data->config, path);
    if (app)
        file_nav_open_with(path, app);
    else
        file_nav_open_default(path);
}

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

        /* Create row with icon and text */
        GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_add_css_class(row_box, "result-row");

        /* File/folder icon */
        GtkWidget *icon_widget;
        if (entry->is_dir) {
            icon_widget = gtk_image_new_from_icon_name("folder");
        } else {
            icon_widget = gtk_image_new_from_icon_name("text-x-generic");
        }
        gtk_image_set_pixel_size(GTK_IMAGE(icon_widget), 32);
        gtk_box_append(GTK_BOX(row_box), icon_widget);

        /* Text container */
        GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

        /* File name */
        GtkWidget *title = gtk_label_new(entry->name);
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_widget_add_css_class(title, "result-title");
        gtk_box_append(GTK_BOX(text_box), title);

        /* Path as subtitle */
        char *parent = g_path_get_dirname(entry->full_path);
        GtkWidget *subtitle = gtk_label_new(parent);
        g_free(parent);
        gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(subtitle), PANGO_ELLIPSIZE_START);
        gtk_widget_add_css_class(subtitle, "result-subtitle");
        gtk_box_append(GTK_BOX(text_box), subtitle);

        gtk_box_append(GTK_BOX(row_box), text_box);

        GtkWidget *row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
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

            GList *results = file_nav_search(saved_path, "", data->config->max_file_results);
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
                open_file_with_config(data, saved_path);
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
                open_file_with_config(data, saved_path);
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
        results = file_nav_search(search_dir, query, data->config->max_file_results);
    }

    data->current_file_results = results;
    int result_count = g_list_length(results);

    /* Schedule debounced auto-fill if exactly one result AND there's a query
     * (don't auto-fill when just browsing with empty query) */
    if (result_count == 1 && query && query[0] != '\0') {
        data->file_auto_timeout = g_timeout_add(200, file_auto_action_cb, data);
    }

    /* Always display results */
    display_file_results(data);

    /* Show/hide revealer based on results */
    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), result_count > 0);
    if (result_count > 0)
        select_first_row(GTK_LIST_BOX(data->listbox));

    g_free(search_dir);
    g_free(query);
}


/* --- Claude integration helpers --- */

static char *
find_claude_path(void)
{
    char *path = g_find_program_in_path("claude");
    if (path) return path;

    /* Fallback: ~/.local/bin/claude */
    char *local = g_build_filename(g_get_home_dir(), ".local", "bin", "claude", NULL);
    if (g_file_test(local, G_FILE_TEST_IS_EXECUTABLE))
        return local;
    g_free(local);
    return NULL;
}

static char *
find_terminal_path(WindowData *data)
{
    /* Use config override first */
    if (data->config->terminal_cmd && *data->config->terminal_cmd)
        return g_find_program_in_path(data->config->terminal_cmd);

    /* Try $TERMINAL env var, then standard fallbacks */
    const char *env_term = g_getenv("TERMINAL");
    if (env_term) {
        char *path = g_find_program_in_path(env_term);
        if (path) return path;
    }
    char *path = g_find_program_in_path("x-terminal-emulator");
    if (path) return path;
    return g_find_program_in_path("sensible-terminal");
}

/* Strip ANSI escape sequences from text */
static char *
strip_ansi(const char *text)
{
    GString *out = g_string_sized_new(strlen(text));
    const char *p = text;
    while (*p) {
        if (*p == '\x1b' && *(p + 1) == '[') {
            p += 2;
            while (*p && (*p == ';' || (*p >= '0' && *p <= '9')))
                p++;
            if (*p) p++;  /* skip final letter */
        } else {
            g_string_append_c(out, *p++);
        }
    }
    return g_string_free(out, FALSE);
}

/* --- cc mode: open Claude Code in a directory --- */

/* Expand ~ and return an absolute path. Caller must g_free(). */
static char *
expand_path(const char *path)
{
    if (path[0] == '~')
        return g_build_filename(g_get_home_dir(), path + 1, NULL);
    return g_strdup(path);
}

static void
show_cc_hint(WindowData *data, const char *dir_input)
{
    clear_listbox(GTK_LIST_BOX(data->listbox));

    if (!dir_input || *dir_input == '\0') {
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    char *resolved = expand_path(dir_input);
    gboolean exists = g_file_test(resolved, G_FILE_TEST_IS_DIR);

    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(row_box, "result-row");

    GtkWidget *icon = gtk_image_new_from_icon_name(
        exists ? "utilities-terminal" : "dialog-warning");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(row_box), icon);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

    GtkWidget *label = gtk_label_new(resolved);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_START);
    gtk_widget_add_css_class(label, "result-title");
    gtk_box_append(GTK_BOX(text_box), label);

    GtkWidget *hint = gtk_label_new(
        exists ? "Enter to open Claude Code here" : "Directory not found");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_add_css_class(hint, "result-subtitle");
    gtk_box_append(GTK_BOX(text_box), hint);

    gtk_box_append(GTK_BOX(row_box), text_box);

    GtkWidget *list_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), row_box);
    gtk_list_box_append(GTK_LIST_BOX(data->listbox), list_row);
    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), TRUE);
    if (exists)
        gtk_list_box_select_row(GTK_LIST_BOX(data->listbox), GTK_LIST_BOX_ROW(list_row));

    g_free(resolved);
}

static void
launch_claude_session(WindowData *data, const char *dir_input)
{
    char *dir = expand_path(dir_input);

    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
        g_warning("thundersearch: not a directory: %s", dir);
        g_free(dir);
        return;
    }

    char *claude_path = find_claude_path();
    if (!claude_path) {
        g_warning("thundersearch: claude not found");
        g_free(dir);
        return;
    }

    /* Build a clean environment without GDK_BACKEND so Wayland terminals work */
    char **envp = g_get_environ();
    envp = g_environ_unsetenv(envp, "GDK_BACKEND");

    GError *error = NULL;
    gboolean spawned = FALSE;

    char *xdg_term = g_find_program_in_path("xdg-terminal-exec");
    if (xdg_term) {
        char *dir_flag = g_strdup_printf("--dir=%s", dir);
        const char *argv[] = { xdg_term, dir_flag, "--", claude_path, NULL };
        spawned = g_spawn_async(NULL, (char **)argv, envp,
                                G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
        g_free(dir_flag);
        g_free(xdg_term);
    }

    if (!spawned) {
        g_clear_error(&error);
        char *term_path = find_terminal_path(data);
        if (term_path) {
            char *quoted_dir = g_shell_quote(dir);
            char *shell_cmd = g_strdup_printf("cd %s && %s", quoted_dir, claude_path);
            g_free(quoted_dir);
            const char *argv[] = { term_path, "-e", "bash", "-c", shell_cmd, NULL };
            spawned = g_spawn_async(NULL, (char **)argv, envp,
                                    G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
            g_free(term_path);
            g_free(shell_cmd);
        }
    }

    if (error) {
        g_warning("thundersearch: failed to launch terminal: %s", error->message);
        g_error_free(error);
    }

    g_strfreev(envp);
    g_free(claude_path);
    g_free(dir);
}

/* --- ai mode: quick inline Claude query --- */

static void
show_ai_prompt_hint(WindowData *data, const char *query)
{
    clear_listbox(GTK_LIST_BOX(data->listbox));

    if (!query || *query == '\0') {
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(row_box, "result-row");

    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-question");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(row_box), icon);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

    GtkWidget *label = gtk_label_new(query);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(label, "result-title");
    gtk_box_append(GTK_BOX(text_box), label);

    GtkWidget *hint = gtk_label_new("Enter to ask Claude");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_add_css_class(hint, "result-subtitle");
    gtk_box_append(GTK_BOX(text_box), hint);

    gtk_box_append(GTK_BOX(row_box), text_box);

    GtkWidget *list_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), row_box);
    gtk_list_box_append(GTK_LIST_BOX(data->listbox), list_row);
    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), TRUE);
    gtk_list_box_select_row(GTK_LIST_BOX(data->listbox), GTK_LIST_BOX_ROW(list_row));
}

static void
show_ai_thinking(WindowData *data)
{
    clear_listbox(GTK_LIST_BOX(data->listbox));

    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(row_box, "result-row");

    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-question");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(row_box), icon);

    GtkWidget *label = gtk_label_new("Asking Claude…");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "result-subtitle");
    gtk_box_append(GTK_BOX(row_box), label);

    GtkWidget *list_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), row_box);
    gtk_list_box_append(GTK_LIST_BOX(data->listbox), list_row);
    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), TRUE);
}

static void
show_ai_response(WindowData *data, const char *response)
{
    clear_listbox(GTK_LIST_BOX(data->listbox));

    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(row_box, "result-row");

    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-information");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(row_box), icon);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(text_box, TRUE);

    GtkWidget *label = gtk_label_new(response);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 60);
    gtk_label_set_selectable(GTK_LABEL(label), FALSE);
    gtk_widget_add_css_class(label, "result-title");
    gtk_box_append(GTK_BOX(text_box), label);

    GtkWidget *hint = gtk_label_new("Enter to copy · Esc to close");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_add_css_class(hint, "result-subtitle");
    gtk_box_append(GTK_BOX(text_box), hint);

    gtk_box_append(GTK_BOX(row_box), text_box);

    GtkWidget *list_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), row_box);
    gtk_list_box_append(GTK_LIST_BOX(data->listbox), list_row);
    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), TRUE);
    gtk_list_box_select_row(GTK_LIST_BOX(data->listbox), GTK_LIST_BOX_ROW(list_row));
}

static void
cancel_ai_query(WindowData *data)
{
    if (data->ai_cancel) {
        g_cancellable_cancel(data->ai_cancel);
        g_object_unref(data->ai_cancel);
        data->ai_cancel = NULL;
    }
    data->ai_waiting = FALSE;
    g_free(data->ai_result);
    data->ai_result = NULL;
}

static void
on_ai_subprocess_done(GObject *source, GAsyncResult *async_result, gpointer user_data)
{
    WindowData *data = (WindowData *)user_data;
    GSubprocess *proc = G_SUBPROCESS(source);
    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    GError *error = NULL;

    gboolean ok = g_subprocess_communicate_utf8_finish(proc, async_result,
                                                        &stdout_buf, &stderr_buf,
                                                        &error);
    g_object_unref(proc);

    /* If cancelled or window moved on, discard */
    if (!ok || (data->ai_cancel && g_cancellable_is_cancelled(data->ai_cancel))) {
        g_clear_error(&error);
        g_free(stdout_buf);
        g_free(stderr_buf);
        data->ai_waiting = FALSE;
        return;
    }

    data->ai_waiting = FALSE;

    char *response;
    if (stdout_buf && *stdout_buf) {
        char *stripped = strip_ansi(stdout_buf);
        g_strstrip(stripped);

        /* Truncate at 600 chars with clean word break */
        if (strlen(stripped) > 600) {
            stripped[597] = '\0';
            char *last_space = strrchr(stripped, ' ');
            if (last_space && last_space > stripped + 400)
                *last_space = '\0';
            char *truncated = g_strdup_printf("%s…", stripped);
            g_free(stripped);
            response = truncated;
        } else {
            response = stripped;
        }
    } else {
        response = g_strdup(error ? error->message : "(no response)");
    }

    g_free(data->ai_result);
    data->ai_result = response;

    /* Only update display if still in ai mode */
    const char *text = gtk_editable_get_text(GTK_EDITABLE(data->entry));
    if (text[0] == 'a' && text[1] == 'i' && (text[2] == ' ' || text[2] == '\0'))
        show_ai_response(data, data->ai_result);

    g_free(stdout_buf);
    g_free(stderr_buf);
    g_clear_error(&error);
}

static void
run_ai_query(WindowData *data, const char *query)
{
    cancel_ai_query(data);
    data->ai_cancel = g_cancellable_new();
    data->ai_waiting = TRUE;

    show_ai_thinking(data);

    char *claude_path = find_claude_path();
    if (!claude_path) {
        data->ai_waiting = FALSE;
        data->ai_result = g_strdup("claude not found — install Claude Code first");
        show_ai_response(data, data->ai_result);
        return;
    }

    /* Non-interactive, plain text, no tools for speed */
    const char *argv[] = {
        claude_path, "-p", "--output-format", "text", "--tools", "", query, NULL
    };

    GError *error = NULL;
    GSubprocess *proc = g_subprocess_newv((const char * const *)argv,
                                          G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                          G_SUBPROCESS_FLAGS_STDERR_PIPE,
                                          &error);
    g_free(claude_path);

    if (!proc) {
        data->ai_waiting = FALSE;
        data->ai_result = g_strdup_printf("Failed to start claude: %s",
                                           error ? error->message : "unknown error");
        show_ai_response(data, data->ai_result);
        g_clear_error(&error);
        return;
    }

    g_subprocess_communicate_utf8_async(proc, NULL, data->ai_cancel,
                                         on_ai_subprocess_done, data);
}

/* --- Calculator mode --- */

static void
update_calc_result(WindowData *data, const char *expr)
{
    clear_listbox(GTK_LIST_BOX(data->listbox));
    clear_file_results(data);

    if (!expr || *expr == '\0') {
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    double result;
    char *error_msg = NULL;
    gboolean ok = calc_evaluate(expr, &result, &error_msg);

    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(row_box, "result-row");

    GtkWidget *icon = gtk_image_new_from_icon_name("accessories-calculator");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
    gtk_box_append(GTK_BOX(row_box), icon);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);

    if (ok) {
        char *formatted = calc_format_result(result);
        GtkWidget *label = gtk_label_new(formatted);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_add_css_class(label, "result-title");
        gtk_box_append(GTK_BOX(text_box), label);

        GtkWidget *hint = gtk_label_new("Enter to copy");
        gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
        gtk_widget_add_css_class(hint, "result-subtitle");
        gtk_box_append(GTK_BOX(text_box), hint);
        g_free(formatted);
    } else {
        GtkWidget *label = gtk_label_new(error_msg ? error_msg : "Invalid expression");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_add_css_class(label, "result-subtitle");
        gtk_box_append(GTK_BOX(text_box), label);
        g_free(error_msg);
    }

    gtk_box_append(GTK_BOX(row_box), text_box);

    GtkWidget *list_row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(list_row), row_box);
    gtk_list_box_append(GTK_LIST_BOX(data->listbox), list_row);

    gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), TRUE);
    gtk_list_box_select_row(GTK_LIST_BOX(data->listbox),
                            GTK_LIST_BOX_ROW(list_row));
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

    /* cc prefix: open Claude Code session in terminal */
    if (g_str_has_prefix(text, "cc ")) {
        cancel_file_timeout(data);
            cancel_ai_query(data);
        if (data->current_matches) {
            g_list_free(data->current_matches);
            data->current_matches = NULL;
        }
        show_cc_hint(data, text + 3);
        return;
    }
    if (g_strcmp0(text, "cc") == 0) {
        cancel_file_timeout(data);
            cancel_ai_query(data);
        if (data->current_matches) {
            g_list_free(data->current_matches);
            data->current_matches = NULL;
        }
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    /* ai prefix: quick inline Claude query */
    if (g_str_has_prefix(text, "ai ")) {
        cancel_file_timeout(data);
            if (data->current_matches) {
            g_list_free(data->current_matches);
            data->current_matches = NULL;
        }
        /* If result is already shown for the same query, don't clear it */
        if (!data->ai_waiting && !data->ai_result)
            show_ai_prompt_hint(data, text + 3);
        else if (!data->ai_waiting && data->ai_result)
            show_ai_response(data, data->ai_result);
        else
            show_ai_thinking(data);
        return;
    }
    if (g_strcmp0(text, "ai") == 0) {
        cancel_file_timeout(data);
            cancel_ai_query(data);
        if (data->current_matches) {
            g_list_free(data->current_matches);
            data->current_matches = NULL;
        }
        gtk_revealer_set_reveal_child(GTK_REVEALER(data->results_revealer), FALSE);
        return;
    }

    /* = prefix: calculator mode */
    if (*text == '=') {
        cancel_file_timeout(data);
            cancel_ai_query(data);
        if (data->current_matches) {
            g_list_free(data->current_matches);
            data->current_matches = NULL;
        }
        /* Skip '=' and any leading space */
        const char *expr = text + 1;
        while (*expr == ' ') expr++;
        update_calc_result(data, expr);
        return;
    }

    /* Normal app search */
    cancel_file_timeout(data);
    cancel_ai_query(data);
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

    /* Arrow keys: navigate the result list without leaving the entry */
    if (keyval == GDK_KEY_Down || keyval == GDK_KEY_Up) {
        GtkListBox *lb = GTK_LIST_BOX(data->listbox);
        GtkListBoxRow *cur = gtk_list_box_get_selected_row(lb);
        int next;

        if (!cur) {
            next = (keyval == GDK_KEY_Down) ? 0 : -1;
        } else {
            next = gtk_list_box_row_get_index(cur)
                   + (keyval == GDK_KEY_Down ? 1 : -1);
        }

        if (next < 0) {
            /* Up past the first row — deselect so Enter uses index 0 */
            gtk_list_box_unselect_all(lb);
        } else {
            GtkListBoxRow *row = gtk_list_box_get_row_at_index(lb, next);
            if (row) {
                gtk_list_box_select_row(lb, row);
                /* Scroll the selected row into view */
                GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(
                    GTK_SCROLLED_WINDOW(data->scrolled_window));
                int row_y = 0, row_h = 0;
                GtkWidget *w = GTK_WIDGET(row);
                graphene_point_t pt;
                if (gtk_widget_compute_point(w, GTK_WIDGET(data->scrolled_window),
                                             &GRAPHENE_POINT_INIT(0, 0), &pt))
                    row_y = (int)pt.y;
                row_h = gtk_widget_get_height(w);
                double cur   = gtk_adjustment_get_value(adj);
                double pgsz  = gtk_adjustment_get_page_size(adj);
                if (row_y < cur)
                    gtk_adjustment_set_value(adj, row_y);
                else if (row_y + row_h > cur + pgsz)
                    gtk_adjustment_set_value(adj, row_y + row_h - pgsz);
            }
        }
        /* Keep keyboard focus in the entry so typing still works */
        gtk_widget_grab_focus(data->entry);
        return TRUE;
    }

    /* Enter: perform action on selected row (fallback to first) */
    if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
        /* Determine which list index is active */
        GtkListBoxRow *sel = gtk_list_box_get_selected_row(
                                 GTK_LIST_BOX(data->listbox));
        int sel_idx = sel ? gtk_list_box_row_get_index(sel) : 0;

        const char *text = gtk_editable_get_text(GTK_EDITABLE(data->entry));
        char *prefix = NULL;
        FileCommand cmd;
        const char *after_prefix = detect_file_prefix(text, &prefix, &cmd,
                                                       data->config);

        if (after_prefix) {
            /* File mode: Enter confirms action on selected item */
            cancel_file_timeout(data);

            if (data->current_file_results) {
                GList *node = g_list_nth(data->current_file_results,
                                         (guint)sel_idx);
                if (!node) node = data->current_file_results;
                FileEntry *entry = (FileEntry *)node->data;
                char *saved_path = g_strdup(entry->full_path);
                char *saved_name = g_strdup(entry->name);
                gboolean saved_is_dir = entry->is_dir;
                gboolean path_mode = (after_prefix[0] == '/');

                if (path_mode && saved_is_dir) {
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
                    GList *results = file_nav_search(saved_path, "", data->config->max_file_results);
                    data->current_file_results = results;
                    display_file_results(data);
                } else if (saved_is_dir) {
                    file_nav_open_file_manager(saved_path);
                    hide_window(data);
                } else {
                    if (cmd == FILE_CMD_OPEN) {
                        open_file_with_config(data, saved_path);
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
                    if (g_file_test(search_dir, G_FILE_TEST_IS_DIR))
                        file_nav_open_file_manager(search_dir);
                    g_free(search_dir);
                    g_free(query);
                    hide_window(data);
                }
            }

            g_free(prefix);
            return TRUE;
        }

        g_free(prefix);

        /* cc mode: open Claude Code in a directory */
        if (g_str_has_prefix(text, "cc ")) {
            const char *dir_input = text + 3;
            if (*dir_input) {
                char *resolved = expand_path(dir_input);
                if (g_file_test(resolved, G_FILE_TEST_IS_DIR)) {
                    /* Copy before hide_window clears the entry and invalidates text */
                    char *dir_copy = g_strdup(dir_input);
                    hide_window(data);
                    launch_claude_session(data, dir_copy);
                    g_free(dir_copy);
                }
                g_free(resolved);
            }
            return TRUE;
        }

        /* ai mode: fire query on first Enter, copy on second */
        if (g_str_has_prefix(text, "ai ")) {
            const char *query = text + 3;
            if (!*query) return TRUE;

            if (data->ai_waiting) {
                /* Still running — ignore Enter */
                return TRUE;
            }
            if (data->ai_result) {
                /* Result ready — copy to clipboard and close */
                GdkClipboard *cb = gtk_widget_get_clipboard(data->entry);
                gdk_clipboard_set_text(cb, data->ai_result);
                hide_window(data);
            } else {
                /* First Enter — fire the query */
                run_ai_query(data, query);
            }
            return TRUE;
        }

        /* Calculator mode: copy result to clipboard */
        if (*text == '=') {
            const char *expr = text + 1;
            while (*expr == ' ') expr++;
            double result;
            if (calc_evaluate(expr, &result, NULL)) {
                char *formatted = calc_format_result(result);
                GdkClipboard *cb = gtk_widget_get_clipboard(data->entry);
                gdk_clipboard_set_text(cb, formatted);
                g_free(formatted);
                hide_window(data);
            }
            return TRUE;
        }

        /* Normal app mode */
        if (data->current_matches) {
            GList *node = g_list_nth(data->current_matches, (guint)sel_idx);
            if (!node) node = data->current_matches;
            AppEntry *app = (AppEntry *)node->data;
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
    if (data->anim_tick_id > 0)
        g_source_remove(data->anim_tick_id);
    if (data->current_matches)
        g_list_free(data->current_matches);
    clear_file_results(data);
    animation_free(data->show_anim);
    g_free(data);
}

static void
on_window_realize(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(display))
        return;

    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(widget));
    if (!surface)
        return;

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *xdisplay = gdk_x11_display_get_xdisplay(display);
    Window xid = gdk_x11_surface_get_xid(surface);
    G_GNUC_END_IGNORE_DEPRECATIONS

    /* Bypass the window manager — we control position and focus entirely */
    XSetWindowAttributes attrs = {0};
    attrs.override_redirect = True;
    XChangeWindowAttributes(xdisplay, xid, CWOverrideRedirect, &attrs);
    XFlush(xdisplay);
#endif
}

void
toggle_window(WindowData *data)
{
    /* Don't toggle if animation is in progress */
    if (data->hiding) {
        return;
    }

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
    GtkWidget *main_container;
    GtkWidget *search_pill;
    GtkWidget *entry;
    GtkWidget *results_revealer;
    GtkWidget *results_container;
    GtkWidget *scrolled;
    GtkWidget *listbox;
    GtkEventController *key_controller;
    GtkCssProvider *css_provider;
    WindowData *data;

    gboolean use_layer_shell = gtk_layer_is_supported();

    window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "ThunderSearch");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);

    gtk_window_set_default_size(GTK_WINDOW(window), config->win_width, -1);

    /* Load CSS */
    css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css_provider,
        /* Window and all containers - fully transparent */
        "window, window.background, box, scrolledwindow {"
        "  background: transparent;"
        "  background-color: transparent;"
        "  border: none;"
        "  box-shadow: none;"
        "  outline: none;"
        "}"
        /* Search pill container */
        ".search-pill {"
        "  background: rgba(58, 58, 60, 0.95);"
        "  border-radius: 26px;"
        "  padding: 6px 20px;"
        "}"
        /* Entry inside pill - remove all borders and outlines */
        ".search-pill entry, .search-pill entry:focus, .search-pill entry:hover {"
        "  background: transparent;"
        "  border: none;"
        "  border-width: 0;"
        "  box-shadow: none;"
        "  outline: none;"
        "  outline-width: 0;"
        "  font-size: 20px;"
        "  font-weight: 400;"
        "  color: #ffffff;"
        "  min-height: 44px;"
        "  caret-color: #007AFF;"
        "}"
        "entry > text {"
        "  background: transparent;"
        "  border: none;"
        "  box-shadow: none;"
        "  outline: none;"
        "}"
        /* Results container */
        ".results-container {"
        "  background: rgba(40, 40, 42, 0.98);"
        "  border-radius: 14px;"
        "  padding: 8px;"
        "  margin-top: 8px;"
        "}"
        /* Listbox */
        ".results-container listbox {"
        "  background: transparent;"
        "}"
        /* Result rows */
        ".results-container row {"
        "  padding: 8px 12px;"
        "  border-radius: 10px;"
        "  background: transparent;"
        "  margin: 2px 0;"
        "}"
        ".results-container row:hover {"
        "  background: rgba(255, 255, 255, 0.08);"
        "}"
        ".results-container row:selected {"
        "  background: rgba(0, 122, 255, 0.4);"
        "}"
        /* Result row box layout */
        ".result-row {"
        "  padding: 0;"
        "}"
        ".result-row image {"
        "  margin-right: 12px;"
        "}"
        /* Result text */
        ".result-title {"
        "  font-size: 15px;"
        "  font-weight: 500;"
        "  color: #ffffff;"
        "}"
        ".result-subtitle {"
        "  font-size: 12px;"
        "  color: rgba(255, 255, 255, 0.55);"
        "}");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(css_provider);

    /* Layer shell setup (Wayland + wlr-layer-shell only) */
    if (use_layer_shell) {
        gtk_layer_init_for_window(GTK_WINDOW(window));
        gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(window),
                                     GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
        gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_TOP, config->top_offset);
    }

    /* Main container */
    main_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(main_container, 16);
    gtk_widget_set_margin_end(main_container, 16);
    gtk_widget_set_margin_top(main_container, 16);
    gtk_widget_set_margin_bottom(main_container, 16);

    gtk_window_set_child(GTK_WINDOW(window), main_container);

    /* Search pill */
    search_pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(search_pill, "search-pill");
    gtk_box_append(GTK_BOX(main_container), search_pill);

    /* Entry inside pill */
    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Search apps, files, windows...");
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(search_pill), entry);

    /* Results revealer (hidden by default) */
    results_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(results_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(GTK_REVEALER(results_revealer), 150);
    gtk_revealer_set_reveal_child(GTK_REVEALER(results_revealer), FALSE);
    gtk_box_append(GTK_BOX(main_container), results_revealer);

    /* Results container inside revealer */
    results_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(results_container, "results-container");
    gtk_revealer_set_child(GTK_REVEALER(results_revealer), results_container);

    /* Scrolled window for results */
    scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                    GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scrolled), 360);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scrolled), TRUE);
    gtk_box_append(GTK_BOX(results_container), scrolled);

    /* Listbox */
    listbox = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), listbox);

    /* Setup WindowData */
    data = g_new0(WindowData, 1);
    data->entry = entry;
    data->listbox = listbox;
    data->window = window;
    data->main_container = main_container;
    data->search_pill = search_pill;
    data->results_revealer = results_revealer;
    data->results_container = results_container;
    data->scrolled_window = scrolled;
    data->index = index;
    data->config = config;
    data->current_matches = NULL;
    data->is_visible = FALSE;   /* Start hidden; first toggle shows it */
    data->current_file_results = NULL;
    data->suppress_entry_change = FALSE;
    data->file_auto_timeout = 0;
    data->show_anim = animation_new();
    data->anim_tick_id = 0;
    data->hiding = FALSE;
    data->base_margin_top = 16;

    g_signal_connect(entry, "changed", G_CALLBACK(on_entry_changed), data);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), data);
    g_signal_connect(window, "realize", G_CALLBACK(on_window_realize), NULL);

    key_controller = gtk_event_controller_key_new();
    /* CAPTURE phase: intercept keys before GtkEntry can consume them.
     * We return TRUE for Escape/Enter/arrows, FALSE for everything else
     * so normal typing still reaches the entry. */
    gtk_event_controller_set_propagation_phase(
        GTK_EVENT_CONTROLLER(key_controller), GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller, "key-pressed",
                     G_CALLBACK(on_key_pressed), data);
    gtk_widget_add_controller(window, key_controller);

    /* Realize now (creates the X surface) so override_redirect is set
     * before the window is ever mapped */
    gtk_widget_realize(window);

    if (out_data) {
        *out_data = data;
    }

    return window;
}
