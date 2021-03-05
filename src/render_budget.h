#ifndef VS_RENDER_BUDGET_H
#define VS_RENDER_BUDGET_H

#include "vesper/film.h"

static inline bool vs_budget_valid(const VsFilm *f,VsSampleBudget budget) {
    if (!budget.max_spp || budget.max_spp>VS_MAX_SPP || !vs_region_valid(f,budget.region)) return false;
    if (!budget.targets) return true;
    uint32_t count=budget.region.width*budget.region.height;
    for (uint32_t i=0;i<count;++i)
        if (budget.targets[i]>budget.max_spp) return false;
    return true;
}

static inline uint32_t vs_budget_target(VsSampleBudget budget,uint32_t x,uint32_t y) {
    VsRegion r=budget.region;
    return budget.targets?budget.targets[(y-r.y)*r.width+x-r.x]:budget.max_spp;
}

#endif
