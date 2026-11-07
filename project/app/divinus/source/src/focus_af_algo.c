#include "focus_af_algo.h"

void af_tracker_reset(af_tracker *tracker, unsigned int patience,
                      unsigned long long metric, int position)
{
    tracker->patience = patience ? patience : 1;
    tracker->misses = 0;
    tracker->best_metric = metric;
    tracker->best_position = position;
    tracker->have_best = 1;
}

int af_tracker_update(af_tracker *tracker, unsigned long long metric,
                      int position)
{
    if (!tracker->have_best || metric > tracker->best_metric)
    {
        tracker->best_metric = metric;
        tracker->best_position = position;
        tracker->have_best = 1;
        tracker->misses = 0;
        return 0;
    }
    tracker->misses++;
    return tracker->misses >= tracker->patience;
}

int af_pick_direction(unsigned long long start, unsigned long long plus,
                      unsigned long long minus)
{
    int plus_better = plus > start;
    int minus_better = minus > start;

    if (plus_better && plus >= minus)
        return 1;
    if (minus_better)
        return -1;
    if (plus_better)
        return 1;
    return 0;
}