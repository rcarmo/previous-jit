#include <SDL.h>
#include <SDL_keycode.h>
#include <SDL_mouse.h>

#include "main.h"
#include "screen.h"
#include "vnc_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBVNCSERVER
#include <rfb/rfb.h>
#include <rfb/keysym.h>
#endif

static bool vnc_enabled = false;
static int vnc_port = 5900;
static bool vnc_warned_unavailable = false;

#ifdef HAVE_LIBVNCSERVER
static rfbScreenInfoPtr vnc_server = NULL;
static SDL_Thread *vnc_thread = NULL;
static SDL_mutex *vnc_mutex = NULL;
static SDL_cond *vnc_cond = NULL;
static SDL_atomic_t vnc_thread_quit;
static uint8_t *vnc_framebuffer = NULL;
static uint8_t *vnc_snapshot = NULL;
static int vnc_width = 0;
static int vnc_height = 0;
static int vnc_pitch = 0;
static bool vnc_pending_frame = false;
static int vnc_pointer_x = 0;
static int vnc_pointer_y = 0;
static int vnc_pointer_buttons = 0;
static SDL_Keymod vnc_mod_state = KMOD_NONE;
#endif

static bool env_truthy(const char *name)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return false;
    return !strcasecmp(value, "1") || !strcasecmp(value, "true") ||
           !strcasecmp(value, "yes") || !strcasecmp(value, "on");
}

static int env_port(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (!value || !*value)
        return fallback;
    int port = atoi(value);
    if (port < 1 || port > 65535)
        return fallback;
    return port;
}

bool VNCServerEnabled(void)
{
    return vnc_enabled;
}

#ifdef HAVE_LIBVNCSERVER
static SDL_Keycode vnc_keysym_to_sdl(rfbKeySym key)
{
    if (key >= 'A' && key <= 'Z')
        return (SDL_Keycode)(key + ('a' - 'A'));
    if (key >= 0x20 && key <= 0x7e)
        return (SDL_Keycode)key;

    switch (key) {
        case XK_Return: return SDLK_RETURN;
        case XK_BackSpace: return SDLK_BACKSPACE;
        case XK_Tab: return SDLK_TAB;
        case XK_Escape: return SDLK_ESCAPE;
        case XK_Delete: return SDLK_DELETE;
        case XK_Home: return SDLK_HOME;
        case XK_End: return SDLK_END;
        case XK_Page_Up: return SDLK_PAGEUP;
        case XK_Page_Down: return SDLK_PAGEDOWN;
        case XK_Left: return SDLK_LEFT;
        case XK_Right: return SDLK_RIGHT;
        case XK_Up: return SDLK_UP;
        case XK_Down: return SDLK_DOWN;
        case XK_F1: return SDLK_F1;
        case XK_F2: return SDLK_F2;
        case XK_F3: return SDLK_F3;
        case XK_F4: return SDLK_F4;
        case XK_F5: return SDLK_F5;
        case XK_F6: return SDLK_F6;
        case XK_F7: return SDLK_F7;
        case XK_F8: return SDLK_F8;
        case XK_F9: return SDLK_F9;
        case XK_F10: return SDLK_F10;
        case XK_F11: return SDLK_F11;
        case XK_F12: return SDLK_F12;
        case XK_Shift_L: return SDLK_LSHIFT;
        case XK_Shift_R: return SDLK_RSHIFT;
        case XK_Control_L: return SDLK_LCTRL;
        case XK_Control_R: return SDLK_RCTRL;
        case XK_Alt_L: return SDLK_LALT;
        case XK_Alt_R: return SDLK_RALT;
        case XK_Meta_L: return SDLK_LGUI;
        case XK_Meta_R: return SDLK_RGUI;
        case XK_Super_L: return SDLK_LGUI;
        case XK_Super_R: return SDLK_RGUI;
        case XK_Caps_Lock: return SDLK_CAPSLOCK;
        case XK_Num_Lock: return SDLK_NUMLOCKCLEAR;
        case XK_KP_0: return SDLK_KP_0;
        case XK_KP_1: return SDLK_KP_1;
        case XK_KP_2: return SDLK_KP_2;
        case XK_KP_3: return SDLK_KP_3;
        case XK_KP_4: return SDLK_KP_4;
        case XK_KP_5: return SDLK_KP_5;
        case XK_KP_6: return SDLK_KP_6;
        case XK_KP_7: return SDLK_KP_7;
        case XK_KP_8: return SDLK_KP_8;
        case XK_KP_9: return SDLK_KP_9;
        case XK_KP_Decimal: return SDLK_KP_PERIOD;
        case XK_KP_Add: return SDLK_KP_PLUS;
        case XK_KP_Subtract: return SDLK_KP_MINUS;
        case XK_KP_Multiply: return SDLK_KP_MULTIPLY;
        case XK_KP_Divide: return SDLK_KP_DIVIDE;
        case XK_KP_Enter: return SDLK_KP_ENTER;
        case XK_KP_Equal: return SDLK_KP_EQUALS;
        default: return SDLK_UNKNOWN;
    }
}

