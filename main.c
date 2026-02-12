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

static void
on_activate(GtkApplication *app, gpointer user_data)
{
    AppData *app_data = (AppData *)user_data;

    /* Create window only once (daemon mode) */
    if (!global_window_data) {
        GtkWidget *window;
        window = create_launcher_window(app, app_data->index, app_data->config, &global_window_data);
        gtk_window_present(GTK_WINDOW(window));
    } else {
        /* Toggle visibility on subsequent activations */
        toggle_window(global_window_data);
    }
}

int
main(int argc, char *argv[])
{
    GtkApplication *app;
    AppIndex *index;
    Config *config;
    AppData app_data;
    int status;

    /* Initialize config */
    config = config_new();
    config_load(config);

    /* Initialize app index */
    index = app_index_new();
    app_index_load(index);

    g_print("Loaded %d applications\n", app_index_get_count(index));
    g_print("ThunderSearch running in daemon mode\n");
    g_print("Config: %s\n", config->config_path);
    g_print("History: %s\n", config->history_path);
    g_print("Bind a key in your compositor to run: thundersearch\n");

    /* Setup app data */
    app_data.index = index;
    app_data.config = config;

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
