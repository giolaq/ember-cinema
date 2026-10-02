#include "ember.h"
#include <stdio.h>
#include <string.h>

const Movie ember_movies[EMBER_MOVIES] = {
    {"Sintel", "FANTASY  /  2010  /  TRAILER",
     "A young traveler searches for the dragon she once befriended.\nAn "
     "extraordinary journey from the Blender open movie project.",
     "sintel.jpg", "https://media.w3.org/2010/05/sintel/trailer.mp4"},
    {"Big Buck Bunny", "ANIMATION  /  2008  /  10-SECOND PREVIEW",
     "A gentle giant. Three mischievous woodland creatures.\nA sunlit "
     "adventure from the Blender open movie project.",
     "bunny.jpg",
     "https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/720/Big_Buck_Bunny_720_10s_1MB.mp4"},
    {"Sintel: The Journey", "DEMO COLLECTION  /  FANTASY",
     "Return to a world of snowy peaks and unlikely friendship.\nThis "
     "collection tile plays the Sintel trailer.",
     "journey.jpg", "https://media.w3.org/2010/05/sintel/trailer.mp4"},
    {"Bunny: The Meadow", "DEMO COLLECTION  /  ANIMATION",
     "A little escape into a beautifully animated woodland.\nThis "
     "collection tile plays the Big Buck Bunny demo clip.",
     "meadow.jpg",
     "https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/720/Big_Buck_Bunny_720_10s_1MB.mp4"}};

static const EmberAction none = {EA_NONE, 0};

void ember_init(Ember *e) {
    memset(e, 0, sizeof *e);
    e->model.screen = HOME;
}
void ember_stop(Ember *e) {
    e->loading = false;
    e->started = false;
    e->model.screen = DETAILS;
}
void ember_player_busy(Ember *e) {
    e->model.screen = DETAILS;
    e->loading = false;
    snprintf(e->error, sizeof e->error, "Finishing the previous connection. Try again shortly.");
}
static EmberAction start(Ember *e, double now) {
    e->error[0] = 0;
    e->model.screen = PLAYER;
    e->model.control = 1;
    reveal(&e->model, now);
    e->position = e->duration = 0;
    e->paused = e->ended = e->started = false;
    e->loading = true;
    return (EmberAction){EA_START, 0};
}
static EmberAction toggle(Ember *e, double now) {
    if (!e->started)
        return none;
    if (e->ended) {
        e->position = 0;
        e->seek_grace = now + 1.0;
        e->ended = e->paused = false;
        return (EmberAction){EA_REPLAY, 0};
    }
    e->paused = !e->paused;
    return (EmberAction){e->paused ? EA_PAUSE : EA_RESUME, 0};
}
static EmberAction seek(Ember *e, int delta, double now) {
    if (!e->started)
        return none;
    int p = seek_target(e->position, delta, e->duration);
    e->position = p;
    e->seek_grace = now + 1.0;
    e->ended = p >= e->duration;
    return (EmberAction){EA_SEEK, p};
}
EmberAction ember_key(Ember *e, EmberKey key, bool repeat, double now) {
    Model *m = &e->model;
    bool ok = key == EK_OK, left = key == EK_LEFT, right = key == EK_RIGHT;
    if (key == EK_NONE)
        return none;
    if (key == EK_BACK) {
        if (repeat)
            return none;
        if (m->screen == PLAYER) {
            ember_stop(e);
            return (EmberAction){EA_STOP, 0};
        }
        if (m->screen == DETAILS) {
            m->screen = HOME;
            e->error[0] = 0;
            return none;
        }
        return (EmberAction){EA_EXIT, 0};
    }
    if (m->screen == HOME) {
        if (left || right)
            browse(m, right ? 1 : -1);
        if (ok && !repeat)
            m->screen = DETAILS;
        return none;
    }
    if (m->screen == DETAILS)
        return ok && !repeat ? start(e, now) : none;
    bool visible = ember_controls_shown(e, now);
    reveal(m, now);
    switch (key) {
    case EK_PLAY_PAUSE:
        return repeat ? none : toggle(e, now);
    case EK_PLAY:
        return e->paused ? toggle(e, now) : none;
    case EK_PAUSE:
        return e->paused ? none : toggle(e, now);
    case EK_FAST_FORWARD:
        return seek(e, 10000, now);
    case EK_REWIND:
        return seek(e, -10000, now);
    default:
        break;
    }
    if (!visible)
        return none;
    if (left)
        m->control = (m->control + 2) % 3;
    if (right)
        m->control = (m->control + 1) % 3;
    if (ok && !repeat)
        return m->control == 1 ? toggle(e, now) : seek(e, m->control == 0 ? -10000 : 10000, now);
    return none;
}
void ember_player_ready(Ember *e, int duration, double now) {
    e->loading = false;
    e->started = true;
    e->duration = duration;
    reveal(&e->model, now);
}
void ember_player_ended(Ember *e, double now) {
    e->ended = e->paused = true;
    reveal(&e->model, now);
}
bool ember_player_progress(Ember *e, int position, bool playing, double now) {
    if (!e->started)
        return false;
    e->position = position;
    if (!playing && !e->paused && !e->ended && now > e->seek_grace &&
        position >= e->duration - 500) {
        ember_player_ended(e, now);
        return true;
    }
    return false;
}
void ember_player_failed(Ember *e, const char *message) {
    e->loading = e->started = false;
    snprintf(e->error, sizeof e->error, "%s", message);
}
bool ember_controls_shown(Ember *e, double now) {
    return controls_visible(&e->model, now) || e->ended;
}