static void vnc_update_mod_state(SDL_Keycode key, bool down)
{
    SDL_Keymod bit = KMOD_NONE;
    if (key == SDLK_LSHIFT || key == SDLK_RSHIFT) bit = KMOD_SHIFT;
    else if (key == SDLK_LCTRL || key == SDLK_RCTRL) bit = KMOD_CTRL;
    else if (key == SDLK_LALT || key == SDLK_RALT) bit = KMOD_ALT;
    else if (key == SDLK_LGUI || key == SDLK_RGUI) bit = KMOD_GUI;

    if (bit == KMOD_NONE)
        return;

    if (down)
        vnc_mod_state = (SDL_Keymod)(vnc_mod_state | bit);
    else
        vnc_mod_state = (SDL_Keymod)(vnc_mod_state & ~bit);
}

static void vnc_push_key_event(bool down, SDL_Keycode key)
{
    if (key == SDLK_UNKNOWN)
        return;

    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat = 0;
    ev.key.keysym.sym = key;
    ev.key.keysym.mod = (Uint16)vnc_mod_state;
    SDL_PushEvent(&ev);
}

static void vnc_push_pointer_button(Uint8 button, bool down, int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.button.button = button;
    ev.button.x = x;
    ev.button.y = y;
    SDL_PushEvent(&ev);
}

static void vnc_push_pointer_motion(int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEMOTION;
    ev.motion.state = 0;
    ev.motion.x = x;
    ev.motion.y = y;
    ev.motion.xrel = x - vnc_pointer_x;
    ev.motion.yrel = y - vnc_pointer_y;
    SDL_PushEvent(&ev);
    vnc_pointer_x = x;
    vnc_pointer_y = y;
}

static void vnc_push_wheel(int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEWHEEL;
    ev.wheel.x = 0;
    ev.wheel.y = y;
    SDL_PushEvent(&ev);
}

static void vnc_keyboard_callback(rfbBool down, rfbKeySym key, rfbClientPtr client)
{
    (void)client;
    SDL_Keycode sdl_key = vnc_keysym_to_sdl(key);
    vnc_update_mod_state(sdl_key, down != 0);
    vnc_push_key_event(down != 0, sdl_key);
}

static void vnc_pointer_callback(int button_mask, int x, int y, rfbClientPtr client)
{
    (void)client;
    int previous = vnc_pointer_buttons;
    vnc_pointer_buttons = button_mask;

    vnc_push_pointer_motion(x, y);

    const int transitions[] = {1, 2, 4};
    const Uint8 mapped[] = {SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT};
    for (size_t i = 0; i < 3; ++i) {
        bool was_down = (previous & transitions[i]) != 0;
        bool now_down = (button_mask & transitions[i]) != 0;
        if (was_down != now_down)
            vnc_push_pointer_button(mapped[i], now_down, x, y);
    }

    if ((button_mask & 8) && !(previous & 8))
        vnc_push_wheel(1);
    if ((button_mask & 16) && !(previous & 16))
        vnc_push_wheel(-1);
}

static int vnc_thread_func(void *unused)
{
    (void)unused;
    while (!SDL_AtomicGet(&vnc_thread_quit)) {
        bool have_frame = false;
        SDL_LockMutex(vnc_mutex);
        if (!vnc_pending_frame && !SDL_AtomicGet(&vnc_thread_quit))
            SDL_CondWaitTimeout(vnc_cond, vnc_mutex, 16);
        if (SDL_AtomicGet(&vnc_thread_quit)) {
            SDL_UnlockMutex(vnc_mutex);
            break;
        }
        if (vnc_pending_frame) {
            memcpy(vnc_framebuffer, vnc_snapshot, (size_t)vnc_pitch * (size_t)vnc_height);
            vnc_pending_frame = false;
            have_frame = true;
        }
        SDL_UnlockMutex(vnc_mutex);

        if (have_frame && vnc_server)
            rfbMarkRectAsModified(vnc_server, 0, 0, vnc_width, vnc_height);
        if (vnc_server)
            rfbProcessEvents(vnc_server, 0);
    }
    return 0;
}

