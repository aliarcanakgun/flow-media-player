#include <SDL2/SDL.h>
#include <iostream>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
#include <GL/gl.h>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif

namespace fs = std::filesystem;

struct PlaylistItem {
    std::string path;
    double volume;
    std::vector<int> skip_points;
    bool enable_audio_fading;
};

static void on_mpv_render_update(void* ctx) {
    SDL_Event event = {0};
    event.type = SDL_RegisterEvents(1);
    if (event.type != ((Uint32)-1)) {
        SDL_PushEvent(&event);
    }
}

static void* get_proc_address(void* ctx, const char* name) {
    return SDL_GL_GetProcAddress(name);
}

static void die(const char* msg) {
    std::cerr << "ERROR: " << msg << std::endl;
    exit(1);
}

void load_video(mpv_handle* mpv, const PlaylistItem& item) {
    const char* cmd[] = {"loadfile", item.path.c_str(), nullptr};
    mpv_command(mpv, cmd);
    double vol = item.volume;
    mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}

std::string open_file_dialog() {
#ifdef _WIN32
    char filename[MAX_PATH] = {0};
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Playlist JSON\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";
    
    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#endif
    return "";
}

void toggle_pause(mpv_handle* mpv) {
    int pause = 0;
    mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &pause);
    pause = !pause;
    mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &pause);
}

// pause/play icon state
static bool icon_visible = false;
static bool icon_is_paused = false; // true = pause bars, false = play triangle
static Uint32 icon_show_time = 0;
static const Uint32 ICON_DURATION_MS = 1000;

void show_pause_icon(bool paused) {
    icon_visible = true;
    icon_is_paused = paused;
    icon_show_time = SDL_GetTicks();
}

void draw_pause_play_icon(int win_w, int win_h) {
    if (!icon_visible) return;

    Uint32 elapsed = SDL_GetTicks() - icon_show_time;
    if (elapsed >= ICON_DURATION_MS) {
        icon_visible = false;
        return;
    }

    float icon_alpha = 1.0f - (float)elapsed / (float)ICON_DURATION_MS;

    // switch to pixel coords so we can draw the icon at a fixed spot
    glMatrixMode(0x1701); // gl_projection
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1); // origin at top-left
    glMatrixMode(0x1700); // gl_modelview
    glPushMatrix();
    glLoadIdentity();

    glEnable(0x0BE2); // gl_blend
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // position and sizing
    float margin = 20.0f;
    float size = 18.0f;

    if (icon_is_paused) {
        // two vertical bars for pause
        float bar_w = 5.0f;
        float gap = 4.0f;
        float x = margin;
        float y = margin;

        glColor4f(1.0f, 1.0f, 1.0f, icon_alpha * 0.85f);

        // left bar
        glBegin(0x0007); // gl_quads
        glVertex2f(x, y);
        glVertex2f(x + bar_w, y);
        glVertex2f(x + bar_w, y + size);
        glVertex2f(x, y + size);
        glEnd();

        // right bar
        glBegin(0x0007);
        glVertex2f(x + bar_w + gap, y);
        glVertex2f(x + bar_w + gap + bar_w, y);
        glVertex2f(x + bar_w + gap + bar_w, y + size);
        glVertex2f(x + bar_w + gap, y + size);
        glEnd();
    } else {
        // right-pointing triangle for play
        float x = margin;
        float y = margin;

        glColor4f(1.0f, 1.0f, 1.0f, icon_alpha * 0.85f);

        glBegin(0x0004); // gl_triangles
        glVertex2f(x, y);
        glVertex2f(x, y + size);
        glVertex2f(x + size * 0.85f, y + size / 2.0f);
        glEnd();
    }

    glDisable(0x0BE2);

    // restore previous matrices
    glMatrixMode(0x1700);
    glPopMatrix();
    glMatrixMode(0x1701);
    glPopMatrix();
}

// stop icon — stays on screen after the last video finishes
static bool stop_icon_visible = false;

void draw_stop_icon(int win_w, int win_h) {
    if (!stop_icon_visible) return;

    glMatrixMode(0x1701); // gl_projection
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);
    glMatrixMode(0x1700); // gl_modelview
    glPushMatrix();
    glLoadIdentity();

    glEnable(0x0BE2); // gl_blend
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float margin = 20.0f;
    float size = 16.0f;

    glColor4f(1.0f, 1.0f, 1.0f, 0.60f);
    glBegin(0x0007); // gl_quads
    glVertex2f(margin, margin);
    glVertex2f(margin + size, margin);
    glVertex2f(margin + size, margin + size);
    glVertex2f(margin, margin + size);
    glEnd();

    glDisable(0x0BE2);

    glMatrixMode(0x1700);
    glPopMatrix();
    glMatrixMode(0x1701);
    glPopMatrix();
}

