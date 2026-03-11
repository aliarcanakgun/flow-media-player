#include <SDL2/SDL.h>
#include <iostream>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
#include <GL/gl.h>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

#ifndef GL_BLEND
#define GL_BLEND        0x0BE2
#endif
#ifndef GL_SRC_ALPHA
#define GL_SRC_ALPHA    0x0302
#endif
#ifndef GL_ONE_MINUS_SRC_ALPHA
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#endif
#ifndef GL_QUADS
#define GL_QUADS        0x0007
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES    0x0004
#endif
#ifndef GL_PROJECTION
#define GL_PROJECTION   0x1701
#endif
#ifndef GL_MODELVIEW
#define GL_MODELVIEW    0x1700
#endif

namespace fs = std::filesystem;

struct PlaylistItem {
    std::string path;
    double volume;
    std::vector<int> skip_points;
    bool enable_audio_fading;
};

static Uint32 wakeup_event_type = (Uint32)-1;

static void on_mpv_render_update(void* ctx) {
    SDL_Event event = {0};
    event.type = wakeup_event_type;
    SDL_PushEvent(&event);
}

static void* get_proc_address(void* ctx, const char* name) {
    return SDL_GL_GetProcAddress(name);
}

static void die(const char* msg) {
    std::cerr << "ERROR: " << msg << std::endl;
    exit(1);
}

// sets up an orthographic projection for 2d overlay drawing.
// call end_overlay() when done to restore the previous matrices.
static void begin_overlay(int w, int h) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void end_overlay() {
    glDisable(GL_BLEND);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

static double current_volume = 100.0;
static bool volume_needs_restore = false;
static bool is_paused = false;
static double last_sent_volume = -1.0;

void load_video(mpv_handle* mpv, const PlaylistItem& item) {
    const char* cmd[] = {"loadfile", item.path.c_str(), nullptr};
    mpv_command_async(mpv, 0, cmd);
    current_volume = item.volume;
    double vol = current_volume;
    mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &vol);
    last_sent_volume = vol;
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
    is_paused = !is_paused;
    int pause = is_paused ? 1 : 0;
    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
}

enum class TopLeftIcon { PAUSE, PLAY, SEEK };

static bool icon_visible = false;
static TopLeftIcon current_top_left_icon = TopLeftIcon::PLAY;
static Uint32 icon_show_time = 0;
static const Uint32 ICON_DURATION_MS = 1000;

void show_top_left_icon(TopLeftIcon type) {
    icon_visible = true;
    current_top_left_icon = type;
    icon_show_time = SDL_GetTicks();
}

void draw_top_left_icon(int win_w, int win_h) {
    if (!icon_visible) return;

    Uint32 elapsed = SDL_GetTicks() - icon_show_time;
    if (elapsed >= ICON_DURATION_MS) {
        icon_visible = false;
        return;
    }

    float alpha = 1.0f - (float)elapsed / (float)ICON_DURATION_MS;
    begin_overlay(win_w, win_h);

    float margin = 20.0f;
    float size = 18.0f;

    if (current_top_left_icon == TopLeftIcon::PAUSE) {
        float bar_w = 5.0f, gap = 4.0f;
        float x = margin, y = margin;
        glColor4f(1, 1, 1, alpha * 0.85f);

        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + bar_w, y);
        glVertex2f(x + bar_w, y + size);
        glVertex2f(x, y + size);
        glEnd();

        glBegin(GL_QUADS);
        glVertex2f(x + bar_w + gap, y);
        glVertex2f(x + bar_w + gap + bar_w, y);
        glVertex2f(x + bar_w + gap + bar_w, y + size);
        glVertex2f(x + bar_w + gap, y + size);
        glEnd();
    } else if (current_top_left_icon == TopLeftIcon::PLAY) {
        float x = margin, y = margin;
        glColor4f(1, 1, 1, alpha * 0.85f);

        glBegin(GL_TRIANGLES);
        glVertex2f(x, y);
        glVertex2f(x, y + size);
        glVertex2f(x + size * 0.85f, y + size / 2.0f);
        glEnd();
    } else if (current_top_left_icon == TopLeftIcon::SEEK) {
        float x = margin, y = margin;
        float gap = -2.0f;
        glColor4f(1, 1, 1, alpha * 0.85f);

        glBegin(GL_TRIANGLES);
        // First arrow
        glVertex2f(x, y);
        glVertex2f(x, y + size);
        glVertex2f(x + size * 0.6f, y + size / 2.0f);
        
        // Second arrow
        float x2 = x + size * 0.6f + gap;
        glVertex2f(x2, y);
        glVertex2f(x2, y + size);
        glVertex2f(x2 + size * 0.6f, y + size / 2.0f);
        glEnd();
    }

    end_overlay();
}

static bool volume_icon_visible = false;
static Uint32 volume_show_time = 0;
static bool volume_is_up = true;

