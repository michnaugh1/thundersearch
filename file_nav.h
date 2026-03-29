#ifndef FILE_NAV_H
#define FILE_NAV_H

#include <glib.h>

typedef enum {
    FILE_CMD_BROWSE,     /* /f  - open directory in file manager */
    FILE_CMD_OPEN,       /* /f/o - open file with default app */
    FILE_CMD_DEFAULT,    /* /fd  - browse from configured default dir */
} FileCommand;

typedef struct {
    char *name;          /* Entry name (basename) */
    char *full_path;     /* Full filesystem path */
    gboolean is_dir;     /* TRUE if directory, FALSE if file */
} FileEntry;

/* Search a directory for entries matching query (case-insensitive substring) */
GList *file_nav_search(const char *dir_path, const char *query, int max_results);

void file_nav_open_file_manager(const char *path);
void file_nav_open_default(const char *path);
void file_nav_open_with(const char *path, const char *app);
void file_entry_free(FileEntry *entry);

#endif /* FILE_NAV_H */
