#include "animation.h"
#include <math.h>

/* Easing functions - see easings.net for reference */

gdouble
ease_out_cubic(gdouble t)
{
    return 1.0 - pow(1.0 - t, 3);
}

gdouble
ease_in_cubic(gdouble t)
{
    return t * t * t;
}

gdouble
ease_out_quart(gdouble t)
{
    return 1.0 - pow(1.0 - t, 4);
}

/* Animation lifecycle */

AnimationState *
animation_new(void)
{
    AnimationState *anim = g_new0(AnimationState, 1);
    anim->active = FALSE;
    anim->progress = 0.0;
    anim->duration_ms = 200;
    anim->reverse = FALSE;
    return anim;
}

void
animation_free(AnimationState *anim)
{
    if (anim) {
        g_free(anim);
    }
}

void
animation_start(AnimationState *anim, gint duration_ms, gboolean reverse)
{
    anim->start_time = g_get_monotonic_time();
    anim->duration_ms = duration_ms;
    anim->progress = 0.0;
    anim->active = TRUE;
    anim->reverse = reverse;
}

gboolean
animation_tick(AnimationState *anim)
{
    if (!anim->active) {
        return FALSE;
    }

    gint64 now = g_get_monotonic_time();
    gint64 elapsed_us = now - anim->start_time;
    gdouble elapsed_ms = (gdouble)elapsed_us / 1000.0;

    anim->progress = elapsed_ms / (gdouble)anim->duration_ms;

    if (anim->progress >= 1.0) {
        anim->progress = 1.0;
        anim->active = FALSE;
        return FALSE;
    }

    return TRUE;
}

gdouble
animation_value(AnimationState *anim)
{
    gdouble t = CLAMP(anim->progress, 0.0, 1.0);
    gdouble eased = ease_out_cubic(t);

    if (anim->reverse) {
        return 1.0 - eased;
    }

    return eased;
}

/* Interpolation helpers */

gdouble
lerp(gdouble a, gdouble b, gdouble t)
{
    return a + (b - a) * t;
}

gint
lerp_int(gint a, gint b, gdouble t)
{
    return (gint)(a + (b - a) * t);
}