void show_volume_icon(bool is_up) {
    volume_icon_visible = true;
    volume_show_time = SDL_GetTicks();
    volume_is_up = is_up;
}

void draw_volume_icon(int win_w, int win_h) {
    if (!volume_icon_visible) return;

    Uint32 elapsed = SDL_GetTicks() - volume_show_time;
    if (elapsed >= ICON_DURATION_MS) {
        volume_icon_visible = false;
        return;
    }

    float alpha = 1.0f - (float)elapsed / (float)ICON_DURATION_MS;
    begin_overlay(win_w, win_h);

    float margin = 20.0f;
    float size = 18.0f;
    float x = win_w - margin - size * 2.5f; // Positioned top-right
    float y = margin;
    
    glColor4f(1, 1, 1, alpha * 0.85f);
    
    // Draw speaker body
    glBegin(GL_QUADS);
    glVertex2f(x, y + size * 0.3f);
    glVertex2f(x + size * 0.4f, y + size * 0.3f);
    glVertex2f(x + size * 0.4f, y + size * 0.7f);
    glVertex2f(x, y + size * 0.7f);
    glEnd();

    // Draw speaker cone
    glBegin(GL_TRIANGLES);
    glVertex2f(x + size * 0.4f, y + size * 0.5f);
    glVertex2f(x + size * 0.8f, y);
    glVertex2f(x + size * 0.8f, y + size);
    glEnd();

    // Draw + or -
    float op_x = x + size + 4.0f;
    float op_y = y + size * 0.5f;
    float line_w = 2.5f;
    float line_len = 10.0f;

    // Horizontal bar (for both + and -)
    glBegin(GL_QUADS);
    glVertex2f(op_x, op_y - line_w / 2.0f);
    glVertex2f(op_x + line_len, op_y - line_w / 2.0f);
    glVertex2f(op_x + line_len, op_y + line_w / 2.0f);
    glVertex2f(op_x, op_y + line_w / 2.0f);
    glEnd();

    // Vertical bar (only for +)
    if (volume_is_up) {
        glBegin(GL_QUADS);
        glVertex2f(op_x + line_len / 2.0f - line_w / 2.0f, op_y - line_len / 2.0f);
        glVertex2f(op_x + line_len / 2.0f + line_w / 2.0f, op_y - line_len / 2.0f);
        glVertex2f(op_x + line_len / 2.0f + line_w / 2.0f, op_y + line_len / 2.0f);
        glVertex2f(op_x + line_len / 2.0f - line_w / 2.0f, op_y + line_len / 2.0f);
        glEnd();
    }

    end_overlay();
}

static bool stop_icon_visible = false;

void draw_stop_icon(int win_w, int win_h) {
    if (!stop_icon_visible) return;

    begin_overlay(win_w, win_h);

    float margin = 20.0f, size = 16.0f;
    glColor4f(1, 1, 1, 0.60f);
    glBegin(GL_QUADS);
    glVertex2f(margin, margin);
    glVertex2f(margin + size, margin);
    glVertex2f(margin + size, margin + size);
    glVertex2f(margin, margin + size);
    glEnd();

    end_overlay();
}

