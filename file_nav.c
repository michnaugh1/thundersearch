#include "file_nav.h"
#include <gio/gio.h>
#include <string.h>

void
file_entry_free(FileEntry *entry)
{
    if (!entry) return;
    g_free(entry->name);
    g_free(entry->full_path);
    g_free(entry);
}

/* Case-insensitive substring match */
static gboolean
file_matches(const char *query, const char *name)
{
    if (!query || query[0] == '\0')
        return TRUE;

    char *query_lower = g_ascii_strdown(query, -1);
    char *name_lower = g_ascii_strdown(name, -1);
    gboolean result = strstr(name_lower, query_lower) != NULL;

    g_free(query_lower);
    g_free(name_lower);

    return result;
}

/* Sort: directories first, then alphabetical within each group */
static gint
compare_file_entries(gconstpointer a, gconstpointer b)
{
    const FileEntry *fa = *(const FileEntry **)a;
    const FileEntry *fb = *(const FileEntry **)b;

    if (fa->is_dir && !fb->is_dir)
        return -1;
    if (!fa->is_dir && fb->is_dir)
        return 1;

    return g_ascii_strcasecmp(fa->name, fb->name);
}

GList *
file_nav_search(const char *dir_path, const char *query, int max_results)
{
    GDir *dir;
    GError *error = NULL;
    const char *entry_name;
    GPtrArray *results;
    GList *result_list = NULL;

    if (!dir_path)
        return NULL;

    dir = g_dir_open(dir_path, 0, &error);
    if (!dir) {
        if (error) g_error_free(error);
        return NULL;
    }

    results = g_ptr_array_new();

    while ((entry_name = g_dir_read_name(dir)) != NULL) {
        /* Skip hidden files */
        if (entry_name[0] == '.')
            continue;

        if (!file_matches(query, entry_name))
            continue;

        char *full_path = g_build_filename(dir_path, entry_name, NULL);
        gboolean is_dir = g_file_test(full_path, G_FILE_TEST_IS_DIR);

        FileEntry *entry = g_new0(FileEntry, 1);
        entry->name = g_strdup(entry_name);
        entry->full_path = full_path;
        entry->is_dir = is_dir;

        g_ptr_array_add(results, entry);
    }

    g_dir_close(dir);

    g_ptr_array_sort(results, compare_file_entries);

    int count = 0;
    for (guint i = 0; i < results->len; i++) {
        if (max_results > 0 && count >= max_results)
            break;
        result_list = g_list_append(result_list, results->pdata[i]);
        count++;
    }

    for (guint i = count; i < results->len; i++) {
        file_entry_free(results->pdata[i]);
    }

    g_ptr_array_free(results, TRUE);

    return result_list;
}

void
file_nav_open_file_manager(const char *path)
{
    GError *error = NULL;
    char *argv[] = { "xdg-open", (char *)path, NULL };

    g_spawn_async(NULL, argv, NULL,
                  G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, &error);

    if (error) {
        g_warning("Failed to open file manager for %s: %s", path, error->message);
        g_error_free(error);
    }
}

void
file_nav_open_default(const char *path)
{
    GError *error = NULL;
    char *argv[] = { "xdg-open", (char *)path, NULL };

    g_spawn_async(NULL, argv, NULL,
                  G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, &error);

    if (error) {
        g_warning("Failed to open %s: %s", path, error->message);
        g_error_free(error);
    }
}

void
file_nav_open_with(const char *path, const char *app)
{
    GError *error = NULL;
    char *argv[] = { (char *)app, (char *)path, NULL };

    g_spawn_async(NULL, argv, NULL,
                  G_SPAWN_SEARCH_PATH,
                  NULL, NULL, NULL, &error);

    if (error) {
        g_warning("Failed to open %s with %s: %s", path, app, error->message);
        g_error_free(error);
    }
}
