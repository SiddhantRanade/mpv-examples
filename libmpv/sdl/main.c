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

int main(int argc, char *argv[])
{
    if (argc != 5)
        die("pass four media files as arguments");

    const int N = 4;

    mpv_handle *mpvs[N];
    for (int i = 0; i < N; ++i) {
        mpvs[i] = mpv_create();
        if (!mpvs[i])
            die("context init failed");
        // Some minor options can only be set before mpv_initialize().
        if (mpv_initialize(mpvs[i]) < 0)
            die("mpv init failed");
        mpv_request_log_messages(mpvs[i], "debug");
    }

    // mpv_handle *mpv = mpv_create();
    // if (!mpv)
    //     die("context init failed");

    // mpv_handle *mpv2 = mpv_create();
    // if (!mpv2)
    //     die("context init 2 failed");

    // // Some minor options can only be set before mpv_initialize().
    // if (mpv_initialize(mpv) < 0)
    //     die("mpv init failed");

    // if (mpv_initialize(mpv2) < 0)
    //     die("mpv2 init failed");

    // mpv_request_log_messages(mpv, "debug");
    // mpv_request_log_messages(mpv2, "debug");

    // Jesus Christ SDL, you suck!
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "no");

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        die("SDL init failed");

    int h = 1000, w = 2000;

    SDL_Window *window =
        SDL_CreateWindow("hi", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         w, h, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN); // | SDL_WINDOW_FULLSCREEN);
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

    mpv_render_context *mpv_gls[N];
    for (size_t i = 0; i < N; i++) {
        if (mpv_render_context_create(&mpv_gls[i], mpvs[i], params) < 0)
            die("failed to initialize mpv GL context");
    }
    

    // mpv_render_context *mpv_gl;
    // if (mpv_render_context_create(&mpv_gl, mpv, params) < 0)
    //     die("failed to initialize mpv GL context");
    
    // mpv_render_context *mpv_gl2;
    // if (mpv_render_context_create(&mpv_gl2, mpv2, params) < 0)
    //     die("failed to initialize mpv GL 2 context");

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

    // mpv_set_wakeup_callback(mpv, on_mpv_events, NULL);
    // mpv_set_wakeup_callback(mpv2, on_mpv_events, NULL);

    // When there is a need to call mpv_render_context_update(), which can
    // request a new frame to be rendered.
    // (Separate from the normal event handling mechanism for the sake of
    //  users which run OpenGL on a different thread.)
    // mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, NULL);
    // mpv_render_context_set_update_callback(mpv_gl2, on_mpv_render_update, NULL);

    // Play this file.
    // const char *cmd[] = {"loadfile", argv[1], NULL};
    // const char *cmd2[] = {"loadfile", argv[2], NULL};
    // mpv_command_async(mpv, 0, cmd);
    // mpv_command_async(mpv2, 0, cmd2);

    // setbuf(stdout, NULL);

    glEnable(GL_TEXTURE_2D);

    GLuint fbos[N];
    glGenFramebuffers(N, fbos);

    GLuint fbtex[N];
    glGenTextures(N, fbtex);

    int nrows = sqrt(N);
    int ncols = N / nrows;
    for (int i=0; i < N; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
        glBindTexture(GL_TEXTURE_2D, fbtex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w / ncols, h / nrows, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbtex[i], 0);
    }

    for (int i=0; i < N; i++) {
        // Allow the video decoder to drop frames during seek, if these frames are before the seek target. If this is enabled, precise seeking can be faster, but if you're using video filters which modify timestamps or add new frames, it can lead to precise seeking skipping the target frame. This e.g. can break frame backstepping when deinterlacing is enabled.
        mpv_set_option_string(mpvs[i], "hr-seek-framedrop\0", "no");
        mpv_set_option_string(mpvs[i], "video-timing-offset\0", "0");
    }
    
    // mpv_set_option_string(mpv, "hr-seek-framedrop\0", "no");
    // mpv_set_option_string(mpv2, "hr-seek-framedrop\0", "no");

    // mpv_set_option_string(mpv, "video-timing-offset\0", "0");
    // mpv_set_option_string(mpv2, "video-timing-offset\0", "0");

    while (1) {
        SDL_Event event;
        if (SDL_WaitEvent(&event) != 1)
            die("event loop error");

        int redraws[N];
        for (int i=0; i < N; i++) redraws[i] = 0;

        // int redraw = 0;
        // int redraw2 = 0;
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
                // mpv_command_async(mpv, 0, cmd_pause);
                // mpv_command_async(mpv2, 0, cmd_pause);
            }
            // Need to take a single screenshot from the framebuffer, or save 4 directly from mpv
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
                // mpv_command_async(mpv, 0, cmd_back);
                // mpv_command_async(mpv2, 0, cmd_back);
            }
            if (event.key.keysym.sym == SDLK_RIGHT) {
                const char *cmd_fwd[] = {
                    "frame-step",
                    NULL
                };
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_fwd);
                // mpv_command_async(mpv, 0, cmd_fwd);
                // mpv_command_async(mpv2, 0, cmd_fwd);
            }
            if (event.key.keysym.sym == SDLK_g) {
                const char *cmd_gamma[] = {"vf",
                                         "toggle",
                                         "format:gamma=linear",
                                         NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_gamma);
                // mpv_command_async(mpv, 0, cmd_gamma);
                // mpv_command_async(mpv2, 0, cmd_gamma);
            }
            if (event.key.keysym.sym == SDLK_b) {
                const char *cmd_blur[] = {"vf",
                                         "toggle",
                                         "gblur:sigma=32",
                                         NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_blur);
                // mpv_command_async(mpv, 0, cmd_gamma);
                // mpv_command_async(mpv2, 0, cmd_gamma);
            }
            if (event.key.keysym.sym == SDLK_r) {
                const char *cmd_reset[] = {"seek", "0", "absolute+exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_reset);
                // mpv_command_async(mpv, 0, cmd_reset);
                // mpv_command_async(mpv2, 0, cmd_reset);
            }
            if (event.key.keysym.sym == SDLK_j) {
                const char *cmd_back_30[] = {"seek", "-30", "exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_back_30);
                // mpv_command_async(mpv, 0, cmd_back_30);
                // mpv_command_async(mpv2, 0, cmd_back_30);
            }
            if (event.key.keysym.sym == SDLK_l) {
                const char *cmd_fwd_30[] = {"seek", "30", "exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, cmd_fwd_30);
                // mpv_command_async(mpv, 0, cmd_fwd_30);
                // mpv_command_async(mpv2, 0, cmd_fwd_30);
            }
            if (event.key.keysym.sym == SDLK_e) {
                const char *seek_end[] = {"seek", "100", "absolute-percent+exact", NULL};
                for (int i=0; i < N; i++) mpv_command_async(mpvs[i], 0, seek_end);
                // mpv_command_async(mpv, 0, seek_end);
                // mpv_command_async(mpv2, 0, seek_end);
            }
            if (event.key.keysym.sym == SDLK_z) {
                // const char *cmd_zoom[] = {
                //     "video-crop",
                //     "toggle",
                //     "<300x300+500+500>",
                //     NULL
                // };
                // mpv_command_async(mpv, 0, cmd_zoom);
                for (int i=0; i < N; i++) mpv_set_option_string(mpvs[i], "video-crop\0", "300x300+500+500\0");
                // mpv_set_option_string(mpv, "video-crop\0", "300x300+500+500\0");
            }

            break;
        default:
            // Happens when there is new work for the render thread (such as
            // rendering a new video frame or redrawing it).
            if (event.type == wakeup_on_mpv_render_update) {
                uint64_t flagss[N];
                for (int i=0; i < N; i++) {
                    flagss[i] = mpv_render_context_update(mpv_gls[i]);
                    if (flagss[i] & MPV_RENDER_UPDATE_FRAME)
                        redraws[i] = 1;
                }

                // uint64_t flags = mpv_render_context_update(mpv_gl);
                // if (flags & MPV_RENDER_UPDATE_FRAME)
                //     redraw = 1;
                
                // uint64_t flags2 = mpv_render_context_update(mpv_gl2);
                // if (flags2 & MPV_RENDER_UPDATE_FRAME)
                //     redraw2 = 1;
            }
            // Happens when at least 1 new event is in the mpv event queue.
            if (event.type == wakeup_on_mpv_events) {
                // Handle all remaining mpv events.
                while (1) {
                    mpv_event *mp_events[N];
                    // mpv_event *mp_event, *mp_event2;

                    for (int i=0; i < N; i++) mp_events[i] = mpv_wait_event(mpvs[i], 0);

                    // mp_event = mpv_wait_event(mpv, 0);
                    // mp_event2 = mpv_wait_event(mpv2, 0);

                    bool no_more_events = true;
                    for (int i=0; i < N; i++) no_more_events &= (mp_events[i]->event_id == MPV_EVENT_NONE);

                    if (no_more_events)
                        break;

                    // if (mp_event->event_id == MPV_EVENT_NONE && mp_event2->event_id == MPV_EVENT_NONE)
                    //     break;

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

                    // if (mp_event->event_id == MPV_EVENT_LOG_MESSAGE) {
                    //     mpv_event_log_message *msg = mp_event->data;
                    //     // Print log messages about DR allocations, just to
                    //     // test whether it works. If there is more than 1 of
                    //     // these, it works. (The log message can actually change
                    //     // any time, so it's possible this logging stops working
                    //     // in the future.)
                    //     // if (strstr(msg->text, "DR image"))
                    //     //     printf("log: %s", msg->text);
                    // }
                    // // printf("event: %s\n", mpv_event_name(mp_event->event_id));

                    // if (mp_event2->event_id == MPV_EVENT_LOG_MESSAGE) {
                    //     mpv_event_log_message *msg = mp_event2->data;
                    //     // Print log messages about DR allocations, just to
                    //     // test whether it works. If there is more than 1 of
                    //     // these, it works. (The log message can actually change
                    //     // any time, so it's possible this logging stops working
                    //     // in the future.)
                    //     // if (strstr(msg->text, "DR image"))
                    //     //     printf("log: %s", msg->text);
                    // }
                    // // printf("event: %s\n", mpv_event_name(mp_event2->event_id));
                }
            }
        }
        int w, h;
        SDL_GetWindowSize(window, &w, &h);

        for (int i=0; i < N; i++) {
            if (redraws[i]) {
                glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]);
                mpv_render_param params[] = {
                    // Specify the default framebuffer (0) as target. This will
                    // render onto the entire screen. If you want to show the video
                    // in a smaller rectangle or apply fancy transformations, you'll
                    // need to render into a separate FBO and draw it manually.
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

                // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                // glBlitFramebuffer(0, 0, w / 2, h, 0, 0, w/2, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        }
        }

        // if (redraw) {
        //     glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
        //     mpv_render_param params[] = {
        //         // Specify the default framebuffer (0) as target. This will
        //         // render onto the entire screen. If you want to show the video
        //         // in a smaller rectangle or apply fancy transformations, you'll
        //         // need to render into a separate FBO and draw it manually.
        //         {MPV_RENDER_PARAM_OPENGL_FBO, &(mpv_opengl_fbo){
        //             .fbo = fbos[0],
        //             // .fbo = 0,
        //             .w = w / 2,
        //             .h = h,
        //         }},
        //         // Flip rendering (needed due to flipped GL coordinate system).
        //         {MPV_RENDER_PARAM_FLIP_Y, &(int){1}},
        //         {0}
        //     };
        //     // See render_gl.h on what OpenGL environment mpv expects, and
        //     // other API details.
        //     mpv_render_context_render(mpv_gl, params);

        //     // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        //     // glBlitFramebuffer(0, 0, w / 2, h, 0, 0, w/2, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        // }
        // if (redraw2) {
        //     glBindFramebuffer(GL_FRAMEBUFFER, fbos[1]);
        //     mpv_render_param params2[] = {
        //         // Specify the default framebuffer (0) as target. This will
        //         // render onto the entire screen. If you want to show the video
        //         // in a smaller rectangle or apply fancy transformations, you'll
        //         // need to render into a separate FBO and draw it manually.
        //         {MPV_RENDER_PARAM_OPENGL_FBO, &(mpv_opengl_fbo){
        //             .fbo = fbos[1],
        //             .w = w / 2,
        //             .h = h,
        //         }},
        //         // Flip rendering (needed due to flipped GL coordinate system).
        //         {MPV_RENDER_PARAM_FLIP_Y, &(int){1}},
        //         {0}
        //     };
        //     // See render_gl.h on what OpenGL environment mpv expects, and
        //     // other API details.
        //     mpv_render_context_render(mpv_gl2, params2);

        //     // glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        //     // glBlitFramebuffer(0, 0, w / 2, h, w/2, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        // }

        bool to_redraw_final = false;
        for (int i=0; i < N; i++) to_redraw_final = (to_redraw_final || redraws[i]);
        
        if (to_redraw_final) {

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

            for (int i=0; i < N; i++) {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[i]);
                
                int x = i / ncols, y = i % ncols;
                glBlitFramebuffer(0, 0, w / ncols, h / nrows, x * w / ncols, y * h / nrows, (x + 1) * w / ncols, (y + 1) * h / nrows, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }

            // glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
            // glBlitFramebuffer(0, 0, w / 2, h, 0, 0, w/2, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);

            // glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[1]);
            // glBlitFramebuffer(0, 0, w / 2, h, w/2, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            
            SDL_GL_SwapWindow(window);
        }
    }
done:

    // Destroy the GL renderer and all of the GL objects it allocated. If video
    // is still running, the video track will be deselected.
    for (int i=0; i < N; i++) mpv_render_context_free(mpv_gls[i]);
    
    // mpv_render_context_free(mpv_gl);

    for (int i=0; i < N; i++) mpv_terminate_destroy(mpvs[i]);
    // mpv_terminate_destroy(mpv);

    printf("properly terminated\n");
    return 0;
}
