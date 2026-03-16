#include "win_nav.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void
win_entry_free(WinEntry *entry)
{
    if (!entry) return;
    g_free(entry->title);
    g_free(entry->app_id);
    g_free(entry);
}

/*
 * Parse the output of `niri msg windows` into a list of WinEntry.
 *
 * Format:
 *   Window ID 38: (focused)
 *     Title: "democratic_loader.c - Visual Studio Code"
 *     App ID: "code"
 *     ...
 *     Workspace ID: 8
 *     ...
 */
static GList *
parse_niri_windows(const char *output)
{
    GList *list = NULL;
    WinEntry *current = NULL;
    char **lines = g_strsplit(output, "\n", -1);

    for (int i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];

        /* New window block */
        if (g_str_has_prefix(line, "Window ID ")) {
            if (current)
                list = g_list_append(list, current);

            current = g_new0(WinEntry, 1);
            /* Parse: "Window ID 38:" or "Window ID 38: (focused)" */
            current->id = (guint)atoi(line + 10);
            current->focused = (strstr(line, "(focused)") != NULL);
        }

        if (!current)
            continue;

        /* Title line */
        if (g_str_has_prefix(line, "  Title: \"")) {
            const char *start = line + 10;  /* skip '  Title: "' */
            /* Find closing quote */
            const char *end = strrchr(start, '"');
            if (end && end > start)
                current->title = g_strndup(start, end - start);
            else
                current->title = g_strdup(start);
        }

        /* App ID line */
        if (g_str_has_prefix(line, "  App ID: \"")) {
            const char *start = line + 11;  /* skip '  App ID: "' */
            const char *end = strrchr(start, '"');
            if (end && end > start)
                current->app_id = g_strndup(start, end - start);
            else
                current->app_id = g_strdup(start);
        }

        /* Workspace ID line */
        if (g_str_has_prefix(line, "  Workspace ID: ")) {
            current->workspace_id = (guint)atoi(line + 16);
        }
    }

    /* Don't forget the last entry */
    if (current)
        list = g_list_append(list, current);

    g_strfreev(lines);
    return list;
}

GList *
win_nav_list_windows(void)
{
    char *stdout_buf = NULL;
    char *stderr_buf = NULL;
    int exit_status = 0;
    GError *error = NULL;

    char *argv[] = { "niri", "msg", "windows", NULL };

    gboolean ok = g_spawn_sync(NULL, argv, NULL,
                               G_SPAWN_SEARCH_PATH,
                               NULL, NULL,
                               &stdout_buf, &stderr_buf,
                               &exit_status, &error);

    if (!ok || exit_status != 0) {
        if (error) {
            g_warning("Failed to run niri msg windows: %s", error->message);
            g_error_free(error);
        }
        g_free(stdout_buf);
        g_free(stderr_buf);
        return NULL;
    }

    g_free(stderr_buf);

    GList *windows = parse_niri_windows(stdout_buf);
    g_free(stdout_buf);

    return windows;
}

/* Case-insensitive substring match against title and app_id */
static gboolean
win_matches(const char *query, const WinEntry *entry)
{
    if (!query || query[0] == '\0')
        return TRUE;

    char *query_lower = g_ascii_strdown(query, -1);
    gboolean found = FALSE;

    if (entry->title) {
        char *title_lower = g_ascii_strdown(entry->title, -1);
        if (strstr(title_lower, query_lower))
            found = TRUE;
        g_free(title_lower);
    }

    if (!found && entry->app_id) {
        char *app_lower = g_ascii_strdown(entry->app_id, -1);
        if (strstr(app_lower, query_lower))
            found = TRUE;
        g_free(app_lower);
    }

    g_free(query_lower);
    return found;
}

GList *
win_nav_search(const char *query, int max_results)
{
    GList *all = win_nav_list_windows();
    GList *filtered = NULL;
    int count = 0;

    for (GList *l = all; l != NULL; l = l->next) {
        WinEntry *entry = (WinEntry *)l->data;

        if (win_matches(query, entry)) {
            /* Move entry to filtered list */
            filtered = g_list_append(filtered, entry);
            count++;
            if (max_results > 0 && count >= max_results)
                break;
        }
    }

    /* Free entries that weren't kept */
    for (GList *l = all; l != NULL; l = l->next) {
        WinEntry *entry = (WinEntry *)l->data;
        /* Check if this entry is in our filtered list */
        if (!g_list_find(filtered, entry))
            win_entry_free(entry);
    }

    g_list_free(all);
    return filtered;
}

void
win_nav_focus(guint window_id)
{
    GError *error = NULL;
    char id_str[32];
    g_snprintf(id_str, sizeof(id_str), "%u", window_id);

    char *argv[] = { "niri", "msg", "action", "focus-window", "--id", id_str, NULL };

    g_spawn_async(NULL, argv, NULL,
                  G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, &error);

    if (error) {
        g_warning("Failed to focus window %u: %s", window_id, error->message);
        g_error_free(error);
    }
}
