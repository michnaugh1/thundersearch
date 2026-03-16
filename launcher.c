#include "launcher.h"
#include <glib.h>

gboolean
launch_app(AppEntry *entry)
{
    GError *error = NULL;
    gboolean result;

    if (!entry || !entry->app_info) {
        g_warning("Invalid app entry");
        return FALSE;
    }

    /* Launch the application asynchronously */
    result = g_app_info_launch(entry->app_info, NULL, NULL, &error);

    if (!result) {
        g_warning("Failed to launch %s: %s", 
                  entry->name, 
                  error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    g_print("Successfully launched: %s\n", entry->name);
    return TRUE;
}#include "launcher.h"
#include <glib.h>

gboolean
launch_app(AppEntry *entry)
{
    GError *error = NULL;
    gboolean result;

    if (!entry || !entry->app_info) {
        g_warning("Invalid app entry");
        return FALSE;
    }

    /* Launch the application asynchronously */
    result = g_app_info_launch(entry->app_info, NULL, NULL, &error);

    if (!result) {
        g_warning("Failed to launch %s: %s", 
                  entry->name, 
                  error ? error->message : "Unknown error");
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    g_print("Successfully launched: %s\n", entry->name);
    return TRUE;
}