static void vnc_shutdown_server(void)
{
    if (vnc_thread) {
        SDL_AtomicSet(&vnc_thread_quit, 1);
        if (vnc_cond)
            SDL_CondSignal(vnc_cond);
        SDL_WaitThread(vnc_thread, NULL);
        vnc_thread = NULL;
    }
    if (vnc_cond) {
        SDL_DestroyCond(vnc_cond);
        vnc_cond = NULL;
    }
    if (vnc_mutex) {
        SDL_DestroyMutex(vnc_mutex);
        vnc_mutex = NULL;
    }
    if (vnc_server) {
        rfbShutdownServer(vnc_server, TRUE);
        rfbScreenCleanup(vnc_server);
        vnc_server = NULL;
    }
    free(vnc_framebuffer);
    free(vnc_snapshot);
    vnc_framebuffer = NULL;
    vnc_snapshot = NULL;
    vnc_width = 0;
    vnc_height = 0;
    vnc_pitch = 0;
}

static bool vnc_ensure_server(int width, int height)
{
    if (!vnc_enabled)
        return false;
    if (vnc_server && vnc_width == width && vnc_height == height)
        return true;

    vnc_shutdown_server();

    if (width <= 0 || height <= 0)
        return false;

    vnc_width = width;
    vnc_height = height;
    vnc_pitch = width * 4;
    vnc_framebuffer = (uint8_t *)calloc((size_t)vnc_pitch, (size_t)vnc_height);
    vnc_snapshot = (uint8_t *)calloc((size_t)vnc_pitch, (size_t)vnc_height);
    if (!vnc_framebuffer || !vnc_snapshot) {
        fprintf(stderr, "WARNING: failed to allocate VNC framebuffer (%dx%d)\n", width, height);
        vnc_shutdown_server();
        return false;
    }

    char arg0[] = "Previous";
    char *argv[] = { arg0 };
    int argc = 1;
    vnc_server = rfbGetScreen(&argc, argv, width, height, 8, 3, 4);
    if (!vnc_server) {
        fprintf(stderr, "WARNING: failed to initialize libvncserver instance\n");
        vnc_shutdown_server();
        return false;
    }

    vnc_server->desktopName = "Previous";
    vnc_server->alwaysShared = TRUE;
    vnc_server->autoPort = FALSE;
    vnc_server->port = vnc_port;
    vnc_server->ipv6port = vnc_port;
    vnc_server->frameBuffer = (char *)vnc_framebuffer;
    vnc_server->kbdAddEvent = vnc_keyboard_callback;
    vnc_server->ptrAddEvent = vnc_pointer_callback;

    rfbInitServer(vnc_server);

    vnc_mutex = SDL_CreateMutex();
    vnc_cond = SDL_CreateCond();
    SDL_AtomicSet(&vnc_thread_quit, 0);
    vnc_pending_frame = false;
    vnc_pointer_x = 0;
    vnc_pointer_y = 0;
    vnc_pointer_buttons = 0;
    vnc_mod_state = KMOD_NONE;
    vnc_thread = SDL_CreateThread(vnc_thread_func, "[Previous] VNC", NULL);
    if (!vnc_thread) {
        fprintf(stderr, "WARNING: failed to create VNC thread: %s\n", SDL_GetError());
        vnc_shutdown_server();
        return false;
    }

    fprintf(stderr, "VNC server enabled on port %d (%dx%d)\n", vnc_port, width, height);
    return true;
}
#endif

void VNCServerInitFromEnv(int width, int height)
{
    vnc_enabled = env_truthy("PREVIOUS_VNC");
    vnc_port = env_port("PREVIOUS_VNC_PORT", 5900);

    if (!vnc_enabled)
        return;
#ifndef HAVE_LIBVNCSERVER
    if (!vnc_warned_unavailable) {
        fprintf(stderr, "WARNING: PREVIOUS_VNC requested but build lacks libvncserver support\n");
        vnc_warned_unavailable = true;
    }
    vnc_enabled = false;
#else
    vnc_ensure_server(width, height);
#endif
}

void VNCServerShutdown(void)
{
#ifdef HAVE_LIBVNCSERVER
    vnc_shutdown_server();
#endif
    vnc_enabled = false;
}

void VNCServerUpdateRGBA(const uint8_t *pixels, int pitch, int width, int height)
{
#ifdef HAVE_LIBVNCSERVER
    if (!vnc_enabled)
        return;
    if (!vnc_ensure_server(width, height))
        return;
    if (!pixels || pitch <= 0)
        return;

    SDL_LockMutex(vnc_mutex);
    for (int y = 0; y < height; ++y) {
        memcpy(vnc_snapshot + (size_t)y * (size_t)vnc_pitch,
               pixels + (size_t)y * (size_t)pitch,
               (size_t)vnc_pitch);
    }
    vnc_pending_frame = true;
    SDL_CondSignal(vnc_cond);
    SDL_UnlockMutex(vnc_mutex);
#else
    (void)pixels; (void)pitch; (void)width; (void)height;
#endif
}
