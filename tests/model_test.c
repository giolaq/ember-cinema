#include "model.h"
#include <assert.h>
int main(void) {
    Model m = {.screen = HOME};
    browse(&m, -1);
    assert(m.movie == 3);
    browse(&m, 1);
    assert(m.movie == 0);
    reveal(&m, 10);
    assert(controls_visible(&m, 13.99));
    assert(!controls_visible(&m, 14));
    reveal(&m, 15);
    assert(controls_visible(&m, 15));
    assert(seek_target(2000, -10000, 52000) == 0);
    assert(seek_target(48000, 10000, 52000) == 52000);
    assert(seek_target(22000, -10000, 52000) == 12000);
}