// volume up/down with clamping
void change_volume(mpv_handle* mpv, double delta) {
    double vol = 0;
    mpv_get_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    vol += delta;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
}

enum class TransitionState {
    NONE,
    FADE_IN,
    FADE_OUT_SWITCH,
    FADE_OUT_EXIT,
    FADE_OUT_END,
    ENDED,
    CROSSFADE_OUT,
    CROSSFADE_IN,
    WAIT_FOR_FADE_IN,
    WAIT_FOR_CROSSFADE_IN
};

// number key → skip point mapping
int normalize_number_key(int key) {
    if (key >= SDLK_1 && key <= SDLK_9) return key - SDLK_1;
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return key - SDLK_KP_1;
    return -1;
}

double get_jump_time(int key, const PlaylistItem& item) {
    int index = normalize_number_key(key);
    if (index >= 0 && index < (int)item.skip_points.size()) {
        return item.skip_points[index];
    }
    return -1.0;
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) die(SDL_GetError());
    SDL_DisableScreenSaver();

    std::string playlist_file = open_file_dialog();
    if (playlist_file.empty()) {
        std::cerr << "No playlist JSON selected, exiting." << std::endl;
        SDL_Quit();
        return 0;
    }

    std::vector<PlaylistItem> playlist;
    std::ifstream ifs(playlist_file);
    if (!ifs.is_open()) die("Cannot open JSON playlist");
    json j;
    ifs >> j;
    
    fs::path base_path = fs::path(playlist_file).parent_path();
    
    for (const auto& el : j) {
        PlaylistItem pi;
        pi.path = el.value("path", "");
        fs::path p(pi.path);
        if (!p.is_absolute()) {
            pi.path = (base_path / p).string();
        }
        pi.volume = el.value("volume", 100.0);
        pi.enable_audio_fading = el.value("enable_audio_fading", true);
        if (el.contains("skip_points")) {
            for (const auto& sp_el : el["skip_points"]) {
                if (sp_el.is_number()) {
                    pi.skip_points.push_back(sp_el.get<int>());
                }
            }
        }
        playlist.push_back(pi);
    }
    if (playlist.empty()) die("Playlist is empty");

    int current_index = 0;

    // need compatibility profile so we can use fixed-function gl for fades
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0) die(SDL_GetError());

    SDL_Window* window = SDL_CreateWindow(
        "Flow-Media-Player",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        dm.w, dm.h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL
    );

    if (!window) die("Window creation failed");

    SDL_GLContext glcontext = SDL_GL_CreateContext(window);
    if (!glcontext) die("OpenGL context creation failed");

    mpv_handle* mpv = mpv_create();
    if (!mpv) die("failed creating context");

    mpv_set_option_string(mpv, "vo", "libmpv"); // render into our window, not a separate one
    mpv_set_option_string(mpv, "terminal", "yes");
    mpv_set_option_string(mpv, "msg-level", "all=v");
    mpv_set_option_string(mpv, "hwdec", "auto"); 
    mpv_set_option_string(mpv, "keep-open", "yes");
    mpv_set_option_string(mpv, "osc", "no");
    mpv_set_option_string(mpv, "osd-level", "0");
    
    if (mpv_initialize(mpv) < 0) die("mpv init failed");

    mpv_opengl_init_params gl_init_params = { get_proc_address, nullptr };
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    mpv_render_context* mpv_gl = nullptr;
    if (mpv_render_context_create(&mpv_gl, mpv, params) < 0) die("failed to initialize mpv GL context");
    mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, nullptr);

    if (!playlist.empty()) load_video(mpv, playlist[current_index]);

    bool quit = false;
    SDL_ShowCursor(SDL_DISABLE);

    TransitionState state = TransitionState::FADE_IN;
    Uint32 transition_start = SDL_GetTicks();
    int pending_index = -1;
    double pending_seek = 0.0;

    while (!quit) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 10)) {
            if (e.type == SDL_QUIT) {
                if (state == TransitionState::ENDED || state == TransitionState::FADE_OUT_END) {
                    quit = true;
                } else if (state != TransitionState::FADE_OUT_EXIT) {
                    state = TransitionState::FADE_OUT_EXIT;
                    transition_start = SDL_GetTicks();
                }
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    if (state == TransitionState::ENDED || state == TransitionState::FADE_OUT_END) {
                        quit = true;
                    } else if (state != TransitionState::FADE_OUT_EXIT) {
                        state = TransitionState::FADE_OUT_EXIT;
                        transition_start = SDL_GetTicks();
                    }
                } else if (e.key.keysym.sym == SDLK_SPACE) {
                    toggle_pause(mpv);
                    int is_paused = 0;
                    mpv_get_property(mpv, "pause", MPV_FORMAT_FLAG, &is_paused);
                    show_pause_icon(is_paused != 0);
                } else if (e.key.keysym.sym == SDLK_UP) {
                    change_volume(mpv, 10.0);
                } else if (e.key.keysym.sym == SDLK_DOWN) {
                    change_volume(mpv, -10.0);
                } else if (e.key.keysym.sym == SDLK_RIGHT) {
                    if (!playlist.empty() && current_index < (int)playlist.size() - 1 && (state == TransitionState::NONE || state == TransitionState::ENDED)) {
                        pending_index = current_index + 1;
                        if (state == TransitionState::ENDED) {
                            // already on black screen, just load straight away
                            stop_icon_visible = false;
                            current_index = pending_index;
                            load_video(mpv, playlist[current_index]);
                            int pause = 0;
                            mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &pause);
                            state = TransitionState::WAIT_FOR_FADE_IN;
                        } else {
                            state = TransitionState::FADE_OUT_SWITCH;
                            transition_start = SDL_GetTicks();
                        }
                    }
                } else if (e.key.keysym.sym == SDLK_LEFT) {
                    if (!playlist.empty() && current_index > 0 && (state == TransitionState::NONE || state == TransitionState::ENDED)) {
                        pending_index = current_index - 1;
                        if (state == TransitionState::ENDED) {
                            stop_icon_visible = false;
                            current_index = pending_index;
                            load_video(mpv, playlist[current_index]);
                            int pause = 0;
                            mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &pause);
                            state = TransitionState::WAIT_FOR_FADE_IN;
                        } else {
                            state = TransitionState::FADE_OUT_SWITCH;
                            transition_start = SDL_GetTicks();
                        }
                    }
                } else if (e.key.keysym.sym == SDLK_LGUI || e.key.keysym.sym == SDLK_RGUI) {
                    int pause = 1;
                    mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &pause);
                    SDL_MinimizeWindow(window);
                } else if (e.key.keysym.sym == SDLK_0) {
                    if (state == TransitionState::NONE && !playlist.empty()) {
                        // restart current video from the beginning, paused
                        mpv_command_string(mpv, "seek 0 absolute");
                        int pause = 1;
                        mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &pause);
                        
                        // wait for mpv to actually restart before fading back in
                        state = TransitionState::WAIT_FOR_CROSSFADE_IN;
                    }
                } else if (normalize_number_key(e.key.keysym.sym) >= 0) {
                    if (state == TransitionState::NONE && !playlist.empty()) {
                        double jump_time = get_jump_time(e.key.keysym.sym, playlist[current_index]);
                        if (jump_time >= 0) {
                            pending_seek = jump_time;
                            state = TransitionState::CROSSFADE_OUT;
                            transition_start = SDL_GetTicks();
                        }
                    }
                }
            }
        }

        // start fading out when there's less than 600ms left in the video
        if (state == TransitionState::NONE) {
            double time_remaining = 0.0;
            if (mpv_get_property(mpv, "time-remaining", MPV_FORMAT_DOUBLE, &time_remaining) >= 0) {
                if (time_remaining <= 0.6 && time_remaining >= 0.0) {
                    state = TransitionState::FADE_OUT_END;
                    transition_start = SDL_GetTicks();
                }
            }
        }

        while (mpv_event* mpv_e = mpv_wait_event(mpv, 0)) {
            if (mpv_e->event_id == MPV_EVENT_NONE) break;
            if (mpv_e->event_id == MPV_EVENT_PLAYBACK_RESTART) {
                if (state == TransitionState::WAIT_FOR_FADE_IN) {
                    state = TransitionState::FADE_IN;
                    transition_start = SDL_GetTicks();
                } else if (state == TransitionState::WAIT_FOR_CROSSFADE_IN) {
                    state = TransitionState::CROSSFADE_IN;
                    transition_start = SDL_GetTicks();
                }
            }
        }

        Uint32 current_time = SDL_GetTicks();

        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        mpv_opengl_fbo fbo = {0, w, h, 0};
        int flip_y = 1;
        mpv_render_param render_params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        // let mpv draw the current frame
        mpv_render_context_render(mpv_gl, render_params);

        // figure out how much black overlay we need right now
        float alpha = 0.0f;
        Uint32 elapsed = current_time - transition_start;

        if (state == TransitionState::FADE_IN) {
            if (elapsed < 1000) alpha = 1.0f - (elapsed / 1000.0f);
            else state = TransitionState::NONE;
        } else if (state == TransitionState::FADE_OUT_SWITCH) {
            if (elapsed < 600) {
                 alpha = (elapsed / 600.0f);
            } else {
                current_index = pending_index;
                load_video(mpv, playlist[current_index]);
                state = TransitionState::WAIT_FOR_FADE_IN;
                alpha = 1.0f;
            }
        } else if (state == TransitionState::FADE_OUT_EXIT) {
            if (elapsed < 600) {
                alpha = (elapsed / 600.0f);
            } else {
                alpha = 1.0f;
                quit = true;
            }
        } else if (state == TransitionState::FADE_OUT_END) {
            if (elapsed < 600) alpha = (elapsed / 600.0f);
            else {
                state = TransitionState::ENDED;
                alpha = 1.0f;
                // only show the stop icon if we've reached the end of the playlist
                if (current_index == (int)playlist.size() - 1) {
                    stop_icon_visible = true;
                }
            }
        } else if (state == TransitionState::ENDED) {
            alpha = 1.0f; // keep the screen fully black
        } else if (state == TransitionState::CROSSFADE_OUT) {
            if (elapsed < 150) alpha = (elapsed / 150.0f); // quick 150ms fade out
            else {
                // jump to the target timestamp
                std::string cmd_str = "seek " + std::to_string(pending_seek) + " absolute";
                mpv_command_string(mpv, cmd_str.c_str());
                state = TransitionState::WAIT_FOR_CROSSFADE_IN;
                alpha = 1.0f;
            }
        } else if (state == TransitionState::CROSSFADE_IN) {
            if (elapsed < 150) alpha = 1.0f - (elapsed / 150.0f); // quick 150ms fade in
            else state = TransitionState::NONE;
        } else if (state == TransitionState::WAIT_FOR_FADE_IN) {
            alpha = 1.0f;
        } else if (state == TransitionState::WAIT_FOR_CROSSFADE_IN) {
            alpha = 1.0f;
            // fallback: if mpv doesn't fire playback_restart in time, just fade in anyway
            if (elapsed > 500) {
                state = TransitionState::CROSSFADE_IN;
                transition_start = SDL_GetTicks();
            }
        }

        // sync audio volume with the visual fade when enabled for this video
        if (playlist[current_index].enable_audio_fading && state != TransitionState::NONE && state != TransitionState::ENDED) {
            double base_vol = playlist[current_index].volume;
            double faded_vol = base_vol * (1.0 - (double)alpha);
            mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &faded_vol);
        } else if (state == TransitionState::NONE) {
            // make sure volume is back to normal once the transition ends
            double base_vol = playlist[current_index].volume;
            mpv_set_property(mpv, "volume", MPV_FORMAT_DOUBLE, &base_vol);
        }

        if (alpha > 0.0f) {
            glEnable(0x0BE2); // gl_blend
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0.0f, 0.0f, 0.0f, alpha);
            glBegin(0x0007); // gl_quads
            glVertex2f(-1.0f, -1.0f);
            glVertex2f( 1.0f, -1.0f);
            glVertex2f( 1.0f,  1.0f);
            glVertex2f(-1.0f,  1.0f);
            glEnd();
            glDisable(0x0BE2);
        }

        // pause/play indicator
        draw_pause_play_icon(w, h);

        // stop icon if playlist ended
        draw_stop_icon(w, h);

        SDL_GL_SwapWindow(window);
    }

    mpv_render_context_free(mpv_gl);
    mpv_terminate_destroy(mpv);
    SDL_GL_DeleteContext(glcontext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
