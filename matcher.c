#include "matcher.h"
#include <string.h>
#include <ctype.h>

/* Simple case-insensitive substring match */
static gboolean
app_matches(const char *query, const char *app_name)
{
    char *query_lower = g_ascii_strdown(query, -1);
    char *name_lower = g_ascii_strdown(app_name, -1);
    gboolean result = strstr(name_lower, query_lower) != NULL;
    
    g_free(query_lower);
    g_free(name_lower);
    
    return result;
}

/* Score a match - higher is better */
static int
score_match(Config *config, const char *query, const char *app_name)
{
    char *query_lower = g_ascii_strdown(query, -1);
    char *name_lower = g_ascii_strdown(app_name, -1);
    int score = 0;
    
    /* Exact match gets highest score */
    if (strcmp(query_lower, name_lower) == 0) {
        score = 10000;
    }
    /* Prefix match gets high score */
    else if (g_str_has_prefix(name_lower, query_lower)) {
        score = 5000;
    }
    /* Substring match gets lower score */
    else if (strstr(name_lower, query_lower) != NULL) {
        score = 1000;
    }
    
    /* Prefer shorter names (more specific) */
    score += (1000 - strlen(app_name));
    
    /* BOOST by usage count - frequently used apps rank higher */
    int usage_count = config_get_usage_count(config, app_name);
    score += usage_count * 100;  /* Each launch adds 100 points */
    
    g_free(query_lower);
    g_free(name_lower);
    
    return score;
}

/* Comparison function for sorting matches */
static gint
compare_matches(gconstpointer a, gconstpointer b, gpointer user_data)
{
    struct {
        Config *config;
        const char *query;
    } *data = user_data;
    
    AppEntry *app_a = (AppEntry *)a;
    AppEntry *app_b = (AppEntry *)b;
    
    int score_a = score_match(data->config, data->query, app_a->name);
    int score_b = score_match(data->config, data->query, app_b->name);
    
    /* Sort descending by score */
    return score_b - score_a;
}

GList *
match_apps(AppIndex *index, Config *config, const char *query, int max_results)
{
    GList *matches = NULL;
    GList *l;
    const char *resolved_query;

    if (!index || !query || strlen(query) == 0) {
        return NULL;
    }
    
    /* Resolve nickname if it exists */
    resolved_query = config_resolve_nickname(config, query);

    /* Find all matching apps */
    for (l = index->app_list; l != NULL; l = l->next) {
        AppEntry *entry = (AppEntry *)l->data;
        
        /* Match against both original query and resolved query */
        if (app_matches(query, entry->name) || 
            (resolved_query != query && app_matches(resolved_query, entry->name))) {
            matches = g_list_prepend(matches, entry);
        }
    }

    /* Sort by score (with usage counts) */
    struct {
        Config *config;
        const char *query;
    } sort_data = { config, resolved_query };
    
    matches = g_list_sort_with_data(matches, compare_matches, &sort_data);

    /* Limit results */
    if (max_results > 0) {
        GList *tail = g_list_nth(matches, max_results - 1);
        if (tail) {
            g_list_free(tail->next);
            tail->next = NULL;
        }
    }

    return matches;
}
