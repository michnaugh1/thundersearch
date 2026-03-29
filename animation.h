#ifndef ANIMATION_H
#define ANIMATION_H

#include <glib.h>

typedef struct {
    gint64 start_time;      /* g_get_monotonic_time() when started */
    gint duration_ms;       /* Animation duration in milliseconds */
    gdouble progress;       /* 0.0 to 1.0 */
    gboolean active;        /* Is animation running? */
    gboolean reverse;       /* Playing in reverse (for hide)? */
} AnimationState;

/* Easing functions */
gdouble ease_out_cubic(gdouble t);
gdouble ease_in_cubic(gdouble t);
gdouble ease_out_quart(gdouble t);

/* Animation helpers */
AnimationState *animation_new(void);
void animation_free(AnimationState *anim);
void animation_start(AnimationState *anim, gint duration_ms, gboolean reverse);
gboolean animation_tick(AnimationState *anim);  /* Returns TRUE if still animating */
gdouble animation_value(AnimationState *anim);  /* Returns eased progress 0-1 */

/* Interpolation helpers */
gdouble lerp(gdouble a, gdouble b, gdouble t);
gint lerp_int(gint a, gint b, gdouble t);

#endif /* ANIMATION_H */
