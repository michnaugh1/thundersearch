#ifndef MATCHER_H
#define MATCHER_H

#include "app_index.h"
#include "config.h"
#include <glib.h>

GList *match_apps(AppIndex *index, Config *config, const char *query, int max_results);

#endif /* MATCHER_H */
