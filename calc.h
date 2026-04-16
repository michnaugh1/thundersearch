#ifndef CALC_H
#define CALC_H

#include <glib.h>

/* Evaluate a math expression string.
 * Returns TRUE on success, setting *result.
 * Returns FALSE on parse/eval error, setting *error_msg (caller must g_free). */
gboolean calc_evaluate(const char *expr, double *result, char **error_msg);

/* Format a result for display — strips trailing zeros from decimals. */
char *calc_format_result(double result);

#endif /* CALC_H */
