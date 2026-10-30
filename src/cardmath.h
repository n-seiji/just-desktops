#pragma once
// Motion and layout math of the switchers: the square cards' easing, where a card sits, which
// positions need a window, how a screenshot fits its box, and the list highlight's slide.
#include <ApplicationServices/ApplicationServices.h>

// Progress of cubic-bezier(0.22, 0.9, 0.25, 1) at time fraction k in 0..1: most of the travel happens
// in the first quarter of the time.
double cardmath_ease(double k);

typedef struct {
    double x, a; // offset from the stage centre in points, alpha
} cardmath_pose_t;
// A card p slots from the centre slides dx points per slot and fades out over 1 / fade slots.
cardmath_pose_t cardmath_pose(double p, double dx, double fade);

// The strip positions that must have a card window with sel_v selected, into out (room for 2): the
// selected one, and the one that just left after a step of last_dir (0 for none) when n > 1.
int cardmath_needed(long sel_v, int last_dir, int n, long *out);

// img scaled to fit box, centred in it, keeping its aspect ratio.
CGRect cardmath_fit(CGSize img, CGRect box);

// The list switcher's highlight, ease-out cubic: from at k = 0 to `to` at k >= 1.
double cardmath_slide(double from, double to, double k);
