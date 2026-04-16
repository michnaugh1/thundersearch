#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include "window.h"
#include "app_index.h"
#include "config.h"

static WindowData *global_window_data = NULL;

typedef struct {
    AppIndex *index;
    Config *config;
} AppData;

/* Callback when async app loading completes */
static void
on_apps_loaded(AppIndex *index, gpointer user_data)
{
    (void)user_data;
    g_print("Loaded %d applications\n", app_index_get_count(index));
}

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    AppData *app_data = (AppData *)user_data;

    /* Create window only once (daemon mode) */
    if (!global_window_data) {
        create_launcher_window(app, app_data->index, app_data->config, &global_window_data);

        /* Start async app loading if not already done */
        if (!app_index_is_ready(app_data->index)) {
            app_index_load_async(app_data->index, on_apps_loaded, app_data);
        }
    }

    /* Toggle on every activation — first press shows, second hides, etc. */
    toggle_window(global_window_data);
}

int
main(int argc, char *argv[])
{
    GtkApplication *app;
    AppIndex *index;
    Config *config;
    AppData app_data;
    int status;

    /* Initialize config (fast, OK to be synchronous) */
    config = config_new();
    config_load(config);

    /* Initialize app index (empty - will load async on first window show) */
    index = app_index_new();
    /* Don't call app_index_load() here - let it load async */

    g_print("ThunderSearch running in daemon mode\n");
    g_print("Config: %s\n", config->config_path);
    g_print("History: %s\n", config->history_path);
    g_print("Bind a key in your compositor to run: thundersearch\n");

    /* Setup app data */
    app_data.index = index;
    app_data.config = config;

    /* Force X11 backend for reliable window positioning.
     * On Wayland sessions this runs under XWayland, giving us
     * override_redirect support and exact position control. */
    g_setenv("GDK_BACKEND", "x11", FALSE);

    /* Create GTK application */
    app = gtk_application_new("com.thundersearch.launcher",
                               G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &app_data);

    status = g_application_run(G_APPLICATION(app), argc, argv);

    /* Cleanup */
    config_free(config);
    app_index_free(index);
    g_object_unref(app);

    return status;
}
