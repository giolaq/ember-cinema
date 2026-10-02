#include "ember.h"
#include <assert.h>
#include <string.h>
int main(void) {
    Ember e;
    ember_init(&e);
    assert(ember_key(&e, EK_LEFT, false, 0).type == EA_NONE && e.model.movie == 3);
    assert(ember_key(&e, EK_RIGHT, false, 0).type == EA_NONE && e.model.movie == 0);
    ember_key(&e, EK_OK, false, 0);
    assert(e.model.screen == DETAILS);
    assert(ember_key(&e, EK_OK, true, 0).type == EA_NONE);
    assert(ember_key(&e, EK_OK, false, 1).type == EA_START);
    assert(e.model.screen == PLAYER && e.loading && e.model.control == 1);
    // Transport keys do nothing until the stream is ready.
    assert(ember_key(&e, EK_PLAY_PAUSE, false, 1).type == EA_NONE);
    ember_player_ready(&e, 52000, 2);
    assert(e.started && !e.loading);
    assert(ember_key(&e, EK_PLAY_PAUSE, false, 3).type == EA_PAUSE && e.paused);
    assert(ember_key(&e, EK_PAUSE, false, 3).type == EA_NONE);
    assert(ember_key(&e, EK_PLAY, false, 3).type == EA_RESUME && !e.paused);
    // Hidden controls: the first OK only reveals them.
    assert(ember_key(&e, EK_OK, false, 20).type == EA_NONE && !e.paused);
    assert(ember_key(&e, EK_RIGHT, false, 21).type == EA_NONE && e.model.control == 2);
    e.position = 45000;
    EmberAction a = ember_key(&e, EK_OK, false, 21);
    assert(a.type == EA_SEEK && a.position == 52000 && e.ended);
    e.ended = false;
    a = ember_key(&e, EK_REWIND, false, 21);
    assert(a.type == EA_SEEK && a.position == 42000);
    // Ends only after the seek grace period, then replays from the start.
    assert(!ember_player_progress(&e, 51800, false, 21.5));
    assert(ember_player_progress(&e, 51800, false, 23));
    assert(e.ended && e.paused && ember_controls_shown(&e, 100));
    assert(ember_key(&e, EK_PLAY_PAUSE, false, 24).type == EA_REPLAY);
    assert(!e.ended && !e.paused && e.position == 0);
    ember_player_failed(&e, "Playback interrupted.");
    assert(!e.started && e.model.screen == PLAYER && strcmp(e.error, "Playback interrupted.") == 0);
    assert(ember_key(&e, EK_BACK, false, 25).type == EA_STOP && e.model.screen == DETAILS);
    ember_player_busy(&e);
    assert(e.model.screen == DETAILS && e.error[0]);
    assert(ember_key(&e, EK_BACK, true, 26).type == EA_NONE && e.model.screen == DETAILS);
    assert(ember_key(&e, EK_BACK, false, 26).type == EA_NONE && e.model.screen == HOME && !e.error[0]);
    assert(ember_key(&e, EK_BACK, false, 27).type == EA_EXIT);
}
