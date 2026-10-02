// Platform-neutral Ember Cinema core: catalog, navigation and player state.
// Android (NativeActivity) and Vega (Turbo Module) both drive this code and
// only perform the platform actions it returns.
#ifndef EMBER_H
#define EMBER_H
#include "model.h"
#include <stdbool.h>

typedef struct {
    const char *title, *tag, *description, *poster, *url;
} Movie;
#define EMBER_MOVIES 4
extern const Movie ember_movies[EMBER_MOVIES];

typedef enum {
    EK_NONE,
    EK_OK,
    EK_LEFT,
    EK_RIGHT,
    EK_UP,
    EK_DOWN,
    EK_BACK,
    EK_PLAY_PAUSE,
    EK_PLAY,
    EK_PAUSE,
    EK_FAST_FORWARD,
    EK_REWIND
} EmberKey;

// Commands the platform must carry out after a state change.
typedef enum {
    EA_NONE,
    EA_START,  // open the stream ember_movies[model.movie].url
    EA_STOP,   // release the player
    EA_RESUME, // continue playback
    EA_PAUSE,  // pause playback
    EA_REPLAY, // seek to 0, then continue playback
    EA_SEEK,   // seek to EmberAction.position (milliseconds)
    EA_EXIT    // leave the app
} EmberActionType;
typedef struct {
    EmberActionType type;
    int position;
} EmberAction;

typedef struct {
    Model model;
    bool loading, started, paused, ended;
    int position, duration;
    double seek_grace;
    char error[160];
} Ember;

void ember_init(Ember *e);
EmberAction ember_key(Ember *e, EmberKey key, bool repeat, double now);
// Leave the player (Back or app backgrounded); the platform releases playback.
void ember_stop(Ember *e);
// The previous player is still being released, so playback cannot start yet.
void ember_player_busy(Ember *e);
void ember_player_ready(Ember *e, int duration, double now);
// Returns true when this update detected the end of playback.
bool ember_player_progress(Ember *e, int position, bool playing, double now);
void ember_player_ended(Ember *e, double now);
void ember_player_failed(Ember *e, const char *message);
bool ember_controls_shown(Ember *e, double now);
#endif
