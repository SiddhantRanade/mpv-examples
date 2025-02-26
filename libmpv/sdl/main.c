// Build with: gcc -o main main.c `pkg-config --libs --cflags mpv sdl2` -std=c99

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "GL/glew.h"

#include <SDL.h>

#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <GL/GL.h>
#include <GL/GLU.h>
#include <math.h>
#include <stdbool.h>

#include <time.h>
#include <stdint.h>

// #define TIME_UTC 1; // Not sure why this is needed

static Uint32 wakeup_on_mpv_render_update, wakeup_on_mpv_events;

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

static void *get_proc_address_mpv(void *fn_ctx, const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

static void on_mpv_events(void *ctx)
{
    SDL_Event event = {.type = wakeup_on_mpv_events};
    SDL_PushEvent(&event);
}

static void on_mpv_render_update(void *ctx)
{
    SDL_Event event = {.type = wakeup_on_mpv_render_update};
    SDL_PushEvent(&event);
}

double difftimespec(const struct timespec *ts1, const struct timespec *ts0) {
  return (ts1->tv_sec - ts0->tv_sec)
      + (ts1->tv_nsec - ts0->tv_nsec) / 1000000000.0;
}

inline void print_time_since(struct timespec *ts, char* txt) {
    struct timespec ts_now;
    // clock_gettime(&ts_now, CLOCK_MONOTONIC);
    clock_gettime(CLOCK_MONOTONIC, &ts_now);

    printf(txt);
    printf(" at %f ms.\n", difftimespec(&ts_now, ts) * 1000);
}

int main(int argc, char *argv[]) {

    const int N_max = 4;

    int N = argc - 1;


    mpv_handle *mpvs[N_max];
    for (int i = 0; i < N; ++i) {
        mpvs[i] = mpv_create();
        if (!mpvs[i])
            die("context init failed");
        // Some minor options can only be set before mpv_initialize().
        if (mpv_initialize(mpvs[i]) < 0)
            die("mpv init failed");
        mpv_request_log_messages(mpvs[i], "debug");
    }

    // Jesus Christ SDL, you suck!
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "no");

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        die("SDL init failed");

    int h_in = 2160, w_in = 3840;

    SDL_Window *window =
        SDL_CreateWindow("hi", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         w_in, h_in, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_INPUT_GRABBED);
    if (!window)
        die("failed to create SDL window");

    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    if (!glcontext)
        die("failed to create SDL GL context");
    
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        /* Problem: glewInit failed, something is seriously wrong. */
        fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
    }

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &(mpv_opengl_init_params){
            .get_proc_address = get_proc_address_mpv,
        }},
        // Tell libmpv that you will call mpv_render_context_update() on render
        // context update callbacks, and that you will _not_ block on the core
        // ever (see <libmpv/render.h> "Threading" section for what libmpv
        // functions you can call at all when this is active).
        // In particular, this means you must call e.g. mpv_command_async()
        // instead of mpv_command().
        // If you want to use synchronous calls, either make them on a separate
        // thread, or remove the option below (this will disable features like
        // DR and is not recommended anyway).
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &(int){1}},
        {0}
    };

    // This makes mpv use the currently set GL context. It will use the callback
    // (passed via params) to resolve GL builtin functions, as well as extensions.

    mpv_render_context *mpv_gls[N_max];
    for (size_t i = 0; i < N; i++) {
        if (mpv_render_context_create(&mpv_gls[i], mpvs[i], params) < 0)
            die("failed to initialize mpv GL context");
    }

    // We use events for thread-safe notification of the SDL main loop.
    // Generally, the wakeup callbacks (set further below) should do as least
    // work as possible, and merely wake up another thread to do actual work.
    // On SDL, waking up the mainloop is the ideal course of action. SDL's
    // SDL_PushEvent() is thread-safe, so we use that.
    wakeup_on_mpv_render_update = SDL_RegisterEvents(1);
    wakeup_on_mpv_events = SDL_RegisterEvents(1);
    if (wakeup_on_mpv_render_update == (Uint32)-1 ||
        wakeup_on_mpv_events == (Uint32)-1)
        die("could not register events");

    for (size_t i = 0; i < N; i++) {
        // When normal mpv events are available.
        mpv_set_wakeup_callback(mpvs[i], on_mpv_events, NULL);

        // When there is a need to call mpv_render_context_update(), which can
        // request a new frame to be rendered.
        // (Separate from the normal event handling mechanism for the sake of
        //  users which run OpenGL on a different thread.)
        mpv_render_context_set_update_callback(mpv_gls[i], on_mpv_render_update, NULL);

        // Play this file.
        const char *cmd[] = {"loadfile", argv[i + 1], NULL};
        mpv_command_async(mpvs[i], 0, cmd);
    }

    setbuf(stdout, NULL);

    glEnable(GL_TEXTURE_2D);

    GLuint fbos[N_max];
    glGenFramebuffers(N, fbos);

    GLuint fbtex[N_max];
    glGenTextures(N, fbtex);

    // int ncols = ceil(sqrt((float) N));
    // int nrows = ceil(((float) N) / ncols);
    int nrows = floor(sqrt((float) N));
    int ncols = ceil(((float) N) / nrows);

    int ndivs = fmax(nrows, ncols);

    for (int i=0; i < N; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glBindTexture(GL_TEXTURE_2D, fbtex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w_in / ncols, h_in / nrows, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbtex[i], 0);
    }

    for (int i=0; i < N; i++) {
        // Allow the video decoder to drop frames during seek, if these frames are before the seek target. If this is enabled, precise seeking can be faster, but if you're using video filters which modify timestamps or add new frames, it can lead to precise seeking skipping the target frame. This e.g. can break frame backstepping when deinterlacing is enabled.
        mpv_set_option_string(mpvs[i], "hr-seek-framedrop\0", "no\0");
        mpv_set_option_string(mpvs[i], "video-timing-offset\0", "0\0");
        // mpv_set_option_string(mpvs[i], "keep-open\0", "yes\0");
        mpv_set_option_string(mpvs[i], "loop-file\0", "inf\0");
    }

    bool mouseIsDown = false;
    int mouseX, mouseY;
    float deltax = 0, deltay = 0;
    float zoom_level = 0;

    const float zoom_adjust = 0.125;

    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    // float win_scale_x = ((float) w_in) / w;
    // float win_scale_y = ((float) h_in) / h;

    const char *cmd_pause[] = {"cycle", "pause", NULL};
    for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_pause);

    int redraws[N_max];
    for (int i=0; i < N; i++) redraws[i] = 0;

    struct timespec ts;
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        printf("starting loop\n");
        
        SDL_Event event;
        if (SDL_WaitEvent(&event) != 1)
            die("event loop error");

        print_time_since(&ts, "got event");

        char pan_x_str[8], pan_y_str[8];
        char zoom_level_str[8];

        switch (event.type) {
        case SDL_QUIT:
            goto done;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_EXPOSED)
                for (int i=0; i < N; i++) redraws[i] = 1;
                // redraw = 1;
                // redraw2 = 1;
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_SPACE) {
                const char *cmd_pause[] = {"cycle", "pause", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_pause);
            }
            // Need to take a single screenshot from the framebuffer, or save N directly from mpv
            // if (event.key.keysym.sym == SDLK_s) {
            //     // Also requires MPV_RENDER_PARAM_ADVANCED_CONTROL if you want
            //     // screenshots to be rendered on GPU (like --vo=gpu would do).
            //     const char *cmd_scr[] = {"screenshot-to-file",
            //                              "screenshot.png",
            //                              "window",
            //                              NULL};
            //     printf("attempting to save screenshot to %s\n", cmd_scr[1]);
            //     mpv_command_async(mpv, 0, cmd_scr);
            // }
            if (event.key.keysym.sym == SDLK_LEFT) {
                const char *cmd_back[] = {
                    "frame-back-step",
                    NULL
                };
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_back);
            }
            if (event.key.keysym.sym == SDLK_RIGHT) {
                const char *cmd_fwd[] = {
                    "frame-step",
                    NULL
                };
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_fwd);
            }
            if (event.key.keysym.sym == SDLK_g) {
                const char *cmd_gamma[] = {"vf",
                                         "toggle",
                                         "format:gamma=linear",
                                         NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_gamma);
            }
            if (event.key.keysym.sym == SDLK_b) {
                const char *cmd_blur[] = {"vf",
                                         "toggle",
                                         "gblur:sigma=32",
                                         NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_blur);
            }
            if (event.key.keysym.sym == SDLK_f) {
                // load_files
                for (size_t i = 0; i < N; i++) {
                    // Play this file.
                    const char *cmd[] = {"loadfile", argv[i + 1], NULL};
                    mpv_command_async(mpvs[i], 0, cmd);
                }
            }
            if (event.key.keysym.sym == SDLK_r) {
                deltax = 0; deltay = 0;

                sprintf(pan_x_str, "%.3f\0", deltax / w);
                sprintf(pan_y_str, "%.3f\0", deltay / h);

                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-x\0", pan_x_str);
                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-y\0", pan_y_str);

                zoom_level = 0;

                sprintf(zoom_level_str, "%.3f\0", zoom_level);

                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-zoom\0", zoom_level_str);

                const char *cmd_reset[] = {"seek", "0", "absolute+exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_reset);
            }
            if (event.key.keysym.sym == SDLK_j) {
                const char *cmd_back_30[] = {"seek", "-30", "exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_back_30);
            }
            if (event.key.keysym.sym == SDLK_l) {
                const char *cmd_fwd_30[] = {"seek", "30", "exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_fwd_30);
            }
            if (event.key.keysym.sym == SDLK_e) {
                const char *seek_end[] = {"seek", "100", "absolute-percent+exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, seek_end);
            }
            if (event.key.keysym.sym == SDLK_z) {
                // const char *cmd_zoom[] = {
                //     "video-crop",
                //     "toggle",
                //     "<300x300+500+500>",
                //     NULL
                // };
                // mpv_command_async(mpv, 0, cmd_zoom);
                // for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-crop\0", "300x300+500+500\0");

                // char *zoom_level_str;
                // sprintf(zoom_level_str, "%d", zoom_level);

                // for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-zoom\0", zoom_level_str);
            }

            break;
        case SDL_MOUSEWHEEL:
            int64_t vid_w, vid_h;
            int _err_code_w = mpv_get_property(mpvs[0], "width", MPV_FORMAT_INT64, &vid_w);
            int _err_code_h = mpv_get_property(mpvs[0], "height", MPV_FORMAT_INT64, &vid_h);

            float vid_aspect = (float) vid_w / vid_h;
            float win_aspect = (float) w / ncols * nrows / h;

            float aspect_h = 1, aspect_w = 1;

            if (vid_aspect > win_aspect) aspect_h = (float) win_aspect / vid_aspect;
            else aspect_w = (float) vid_aspect / win_aspect;

            int x = (int) (mouseX) % (int) (w / ncols) - w / ncols / 2;
            int y = (int) (mouseY) % (int) (h / nrows) - h / nrows / 2;

            if(event.wheel.y > 0) // scroll up
            {
                zoom_level -= zoom_adjust;

                deltax = (deltax - x) / pow(2, zoom_adjust) + x;
                deltay = (deltay - y) / pow(2, zoom_adjust) + y;
            }
            else if(event.wheel.y < 0) // scroll down
            {
                zoom_level += zoom_adjust;

                deltax = (deltax - x) * pow(2, zoom_adjust) + x;
                deltay = (deltay - y) * pow(2, zoom_adjust) + y;
            }

            if(event.wheel.x > 0) // scroll right
            {
                // ...
            }
            else if(event.wheel.x < 0) // scroll left
            {
                // ...
            }

            sprintf(zoom_level_str, "%.3f\0", zoom_level);
            for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-zoom\0", zoom_level_str);

            sprintf(pan_x_str, "%.3f\0", deltax / w / pow(2, zoom_level) * ncols / aspect_w);
            sprintf(pan_y_str, "%.3f\0", deltay / h / pow(2, zoom_level) * nrows / aspect_h);

            for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-x\0", pan_x_str);
            for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-y\0", pan_y_str);

            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseIsDown = true;

                mouseX = event.button.x;
                mouseY = event.button.y;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouseIsDown = false;
            // don't copy, create a function

                deltax += event.button.x - mouseX;
                deltay += event.button.y - mouseY;

                mouseX = event.button.x;
                mouseY = event.button.y;
                sprintf(pan_x_str, "%.3f\0", deltax / w / pow(2, zoom_level) * ndivs);
                sprintf(pan_y_str, "%.3f\0", deltay / h / pow(2, zoom_level) * ndivs);

                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-x\0", pan_x_str);
                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-y\0", pan_y_str);
            }
            break;
        case SDL_MOUSEMOTION:
            if (mouseIsDown) {
                deltax += event.button.x - mouseX;
                deltay += event.button.y - mouseY;

                sprintf(pan_x_str, "%.3f\0", deltax / w / pow(2, zoom_level) * ndivs);
                sprintf(pan_y_str, "%.3f\0", deltay / h / pow(2, zoom_level) * ndivs);

                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-x\0", pan_x_str);
                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-pan-y\0", pan_y_str);
            }
            mouseX = event.motion.x;
            mouseY = event.motion.y;
            break;
        default:
            // Happens when there is new work for the render thread (such as
            // rendering a new video frame or redrawing it).
            if (event.type == wakeup_on_mpv_render_update) {
                print_time_since(&ts, "started wakeup_on_mpv_render_update");

                uint64_t flagss[N_max];
                for (int i=0; i < N; i++) {
                    flagss[i] = mpv_render_context_update(mpv_gls[i]);
                    if (flagss[i] & MPV_RENDER_UPDATE_FRAME)
                        redraws[i] = 1;
                }
                print_time_since(&ts, "finished wakeup_on_mpv_render_update");
            }
            // Happens when at least 1 new event is in the mpv event queue.
            if (event.type == wakeup_on_mpv_events) {
                // Handle all remaining mpv events.
                // bool restart_playback = false;
                print_time_since(&ts, "started wakeup_on_mpv_events");
                while (1) {
                    mpv_event *mp_events[N_max];

                    for (int i=0; i < N; i++) mp_events[i] = mpv_wait_event(mpvs[i], 0);

                    bool no_more_events = true;
                    for (int i=0; i < N; i++) no_more_events &= (mp_events[i]->event_id == MPV_EVENT_NONE);

                    if (no_more_events)
                        break;

                    for (int i=0;i < N; i++) {
                        if (mp_events[i]->event_id == MPV_EVENT_LOG_MESSAGE) {
                            mpv_event_log_message *msg = mp_events[i]->data;
                            // Print log messages about DR allocations, just to
                            // test whether it works. If there is more than 1 of
                            // these, it works. (The log message can actually change
                            // any time, so it's possible this logging stops working
                            // in the future.)
                            // if (strstr(msg->text, "DR image"))
                            //     printf("log: %s", msg->text);
                        }
                        // printf("event: %s\n", mpv_event_name(mp_events[i]->event_id));
                    }
                    // for (int i=0;i < N; i++) {
                    //     if (mp_events[i]->event_id == MPV_EVENT_END_FILE) {
                    //         restart_playback = true;
                    //     }
                    // }
                }
                // if (restart_playback) {
                //     printf("Restarting playback\n");
                //     for (int i=0; i < N; i++) {
                //         const char *cmd_load[] = {"loadfile", argv[i + 1], NULL};
                //         mpv_command_async(mpvs[i], 0, cmd_load);
                //     }
                //     const char *cmd_reset[] = {"seek", "0", "absolute+exact", NULL};
                //     const char *cmd_pause[] = {"set", "pause", "no", NULL};
                //     for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_reset);
                //     for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_pause);
                // }
                // printf("Finished wakeup_on_mpv_events at %d", time(NULL) - start);
                print_time_since(&ts, "finished wakeup_on_mpv_events");
            }
        }

        
        print_time_since(&ts, "finished events");

        // TODO: use property eof-reached to reset

        SDL_GetWindowSize(window, &w, &h);

        bool to_redraw_final = true;
        for (int i=0; i < N; i++) to_redraw_final = (to_redraw_final && redraws[i]);
        
        if (to_redraw_final) {
            print_time_since(&ts, "started redraws");
            for (int i=0; i < N; i++) {
                glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
                mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_OPENGL_FBO, &(mpv_opengl_fbo){
                        .fbo = fbos[i],
                        // .fbo = 0,
                        .w = w / ncols,
                        .h = h / nrows,
                    }},
                    // Flip rendering (needed due to flipped GL coordinate system).
                    {MPV_RENDER_PARAM_FLIP_Y, &(int){1}},
                    {0}
                };
                // See render_gl.h on what OpenGL environment mpv expects, and
                // other API details.
                mpv_render_context_render(mpv_gls[i], params);

                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[i]);
                
                int cc = i % ncols, rr = i / ncols;
                glBlitFramebuffer(0, 0, w / ncols, h / nrows, cc * w / ncols, rr * h / nrows, (cc + 1) * w / ncols, (rr + 1) * h / nrows, GL_COLOR_BUFFER_BIT, GL_NEAREST);

                redraws[i] = 0;
            }
            SDL_GL_SwapWindow(window);

            // char* eof_strs[N_max];
            // for (int i=0; i < N; ++i) {
            //     eof_strs[i] = mpv_get_property_string(mpvs[i], "eof-reached");
            //     printf("EOF %d: %s\n", i, eof_strs[i]);
            // }
            print_time_since(&ts, "finished redraws");
        }
        print_time_since(&ts, "finished loop");
    }
done:

    // Destroy the GL renderer and all of the GL objects it allocated. If video
    // is still running, the video track will be deselected.
    for (int i=0; i < N; i++) mpv_render_context_free(mpv_gls[i]);

    for (int i=0; i < N; i++) mpv_terminate_destroy(mpvs[i]);

    printf("properly terminated\n");
    return 0;
}
