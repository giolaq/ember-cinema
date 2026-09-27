#ifndef MODEL_H
#define MODEL_H
#include <stdbool.h>
typedef enum { HOME, DETAILS, PLAYER } Screen;
typedef struct {
    Screen screen;
    int movie, control;
    double last_input;
    bool controls;
} Model;
static inline void reveal(Model *m, double now) {
    m->controls = true;
    m->last_input = now;
}
static inline bool controls_visible(Model *m, double now) {
    return m->controls && now - m->last_input < 4.0;
}
static inline int seek_target(int position, int delta, int duration) {
    int p = position + delta;
    return p < 0 ? 0 : (p > duration ? duration : p);
}
static inline void browse(Model *m, int delta) {
    m->movie = (m->movie + delta + 4) % 4;
}
#endif
