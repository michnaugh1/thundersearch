#ifndef WIN_NAV_H
#define WIN_NAV_H

#include <glib.h>

typedef struct {
    guint id;            /* niri window ID */
    char *title;         /* Window title */
    char *app_id;        /* Application ID (e.g., "firefox") */
    guint workspace_id;  /* Workspace the window is on */
    gboolean focused;    /* TRUE if this is the currently focused window */
} WinEntry;

/* Get list of all open windows from niri. Returns GList of WinEntry*. */
GList *win_nav_list_windows(void);

/* Filter windows by query (case-insensitive substring match on title + app_id) */
GList *win_nav_search(const char *query, int max_results);

/* Focus a window by its niri ID (switches workspace automatically) */
void win_nav_focus(guint window_id);

void win_entry_free(WinEntry *entry);

#endif /* WIN_NAV_H */
