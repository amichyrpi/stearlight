#include "dashboard_layout.h"

#include <stdio.h>
#include <stdlib.h>

static int center_hit(const svrt_dashboard_layout *layout,
                      const SDL_Rect *rect) {
    return svrt_dashboard_layout_hit(layout, rect->x + rect->w / 2,
                                     rect->y + rect->h / 2);
}

int main(void) {
    svrt_dashboard_layout compact;
    svrt_dashboard_layout_make(0, 1440, 1600, 0.047f, 0.43f,
                               0.807f, 0.40f, &compact);

    int failed = 0;
    failed |= compact.rail.x != 86 || compact.rail.y != 376;
    failed |= compact.rail.w != 68 || compact.rail.h != 688;
    failed |= compact.status_bar.y != 1291 || compact.status_bar.h != 112;
    failed |= compact.power_button.y <= compact.rail.y + compact.rail.h;
    failed |= center_hit(&compact, &compact.power_button) !=
              SVRT_DASHBOARD_HIT_POWER;
    failed |= center_hit(&compact, &compact.plus_button) !=
              SVRT_DASHBOARD_HIT_ADD;
    failed |= center_hit(&compact, &compact.frame_button) !=
              SVRT_DASHBOARD_HIT_FRAME;
    failed |= center_hit(&compact, &compact.view_button) !=
              SVRT_DASHBOARD_HIT_VIEW;
    failed |= center_hit(&compact, &compact.avatar) !=
              SVRT_DASHBOARD_HIT_AVATAR;
    failed |= compact.control_group.x <= compact.plus_button.x +
                                          compact.plus_button.w;
    failed |= compact.control_group.x + compact.control_group.w <
              compact.view_button.x + compact.view_button.w;

    for (int item = 0; item < SVRT_DASHBOARD_NAV_ITEMS; ++item)
        failed |= center_hit(&compact, &compact.rail_items[item]) != item;

    const int chrome_x = compact.status_bar.x + compact.status_bar.w / 3;
    const int chrome_y = compact.status_bar.y + 3;
    failed |= svrt_dashboard_layout_hit(&compact, chrome_x, chrome_y) !=
              SVRT_DASHBOARD_HIT_CHROME;

    svrt_dashboard_layout expanded;
    svrt_dashboard_layout_make(0, 1440, 1600, 0.185f, 0.43f,
                               0.807f, 0.40f, &expanded);
    failed |= expanded.rail.w <= compact.rail.w;
    failed |= abs(expanded.rail_items[0].h - compact.rail_items[0].h) > 1;
    failed |= center_hit(&expanded, &expanded.power_button) !=
              SVRT_DASHBOARD_HIT_POWER;

    if (failed) {
        fputs("SteamVR dashboard layout test: FAILED\n", stderr);
        return 1;
    }
    puts("SteamVR dashboard layout test: passed");
    return 0;
}
