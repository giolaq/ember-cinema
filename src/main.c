#define _POSIX_C_SOURCE 200809L
#include "model.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#include "stb_image.h"
#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "Ember", __VA_ARGS__)

typedef struct {
    const char *title, *tag, *description, *poster, *url;
} Movie;
static const Movie movies[] = {
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
typedef struct {
    struct android_app *app;
    JNIEnv *env;
    Model model;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int width, height, sdk;
    bool active;
    GLuint program, video_program, white, font, posters[4], video_texture;
    stbtt_bakedchar glyphs[96];
    const char *play_url;
    jobject mp, st, vs;
    pthread_t worker;
    atomic_int loading;
    bool worker_live, cancelled, started, paused, ended;
    int duration, position, vw, vh;
    double last_poll, last_report, seek_grace, video_start;
    jlong video_stamp;
    int video_frames;
    char error[160];
} App;
static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
static jmethodID method(JNIEnv *e, jobject obj, const char *name, const char *sig) {
    jclass c = (*e)->GetObjectClass(e, obj);
    jmethodID m = (*e)->GetMethodID(e, c, name, sig);
    (*e)->DeleteLocalRef(e, c);
    return m;
}
static bool exception(JNIEnv *e) {
    if (!(*e)->ExceptionCheck(e))
        return false;
    (*e)->ExceptionDescribe(e);
    (*e)->ExceptionClear(e);
    return true;
}
static void call(App *a, jobject o, const char *name) {
    (*a->env)->CallVoidMethod(a->env, o, method(a->env, o, name, "()V"));
}
static int integer(App *a, const char *name) {
    return (*a->env)->CallIntMethod(a->env, a->mp, method(a->env, a->mp, name, "()I"));
}
static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof log, 0, log);
        LOG("shader: %s", log);
    }
    return s;
}
static GLuint program(bool video) {
    const char *v = "attribute vec2 p; attribute vec2 t; varying vec2 uv; void "
                    "main(){gl_Position=vec4(p.x/640.-1.,1.-p.y/360.,0.,1.);uv=t;}";
    const char *f = "precision mediump float; varying vec2 uv; uniform sampler2D tex; uniform vec4 "
                    "color; void main(){gl_FragColor=texture2D(tex,uv)*color;}";
    const char *vf =
        "#extension GL_OES_EGL_image_external : require\nprecision mediump float; varying vec2 uv; "
        "uniform samplerExternalOES tex; uniform mat4 transform; void "
        "main(){gl_FragColor=texture2D(tex,(transform*vec4(uv.x,1.-uv.y,0.,1.)).xy);}";
    GLuint vs = shader(GL_VERTEX_SHADER, v), fs = shader(GL_FRAGMENT_SHADER, video ? vf : f),
           p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "p");
    glBindAttribLocation(p, 1, "t");
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}
static GLuint texture(const unsigned char *data, int w, int h, GLenum format) {
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
    return t;
}
static void quad(App *a, GLuint tex, float x, float y, float w, float h, float u0, float v0,
                 float u1, float v1, float r, float g, float b, float alpha, bool video) {
    float v[] = {x, y, u0, v0, x + w, y, u1, v0, x, y + h, u0, v1, x + w, y + h, u1, v1};
    GLuint p = video ? a->video_program : a->program;
    glUseProgram(p);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(video ? GL_TEXTURE_EXTERNAL_OES : GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(p, "tex"), 0);
    if (!video)
        glUniform4f(glGetUniformLocation(p, "color"), r, g, b, alpha);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), v);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), v + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static void rect(App *a, float x, float y, float w, float h, float r, float g, float b,
                 float alpha) {
    quad(a, a->white, x, y, w, h, 0, 0, 1, 1, r, g, b, alpha, false);
}
static void image_at(App *a, GLuint t, float x, float y, float w, float h, float alpha) {
    quad(a, t, x, y, w, h, 0, 0, 1, 1, 1, 1, 1, alpha, false);
}
static void text_at(App *a, float x, float y, float size, const char *s, float r, float g,
                    float b) {
    float start = x, scale = size / 40;
    for (; *s; s++) {
        if (*s == '\n') {
            x = start;
            y += size * 1.5f;
            continue;
        }
        if (*s < 32 || *s > 126)
            continue;
        float gx = 0, gy = 0;
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(a->glyphs, 1024, 1024, *s - 32, &gx, &gy, &q, 1);
        quad(a, a->font, x + q.x0 * scale, y + q.y0 * scale, (q.x1 - q.x0) * scale,
             (q.y1 - q.y0) * scale, q.s0, q.t0, q.s1, q.t1, r, g, b, 1, false);
        x += gx * scale;
    }
}
static void label(App *a, float x, float y, float size, const char *s) {
    text_at(a, x, y, size, s, .94, .96, .98);
}
static void accent(App *a, float x, float y, float size, const char *s) {
    text_at(a, x, y, size, s, .98, .70, .43);
}
static void button(App *a, float x, float y, float w, const char *s, bool focus) {
    rect(a, x, y, w, 54, focus ? .98 : .12, focus ? .70 : .16, focus ? .43 : .22, 1);
    text_at(a, x + 22, y + 35, 23, s, focus ? .05 : .92, focus ? .07 : .94, focus ? .10 : .97);
}
static void assets(App *a) {
    unsigned char white[] = {255, 255, 255, 255};
    a->white = texture(white, 1, 1, GL_RGBA);
    FILE *f = fopen("/system/fonts/Roboto-Regular.ttf", "rb");
    if (!f)
        f = fopen("/system/fonts/NotoSans-Regular.ttf", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        rewind(f);
        unsigned char *font = malloc(n), *bitmap = calloc(1024 * 1024, 1),
                      *rgba = malloc(1024 * 1024 * 4);
        fread(font, 1, n, f);
        fclose(f);
        stbtt_BakeFontBitmap(font, 0, 40, bitmap, 1024, 1024, 32, 96, a->glyphs);
        for (int i = 0; i < 1024 * 1024; i++) {
            rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = bitmap[i];
        }
        a->font = texture(rgba, 1024, 1024, GL_RGBA);
        free(font);
        free(bitmap);
        free(rgba);
    }
    for (int i = 0; i < 4; i++) {
        AAsset *asset = AAssetManager_open(a->app->activity->assetManager, movies[i].poster,
                                           AASSET_MODE_BUFFER);
        if (asset) {
            int w, h, c;
            unsigned char *pixels = stbi_load_from_memory(AAsset_getBuffer(asset),
                                                          AAsset_getLength(asset), &w, &h, &c, 4);
            if (pixels) {
                a->posters[i] = texture(pixels, w, h, GL_RGBA);
                stbi_image_free(pixels);
            }
            AAsset_close(asset);
        }
    }
}
static void *prepare(void *ptr) {
    App *a = ptr;
    JNIEnv *e;
    (*a->app->activity->vm)->AttachCurrentThread(a->app->activity->vm, &e, 0);
    // Only this worker accesses MediaPlayer until loading publishes a terminal state.
    jstring url = (*e)->NewStringUTF(e, a->play_url);
    (*e)->CallVoidMethod(e, a->mp, method(e, a->mp, "setDataSource", "(Ljava/lang/String;)V"), url);
    (*e)->DeleteLocalRef(e, url);
    bool failed = exception(e);
    if (!failed) {
        (*e)->CallVoidMethod(e, a->mp, method(e, a->mp, "prepare", "()V"));
        failed = exception(e);
    }
    (*a->app->activity->vm)->DetachCurrentThread(a->app->activity->vm);
    atomic_store(&a->loading, failed ? -1 : 2);
    return 0;
}
static void release_player(App *a) {
    if (a->worker_live) {
        pthread_join(a->worker, 0);
        a->worker_live = false;
    }
    if (a->mp) {
        call(a, a->mp, "release");
        (*a->env)->DeleteGlobalRef(a->env, a->mp);
        a->mp = 0;
    }
    if (a->vs) {
        call(a, a->vs, "release");
        (*a->env)->DeleteGlobalRef(a->env, a->vs);
        a->vs = 0;
    }
    if (a->st) {
        call(a, a->st, "release");
        (*a->env)->DeleteGlobalRef(a->env, a->st);
        a->st = 0;
    }
    if (a->video_texture) {
        glDeleteTextures(1, &a->video_texture);
        a->video_texture = 0;
    }
    exception(a->env);
    a->started = false;
    a->cancelled = false;
    atomic_store(&a->loading, 0);
}
static void stop_player(App *a) {
    a->cancelled = true;
    if (atomic_load(&a->loading) != 1)
        release_player(a);
    a->model.screen = DETAILS;
}
static void start_player(App *a) {
    if (a->mp) {
        snprintf(a->error, sizeof a->error,
                 "Finishing the previous connection. Try again shortly.");
        return;
    }
    a->error[0] = 0;
    a->model.screen = PLAYER;
    a->model.control = 1;
    reveal(&a->model, now());
    a->position = 0;
    a->duration = 0;
    a->video_stamp = 0;
    a->video_frames = 0;
    a->paused = false;
    a->ended = false;
    JNIEnv *e = a->env;
    glGenTextures(1, &a->video_texture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, a->video_texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    jclass c = (*e)->FindClass(e, "android/graphics/SurfaceTexture");
    jobject o = (*e)->NewObject(e, c, (*e)->GetMethodID(e, c, "<init>", "(I)V"), a->video_texture);
    a->st = (*e)->NewGlobalRef(e, o);
    (*e)->DeleteLocalRef(e, o);
    (*e)->DeleteLocalRef(e, c);
    c = (*e)->FindClass(e, "android/view/Surface");
    o = (*e)->NewObject(
        e, c, (*e)->GetMethodID(e, c, "<init>", "(Landroid/graphics/SurfaceTexture;)V"), a->st);
    a->vs = (*e)->NewGlobalRef(e, o);
    (*e)->DeleteLocalRef(e, o);
    (*e)->DeleteLocalRef(e, c);
    c = (*e)->FindClass(e, "android/media/MediaPlayer");
    o = (*e)->NewObject(e, c, (*e)->GetMethodID(e, c, "<init>", "()V"));
    a->mp = (*e)->NewGlobalRef(e, o);
    (*e)->DeleteLocalRef(e, o);
    (*e)->DeleteLocalRef(e, c);
    (*e)->CallVoidMethod(e, a->mp, method(e, a->mp, "setSurface", "(Landroid/view/Surface;)V"),
                         a->vs);
    if (exception(e)) {
        snprintf(a->error, sizeof a->error, "Unable to create the video player.");
        release_player(a);
        return;
    }
    a->play_url = movies[a->model.movie].url;
    atomic_store(&a->loading, 1);
    if (pthread_create(&a->worker, 0, prepare, a) != 0) {
        atomic_store(&a->loading, -1);
    } else
        a->worker_live = true;
    LOG("screen=player movie=%d loading", a->model.movie);
}
static void toggle(App *a) {
    if (!a->started)
        return;
    if (a->ended) {
        (*a->env)->CallVoidMethod(a->env, a->mp, method(a->env, a->mp, "seekTo", "(JI)V"), (jlong)0,
                                  3);
        a->position = 0;
        a->seek_grace = now() + 1.0;
        a->ended = false;
        a->paused = true;
    }
    call(a, a->mp, a->paused ? "start" : "pause");
    a->paused = !a->paused;
    exception(a->env);
    LOG("paused=%d position=%d", a->paused, a->position);
}
static void seek(App *a, int delta) {
    if (!a->started)
        return;
    int p = seek_target(integer(a, "getCurrentPosition"), delta, a->duration);
    if (a->sdk >= 26)
        (*a->env)->CallVoidMethod(a->env, a->mp, method(a->env, a->mp, "seekTo", "(JI)V"), (jlong)p,
                                  3);
    else
        (*a->env)->CallVoidMethod(a->env, a->mp, method(a->env, a->mp, "seekTo", "(I)V"), p);
    a->position = p;
    a->seek_grace = now() + 1.0;
    a->ended = p >= a->duration;
    exception(a->env);
    LOG("seek=%d", p);
}
static void poll_player(App *a) {
    int state = atomic_load(&a->loading);
    if (a->cancelled && state != 1) {
        release_player(a);
        return;
    }
    if (a->model.screen != PLAYER)
        return;
    if (state == -1) {
        snprintf(a->error, sizeof a->error,
                 "Could not stream this film. Check your connection and try again.");
        release_player(a);
        LOG("playback error");
        return;
    }
    if (state == 2 && !a->started) {
        if (a->worker_live) {
            pthread_join(a->worker, 0);
            a->worker_live = false;
        }
        a->duration = integer(a, "getDuration");
        a->vw = integer(a, "getVideoWidth");
        a->vh = integer(a, "getVideoHeight");
        call(a, a->mp, "start");
        if (exception(a->env)) {
            snprintf(a->error, sizeof a->error, "Playback could not start. Press Back to retry.");
            release_player(a);
            return;
        }
        a->started = true;
        a->video_start = now();
        reveal(&a->model, now());
        LOG("playing duration=%d video=%dx%d", a->duration, a->vw, a->vh);
    }
    if (a->started && now() - a->last_report > 1) {
        a->last_report = now();
        LOG("status position=%d paused=%d controls=%d frames=%d", a->position, a->paused,
            controls_visible(&a->model, now()), a->video_frames);
    }
    if (a->started && now() - a->last_poll > .25) {
        a->last_poll = now();
        a->position = integer(a, "getCurrentPosition");
        bool playing =
            (*a->env)->CallBooleanMethod(a->env, a->mp, method(a->env, a->mp, "isPlaying", "()Z"));
        if (exception(a->env)) {
            snprintf(a->error, sizeof a->error, "Playback interrupted. Press Back and try again.");
            release_player(a);
            return;
        }
        if (!playing && !a->paused && now() > a->seek_grace && a->position >= a->duration - 500) {
            a->ended = true;
            a->paused = true;
            reveal(&a->model, now());
            LOG("ended");
        }
    }
}
static void draw_video(App *a) {
    if (!a->started)
        return;
    call(a, a->st, "updateTexImage");
    if (exception(a->env))
        return;
    jlong stamp =
        (*a->env)->CallLongMethod(a->env, a->st, method(a->env, a->st, "getTimestamp", "()J"));
    if (stamp != a->video_stamp) {
        a->video_stamp = stamp;
        if (++a->video_frames == 1)
            LOG("video-frame movie=%d", a->model.movie);
    }
    if (!a->video_frames && now() - a->video_start > 10) {
        snprintf(a->error, sizeof a->error,
                 "No video frames received. Press Back to try another film.");
        release_player(a);
        return;
    }
    jfloatArray arr = (*a->env)->NewFloatArray(a->env, 16);
    (*a->env)->CallVoidMethod(a->env, a->st, method(a->env, a->st, "getTransformMatrix", "([F)V"),
                              arr);
    float matrix[16];
    (*a->env)->GetFloatArrayRegion(a->env, arr, 0, 16, matrix);
    (*a->env)->DeleteLocalRef(a->env, arr);
    glUseProgram(a->video_program);
    glUniformMatrix4fv(glGetUniformLocation(a->video_program, "transform"), 1, GL_FALSE, matrix);
    float ratio = a->vh > 0 ? (float)a->vw / a->vh : 16.f / 9;
    float w = 1280, h = w / ratio;
    if (h > 720) {
        h = 720;
        w = h * ratio;
    }
    quad(a, a->video_texture, (1280 - w) / 2, (720 - h) / 2, w, h, 0, 0, 1, 1, 1, 1, 1, 1, true);
}
static void render(App *a) {
    glViewport(0, 0, a->width, a->height);
    glClearColor(.027, .043, .067, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    int sel = a->model.movie;
    const Movie *m = &movies[sel];
    if (a->model.screen == PLAYER) {
        draw_video(a);
        if (!a->started) {
            accent(a, 64, 100, 20, "EMBER  /  NOW PLAYING");
            label(a, 64, 164, 42, m->title);
            label(a, 64, 240, 23, a->error[0] ? a->error : "Connecting to your film...");
            label(a, 64, 660, 20, "BACK  Return to details");
        } else if (controls_visible(&a->model, now()) || a->ended) {
            for (int i = 0; i < 24; i++)
                rect(a, 0, 420 + i * 12.5, 1280, 12.5, 0.02, .035, .06, i / 25.f * .94);
            label(a, 64, 498, 30, m->title);
            accent(a, 1080, 498, 18, a->ended ? "FINISHED" : a->paused ? "PAUSED" : "PLAYING");
            rect(a, 64, 530, 1152, 4, .30, .35, .41, 1);
            rect(a, 64, 530, a->duration ? 1152.f * a->position / a->duration : 0, 4, .98, .70, .43,
                 1);
            char time[80];
            snprintf(time, sizeof time, "%02d:%02d  /  %02d:%02d", a->position / 60000,
                     a->position / 1000 % 60, a->duration / 60000, a->duration / 1000 % 60);
            label(a, 64, 568, 20, time);
            button(a, 420, 585, 138, "- 10 sec", a->model.control == 0);
            button(a, 574, 585, 138,
                   a->ended    ? "Replay"
                   : a->paused ? "Play"
                               : "Pause",
                   a->model.control == 1);
            button(a, 728, 585, 138, "+ 10 sec", a->model.control == 2);
            label(a, 64, 680, 18, "LEFT / RIGHT  Choose     OK  Select     BACK  Movie details");
        }
    } else {
        // Cinematic still with a soft horizontal shade, leaving clear room for copy.
        image_at(a, a->posters[sel], 440, 0, 840, 473, 1);
        for (int i = 0; i < 84; i++)
            rect(a, 440 + i * 10, 0, 10, 475, .027, .043, .067, 1.f - i / 100.f);
        for (int i = 0; i < 30; i++)
            rect(a, 440, 280 + i * 7, 840, 7, .027, .043, .067, i / 29.f);
        accent(a, 64, 62, 24, "E M B E R");
        label(a, 275, 62, 18, "C I N E M A");
        text_at(a, 1020, 62, 18, "THE DEMO EDITION", .57, .65, .73);
        if (a->model.screen == HOME) {
            accent(a, 64, 131, 17, "SMALL FILMS. BIG WORLDS.");
            label(a, 64, 195, 46, m->title);
            text_at(a, 64, 234, 18, m->tag, .65, .73, .81);
            label(a, 64, 290, 22, "Discover something worth watching.");
            label(a, 64, 386, 26, "Tonight's selection");
            text_at(a, 1040, 386, 18, "04 FILMS / CLIPS", .56, .64, .73);
            for (int i = 0; i < 4; i++) {
                float x = 64 + i * 294;
                if (i == sel)
                    rect(a, x - 4, 410, 280, 212, .98, .70, .43, 1);
                rect(a, x, 414, 272, 204, .075, .105, .15, 1);
                image_at(a, a->posters[i], x, 414, 272, 153, 1);
                label(a, x + 12, 598, 21, movies[i].title);
                if (i == sel) {
                    rect(a, x + 10, 425, 62, 24, .98, .70, .43, 1);
                    text_at(a, x + 20, 443, 14, "PLAY", .03, .04, .07);
                }
            }
            text_at(a, 64, 674, 18, "LEFT / RIGHT  Browse     OK  Movie details", .65, .72, .80);
            text_at(a, 936, 674, 17, "FIND YOUR NEXT STORY.", .46, .56, .66);
        } else {
            accent(a, 64, 138, 18, "THE OPEN MOVIE COLLECTION");
            label(a, 64, 209, 48, m->title);
            text_at(a, 64, 255, 19, m->tag, .65, .73, .81);
            label(a, 64, 328, 23, m->description);
            button(a, 64, 416, 220, "Play demo", true);
            label(a, 64, 520, 19, "Streamed over HTTPS  |  Remote ready");
            text_at(a, 64, 565, 18,
                    "(c) Blender Foundation | sintel.org | bigbuckbunny.org | CC BY 3.0", .55, .63,
                    .72);
            if (a->error[0])
                accent(a, 64, 610, 18, a->error);
            label(a, 64, 675, 18, "OK  Play     BACK  Browse movies");
        }
    }
    eglSwapBuffers(a->display, a->surface);
}
static int32_t input(struct android_app *app, AInputEvent *event) {
    App *a = app->userData;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_KEY)
        return 0;
    int key = AKeyEvent_getKeyCode(event);
    bool handled = key == AKEYCODE_DPAD_CENTER || key == AKEYCODE_ENTER ||
                   key == AKEYCODE_NUMPAD_ENTER || key == AKEYCODE_DPAD_LEFT ||
                   key == AKEYCODE_DPAD_RIGHT || key == AKEYCODE_DPAD_UP ||
                   key == AKEYCODE_DPAD_DOWN || key == AKEYCODE_BACK || key == AKEYCODE_ESCAPE ||
                   key == AKEYCODE_MEDIA_PLAY_PAUSE || key == AKEYCODE_MEDIA_PLAY ||
                   key == AKEYCODE_MEDIA_PAUSE || key == AKEYCODE_MEDIA_FAST_FORWARD ||
                   key == AKEYCODE_MEDIA_REWIND || key == AKEYCODE_SPACE;
    if (!handled)
        return 0;
    if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_DOWN)
        return 1;
    bool repeat = AKeyEvent_getRepeatCount(event) > 0;
    bool ok = key == AKEYCODE_DPAD_CENTER || key == AKEYCODE_ENTER || key == AKEYCODE_NUMPAD_ENTER;
    bool left = key == AKEYCODE_DPAD_LEFT, right = key == AKEYCODE_DPAD_RIGHT;
    if (key == AKEYCODE_BACK || key == AKEYCODE_ESCAPE) {
        if (repeat)
            return 1;
        if (a->model.screen == PLAYER)
            stop_player(a);
        else if (a->model.screen == DETAILS) {
            a->model.screen = HOME;
            a->error[0] = 0;
        } else
            ANativeActivity_finish(app->activity);
        LOG("screen=%d", a->model.screen);
        return 1;
    }
    if (a->model.screen == HOME) {
        if (left || right) {
            browse(&a->model, right ? 1 : -1);
            LOG("focus=%d", a->model.movie);
        }
        if (ok && !repeat) {
            a->model.screen = DETAILS;
            LOG("screen=details movie=%d", a->model.movie);
        }
    } else if (a->model.screen == DETAILS) {
        if (ok && !repeat)
            start_player(a);
    } else {
        bool visible = controls_visible(&a->model, now()) || a->ended;
        reveal(&a->model, now());
        if (key == AKEYCODE_MEDIA_PLAY_PAUSE || key == AKEYCODE_SPACE) {
            if (!repeat)
                toggle(a);
        } else if (key == AKEYCODE_MEDIA_PLAY) {
            if (a->paused)
                toggle(a);
        } else if (key == AKEYCODE_MEDIA_PAUSE) {
            if (!a->paused)
                toggle(a);
        } else if (key == AKEYCODE_MEDIA_FAST_FORWARD)
            seek(a, 10000);
        else if (key == AKEYCODE_MEDIA_REWIND)
            seek(a, -10000);
        else if (visible) {
            if (left)
                a->model.control = (a->model.control + 2) % 3;
            if (right)
                a->model.control = (a->model.control + 1) % 3;
            if (ok && !repeat) {
                if (a->model.control == 1)
                    toggle(a);
                else
                    seek(a, a->model.control == 0 ? -10000 : 10000);
            }
        }
    }
    return 1;
}
static void init_gl(App *a) {
    a->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(a->display, 0, 0);
    EGLint attrs[] = {EGL_RENDERABLE_TYPE,
                      EGL_OPENGL_ES2_BIT,
                      EGL_SURFACE_TYPE,
                      EGL_WINDOW_BIT,
                      EGL_RED_SIZE,
                      8,
                      EGL_GREEN_SIZE,
                      8,
                      EGL_BLUE_SIZE,
                      8,
                      EGL_NONE};
    EGLConfig config;
    EGLint n;
    eglChooseConfig(a->display, attrs, &config, 1, &n);
    EGLint format;
    eglGetConfigAttrib(a->display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(a->app->window, 0, 0, format);
    EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    a->context = eglCreateContext(a->display, config, EGL_NO_CONTEXT, ctx);
    a->surface = eglCreateWindowSurface(a->display, config, a->app->window, 0);
    eglMakeCurrent(a->display, a->surface, a->surface, a->context);
    eglSwapInterval(a->display, 1);
    eglQuerySurface(a->display, a->surface, EGL_WIDTH, &a->width);
    eglQuerySurface(a->display, a->surface, EGL_HEIGHT, &a->height);
    a->program = program(false);
    a->video_program = program(true);
    assets(a);
    LOG("renderer %dx%d", a->width, a->height);
}
static void destroy_gl(App *a) {
    if (a->display == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(a->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(a->display, a->surface);
    eglDestroyContext(a->display, a->context);
    eglTerminate(a->display);
    a->video_texture = 0;
    a->display = EGL_NO_DISPLAY;
}
static void command(struct android_app *app, int32_t cmd) {
    App *a = app->userData;
    if (cmd == APP_CMD_INIT_WINDOW && app->window)
        init_gl(a);
    if (cmd == APP_CMD_TERM_WINDOW) {
        if (a->mp)
            stop_player(a);
        destroy_gl(a);
    }
    if (cmd == APP_CMD_RESUME)
        a->active = true;
    if (cmd == APP_CMD_PAUSE) {
        a->active = false;
        if (a->mp)
            stop_player(a);
    }
}
void android_main(struct android_app *app) {
    App a = {0};
    a.app = app;
    a.display = EGL_NO_DISPLAY;
    a.model.screen = HOME;
    a.active = true;
    atomic_init(&a.loading, 0);
    (*app->activity->vm)->AttachCurrentThread(app->activity->vm, &a.env, 0);
    jclass version = (*a.env)->FindClass(a.env, "android/os/Build$VERSION");
    a.sdk = (*a.env)->GetStaticIntField(a.env, version,
                                        (*a.env)->GetStaticFieldID(a.env, version, "SDK_INT", "I"));
    (*a.env)->DeleteLocalRef(a.env, version);
    app->userData = &a;
    app->onAppCmd = command;
    app->onInputEvent = input;
    ANativeActivity_setWindowFlags(app->activity,
                                   AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
    while (!app->destroyRequested) {
        int events;
        struct android_poll_source *source;
        int id;
        while ((id = ALooper_pollOnce(a.display != EGL_NO_DISPLAY && a.active ? 0 : 100, 0, &events,
                                      (void **)&source)) >= 0) {
            if (source)
                source->process(app, source);
            if (app->destroyRequested)
                break;
        }
        poll_player(&a);
        if (a.display != EGL_NO_DISPLAY && a.active)
            render(&a);
    }
    if (a.mp)
        release_player(&a);
    destroy_gl(&a);
    (*app->activity->vm)->DetachCurrentThread(app->activity->vm);
}