void change_volume(mpv_handle* mpv, double delta) {
    current_volume = std::clamp(current_volume + delta, 0.0, 100.0);
    double vol = current_volume;
    mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &vol);
    last_sent_volume = vol;
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

    // compatibility profile needed for fixed-function gl fades
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

    mpv_set_option_string(mpv, "vo", "libmpv");
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
    wakeup_event_type = SDL_RegisterEvents(1);
    if (wakeup_event_type == (Uint32)-1) die("failed to register SDL event");
    mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, nullptr);

    mpv_observe_property(mpv, 0, "time-remaining", MPV_FORMAT_DOUBLE);

    if (!playlist.empty()) load_video(mpv, playlist[current_index]);

    bool quit = false;
    SDL_ShowCursor(SDL_DISABLE);

    TransitionState state = TransitionState::FADE_IN;
    Uint32 transition_start = SDL_GetTicks();
    int pending_index = -1;
    double pending_seek = 0.0;

    auto request_exit = [&]() {
        if (state == TransitionState::ENDED || state == TransitionState::FADE_OUT_END) {
            quit = true;
        } else if (state != TransitionState::FADE_OUT_EXIT) {
            state = TransitionState::FADE_OUT_EXIT;
            transition_start = SDL_GetTicks();
        }
    };

    auto try_navigate = [&](int next) {
        if (state != TransitionState::NONE && state != TransitionState::ENDED) return;
        pending_index = next;
        if (state == TransitionState::ENDED) {
            stop_icon_visible = false;
            current_index = pending_index;
            load_video(mpv, playlist[current_index]);
            is_paused = false;
            int pause = 0;
            mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
            state = TransitionState::WAIT_FOR_FADE_IN;
        } else {
            state = TransitionState::FADE_OUT_SWITCH;
            transition_start = SDL_GetTicks();
        }
    };

    while (!quit) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 10)) {
            if (e.type == SDL_QUIT) {
                request_exit();
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    request_exit();
                } else if (e.key.keysym.sym == SDLK_SPACE) {
                    toggle_pause(mpv);
                    show_top_left_icon(is_paused ? TopLeftIcon::PAUSE : TopLeftIcon::PLAY);
                } else if (e.key.keysym.sym == SDLK_UP) {
                    change_volume(mpv, 10.0);
                    show_volume_icon(true);
                } else if (e.key.keysym.sym == SDLK_DOWN) {
                    change_volume(mpv, -10.0);
                    show_volume_icon(false);
                } else if (e.key.keysym.sym == SDLK_RIGHT) {
                    if (!playlist.empty() && current_index < (int)playlist.size() - 1)
                        try_navigate(current_index + 1);
                } else if (e.key.keysym.sym == SDLK_LEFT) {
                    if (!playlist.empty() && current_index > 0)
                        try_navigate(current_index - 1);
                } else if (e.key.keysym.sym == SDLK_LGUI || e.key.keysym.sym == SDLK_RGUI) {
                    is_paused = true;
                    int pause = 1;
                    mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
                    SDL_MinimizeWindow(window);
                } else if (e.key.keysym.sym == SDLK_0) {
                    if (state == TransitionState::NONE && !playlist.empty()) {
                        mpv_command_string(mpv, "seek 0 absolute");
                        is_paused = true;
                        int pause = 1;
                        mpv_set_property_async(mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
                        state = TransitionState::WAIT_FOR_CROSSFADE_IN;
                    }
                } else if (normalize_number_key(e.key.keysym.sym) >= 0) {
                    if (state == TransitionState::NONE && !playlist.empty()) {
                        double jump_time = get_jump_time(e.key.keysym.sym, playlist[current_index]);
                        if (jump_time >= 0) {
                            pending_seek = jump_time;
                            state = TransitionState::CROSSFADE_OUT;
                            transition_start = SDL_GetTicks();
                            show_top_left_icon(TopLeftIcon::SEEK);
                        }
                    }
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
            } else if (mpv_e->event_id == MPV_EVENT_PROPERTY_CHANGE) {
                mpv_event_property* prop = (mpv_event_property*)mpv_e->data;
                if (prop->format == MPV_FORMAT_DOUBLE && prop->name && std::string(prop->name) == "time-remaining") {
                    double time_remaining = *(double*)prop->data;
                    if (state == TransitionState::NONE && time_remaining <= 0.6 && time_remaining >= 0.0) {
                        state = TransitionState::FADE_OUT_END;
                        transition_start = SDL_GetTicks();
                    }
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

        mpv_render_context_render(mpv_gl, render_params);

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
                if (current_index == (int)playlist.size() - 1)
                    stop_icon_visible = true;
            }
        } else if (state == TransitionState::ENDED) {
            alpha = 1.0f;
        } else if (state == TransitionState::CROSSFADE_OUT) {
            if (elapsed < 150) alpha = (elapsed / 150.0f);
            else {
                std::string cmd_str = "seek " + std::to_string(pending_seek) + " absolute";
                mpv_command_string(mpv, cmd_str.c_str());
                state = TransitionState::WAIT_FOR_CROSSFADE_IN;
                alpha = 1.0f;
            }
        } else if (state == TransitionState::CROSSFADE_IN) {
            if (elapsed < 150) alpha = 1.0f - (elapsed / 150.0f);
            else state = TransitionState::NONE;
        } else if (state == TransitionState::WAIT_FOR_FADE_IN) {
            alpha = 1.0f;
        } else if (state == TransitionState::WAIT_FOR_CROSSFADE_IN) {
            alpha = 1.0f;
            if (elapsed > 500) { // fallback timeout
                state = TransitionState::CROSSFADE_IN;
                transition_start = SDL_GetTicks();
            }
        }

        // fade audio along with the overlay when the flag is set
        if (playlist[current_index].enable_audio_fading && state != TransitionState::NONE && state != TransitionState::ENDED) {
            double vol = current_volume * (1.0 - (double)alpha);
            if (std::abs(vol - last_sent_volume) >= 1.0 || vol <= 0.05) {
                mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &vol);
                last_sent_volume = vol;
            }
            volume_needs_restore = true;
        } else if (volume_needs_restore && state == TransitionState::NONE) {
            // restore volume once after a fade transition ends
            double vol = current_volume;
            mpv_set_property_async(mpv, 0, "volume", MPV_FORMAT_DOUBLE, &vol);
            last_sent_volume = vol;
            volume_needs_restore = false;
        }

        if (alpha > 0.0f) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(0, 0, 0, alpha);
            glBegin(GL_QUADS);
            glVertex2f(-1, -1);
            glVertex2f( 1, -1);
            glVertex2f( 1,  1);
            glVertex2f(-1,  1);
            glEnd();
            glDisable(GL_BLEND);
        }

        draw_top_left_icon(w, h);
        draw_volume_icon(w, h);
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
