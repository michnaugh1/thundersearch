#include "calc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

/*
 * Recursive descent parser for math expressions.
 *
 * Grammar:
 *   expr    = term   (('+' | '-') term)*
 *   term    = power  (('*' | '/' | '%') power)*
 *   power   = unary  ('^' unary)*
 *   unary   = '-' unary | primary
 *   primary = number | '(' expr ')' | ident '(' expr ')' | ident
 */

typedef struct {
    const char *p;     /* current position in input */
    char *error;       /* first error encountered, or NULL */
    int depth;         /* recursion depth to prevent stack overflow */
} Parser;

#define MAX_RECURSION_DEPTH 100

static double parse_expr(Parser *ps);

static void
skip_ws(Parser *ps)
{
    while (*ps->p && isspace((unsigned char)*ps->p))
        ps->p++;
}

static double
parse_primary(Parser *ps)
{
    if (++ps->depth > MAX_RECURSION_DEPTH) {
        if (!ps->error) ps->error = g_strdup("expression too complex");
        return 0.0;
    }

    skip_ws(ps);

    if (ps->error)
        return 0.0;

    /* Parenthesised expression */
    if (*ps->p == '(') {
        ps->p++;
        double val = parse_expr(ps);
        skip_ws(ps);
        if (*ps->p == ')')
            ps->p++;
        else if (!ps->error)
            ps->error = g_strdup("missing ')'");
        ps->depth--;
        return val;
    }

    /* Named constant or function */
    if (isalpha((unsigned char)*ps->p)) {
        const char *start = ps->p;
        while (isalpha((unsigned char)*ps->p) || isdigit((unsigned char)*ps->p))
            ps->p++;
        size_t len = (size_t)(ps->p - start);
        char name[64];
        if (len >= sizeof(name)) {
            if (!ps->error) ps->error = g_strdup("identifier too long");
            ps->depth--;
            return 0.0;
        }
        memcpy(name, start, len);
        name[len] = '\0';

        skip_ws(ps);

        /* Function: name '(' expr ')' */
        if (*ps->p == '(') {
            ps->p++;
            double arg = parse_expr(ps);
            skip_ws(ps);
            if (*ps->p == ')')
                ps->p++;
            else if (!ps->error)
                ps->error = g_strdup("missing ')'");

            ps->depth--;
            if (strcmp(name, "sqrt")  == 0) return sqrt(arg);
            if (strcmp(name, "abs")   == 0) return fabs(arg);
            if (strcmp(name, "floor") == 0) return floor(arg);
            if (strcmp(name, "ceil")  == 0) return ceil(arg);
            if (strcmp(name, "round") == 0) return round(arg);
            if (strcmp(name, "sin")   == 0) return sin(arg);
            if (strcmp(name, "cos")   == 0) return cos(arg);
            if (strcmp(name, "tan")   == 0) return tan(arg);
            if (strcmp(name, "log")   == 0) return log10(arg);
            if (strcmp(name, "ln")    == 0) return log(arg);
            if (strcmp(name, "log2")  == 0) return log2(arg);
            if (strcmp(name, "exp")   == 0) return exp(arg);

            if (!ps->error)
                ps->error = g_strdup_printf("unknown function '%s'", name);
            return 0.0;
        }

        ps->depth--;
        /* Constant */
        if (strcmp(name, "pi")  == 0) return G_PI;
        if (strcmp(name, "e")   == 0) return G_E;
        if (strcmp(name, "tau") == 0) return 2.0 * G_PI;
        if (strcmp(name, "inf") == 0) return INFINITY;

        if (!ps->error)
            ps->error = g_strdup_printf("unknown name '%s'", name);
        return 0.0;
    }

    /* Number */
    if (isdigit((unsigned char)*ps->p) || *ps->p == '.') {
        char *end;
        double val = strtod(ps->p, &end);
        if (end == ps->p) {
            if (!ps->error)
                ps->error = g_strdup("expected number");
            ps->depth--;
            return 0.0;
        }
        ps->p = end;
        ps->depth--;
        return val;
    }

    if (!ps->error)
        ps->error = g_strdup_printf("unexpected character '%c'", *ps->p);
    ps->depth--;
    return 0.0;
}

static double
parse_unary(Parser *ps)
{
    if (++ps->depth > MAX_RECURSION_DEPTH) {
        if (!ps->error) ps->error = g_strdup("expression too complex");
        return 0.0;
    }

    skip_ws(ps);
    if (*ps->p == '-') {
        ps->p++;
        double val = -parse_unary(ps);
        ps->depth--;
        return val;
    }
    if (*ps->p == '+') {
        ps->p++;
        double val = parse_unary(ps);
        ps->depth--;
        return val;
    }
    ps->depth--;
    return parse_primary(ps);
}

static double
parse_power(Parser *ps)
{
    double val = parse_unary(ps);
    skip_ws(ps);
    if (*ps->p == '^') {
        ps->p++;
        val = pow(val, parse_power(ps));
    }
    return val;
}

static double
parse_term(Parser *ps)
{
    double val = parse_power(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->error) break;
        char op = *ps->p;
        if (op == '*' || op == '/' || op == '%') {
            ps->p++;
            double rhs = parse_power(ps);
            if (op == '*')      val *= rhs;
            else if (op == '/') {
                if (rhs == 0.0) { if (!ps->error) ps->error = g_strdup("division by zero"); break; }
                val /= rhs;
            } else              val = fmod(val, rhs);
        } else break;
    }
    return val;
}

static double
parse_expr(Parser *ps)
{
    double val = parse_term(ps);
    for (;;) {
        skip_ws(ps);
        if (ps->error) break;
        char op = *ps->p;
        if (op == '+' || op == '-') {
            ps->p++;
            double rhs = parse_term(ps);
            val = (op == '+') ? val + rhs : val - rhs;
        } else break;
    }
    return val;
}

gboolean
calc_evaluate(const char *expr, double *result, char **error_msg)
{
    if (!expr || !*expr) {
        if (error_msg) *error_msg = g_strdup("empty expression");
        return FALSE;
    }

    Parser ps = { .p = expr, .error = NULL };
    double val = parse_expr(&ps);
    skip_ws(&ps);

    if (ps.error) {
        if (error_msg) *error_msg = ps.error;
        else g_free(ps.error);
        return FALSE;
    }

    if (*ps.p != '\0') {
        if (error_msg) *error_msg = g_strdup_printf("unexpected '%c'", *ps.p);
        return FALSE;
    }

    if (!isfinite(val)) {
        if (error_msg) *error_msg = g_strdup("result is not finite");
        return FALSE;
    }

    *result = val;
    return TRUE;
}

char *
calc_format_result(double result)
{
    /* If the value is an integer (within floating-point tolerance), show it
     * without a decimal point.  Otherwise show up to 10 significant digits
     * and strip trailing zeros. */
    if (result == floor(result) && fabs(result) < 1e15) {
        return g_strdup_printf("%.0f", result);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%.10g", result);
    return g_strdup(buf);
}
