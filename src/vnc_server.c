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

/* Tag value written into SDL_Keysym.unused so the Previous keymap layer can
 * distinguish key events that originated from the VNC server from local SDL
 * keypresses, and skip host-UI shortcut interception on the former. */
#define VNC_KEYSYM_SOURCE_MAGIC 0x564E4350u /* 'V''N''C''P' */

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
static int vnc_synth_shift_count = 0;
#endif

/* Tile size for dirty-region tracking inside VNCServerUpdateRGBA().  Smaller
 * tiles localise small updates better at the cost of more bookkeeping; 32 is
 * a good middle ground that aligns with libvncserver's own internal block
 * size and Hextile's 16x16 cells. */
#define VNC_DIRTY_TILE 32

#ifdef HAVE_LIBVNCSERVER
static enum rfbNewClientAction vnc_new_client_hook(rfbClientPtr cl);
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
        case XK_Insert: return SDLK_INSERT;
        case XK_Menu: return SDLK_MENU;
        case XK_Print: return SDLK_PRINTSCREEN;
        case XK_Pause: return SDLK_PAUSE;
        case XK_Break: return SDLK_PAUSE;
        case XK_Scroll_Lock: return SDLK_SCROLLLOCK;
        case XK_Sys_Req: return SDLK_SYSREQ;
        case XK_Mode_switch: return SDLK_MODE;
#ifdef XK_ISO_Level3_Shift
        case XK_ISO_Level3_Shift: return SDLK_MODE;
#endif
#ifdef XK_ISO_Left_Tab
        case XK_ISO_Left_Tab: return SDLK_TAB;
#endif
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
#ifdef XK_F13
        case XK_F13: return SDLK_F13;
        case XK_F14: return SDLK_F14;
        case XK_F15: return SDLK_F15;
#endif
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
        case XK_KP_Insert: return SDLK_INSERT;
        case XK_KP_Delete: return SDLK_DELETE;
        case XK_KP_Home: return SDLK_HOME;
        case XK_KP_End: return SDLK_END;
        case XK_KP_Page_Up: return SDLK_PAGEUP;
        case XK_KP_Page_Down: return SDLK_PAGEDOWN;
        case XK_KP_Left: return SDLK_LEFT;
        case XK_KP_Right: return SDLK_RIGHT;
        case XK_KP_Up: return SDLK_UP;
        case XK_KP_Down: return SDLK_DOWN;
        default: return SDLK_UNKNOWN;
    }
}

/* If `keysym` is a US-ASCII printable that requires Shift on a standard US
 * layout, write the unshifted base keysym to *out_base and return true.  Some
 * VNC clients (notably iOS Screen Sharing, on-screen keyboards in browser
 * noVNC builds) send only the shifted keysym without a paired Shift_L event;
 * detecting this lets us synthesize the missing Shift wrapper. */
static bool vnc_keysym_shifted_to_base(rfbKeySym keysym, rfbKeySym *out_base)
{
    rfbKeySym base;
    switch (keysym) {
        case '!': base = '1'; break;
        case '@': base = '2'; break;
        case '#': base = '3'; break;
        case '$': base = '4'; break;
        case '%': base = '5'; break;
        case '^': base = '6'; break;
        case '&': base = '7'; break;
        case '*': base = '8'; break;
        case '(': base = '9'; break;
        case ')': base = '0'; break;
        case '_': base = '-'; break;
        case '+': base = '='; break;
        case '{': base = '['; break;
        case '}': base = ']'; break;
        case '|': base = '\\'; break;
        case ':': base = ';'; break;
        case '"': base = '\''; break;
        case '<': base = ','; break;
        case '>': base = '.'; break;
        case '?': base = '/'; break;
        case '~': base = '`'; break;
        default:
            if (keysym >= 'A' && keysym <= 'Z') {
                base = (rfbKeySym)(keysym + ('a' - 'A'));
            } else {
                return false;
            }
            break;
    }
    if (out_base) *out_base = base;
    return true;
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

static void vnc_push_key_event_full(bool down, SDL_Keycode key,
                                    SDL_Scancode scan, SDL_Keymod mod)
{
    if (key == SDLK_UNKNOWN)
        return;

    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    ev.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    ev.key.repeat = 0;
    ev.key.keysym.scancode = scan;
    ev.key.keysym.sym = key;
    ev.key.keysym.mod = (Uint16)mod;
    /* Tag the event so the Previous keymap layer can recognise it as a VNC-
     * originated keypress and skip host-UI shortcut interception. */
    ev.key.keysym.unused = VNC_KEYSYM_SOURCE_MAGIC;
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

static void vnc_push_wheel_xy(int x, int y)
{
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SDL_MOUSEWHEEL;
    ev.wheel.x = x;
    ev.wheel.y = y;
    SDL_PushEvent(&ev);
}

static void vnc_keyboard_callback(rfbBool down_in, rfbKeySym key, rfbClientPtr client)
{
    (void)client;
    bool down = down_in != 0;

    /* If the keysym is a shifted ASCII printable and the client did not send a
     * paired Shift modifier, synthesize a Shift_L wrapper around the key event
     * so NeXT sees the right base scancode + shift state.  Use a refcount so
     * overlapping shifted keys (e.g. `!` down, `@` down, `!` up, `@` up) keep
     * Shift_L held for as long as any synthesized-shifted key is still pressed. */
    rfbKeySym base_keysym = key;
    bool needs_synth_shift = false;
    if (!(vnc_mod_state & KMOD_SHIFT)) {
        if (vnc_keysym_shifted_to_base(key, &base_keysym))
            needs_synth_shift = true;
    }

    SDL_Keycode sdl_key = vnc_keysym_to_sdl(base_keysym);
    SDL_Scancode sdl_scan = SDL_GetScancodeFromKey(sdl_key);

    if (sdl_key == SDLK_UNKNOWN)
        return;

    if (needs_synth_shift && down) {
        if (vnc_synth_shift_count == 0)
            vnc_push_key_event_full(true, SDLK_LSHIFT,
                SDL_GetScancodeFromKey(SDLK_LSHIFT), vnc_mod_state);
        vnc_synth_shift_count++;
    }

    vnc_update_mod_state(sdl_key, down);

    SDL_Keymod effective_mod = vnc_mod_state;
    if (needs_synth_shift)
        effective_mod = (SDL_Keymod)(effective_mod | KMOD_SHIFT);
    vnc_push_key_event_full(down, sdl_key, sdl_scan, effective_mod);

    if (needs_synth_shift && !down) {
        if (vnc_synth_shift_count > 0)
            vnc_synth_shift_count--;
        if (vnc_synth_shift_count == 0)
            vnc_push_key_event_full(false, SDLK_LSHIFT,
                SDL_GetScancodeFromKey(SDLK_LSHIFT), vnc_mod_state);
    }
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

    /* RFB scroll-wheel button mapping:
     *   bit 8  = button 4 (vertical scroll up)
     *   bit 16 = button 5 (vertical scroll down)
     *   bit 32 = button 6 (horizontal scroll left)
     *   bit 64 = button 7 (horizontal scroll right)
     * Trigger on rising edges only; clients that hold the bit emit repeated
     * mask updates with the bit set, which the rising-edge filter discards.
     * That matches noVNC / libvncserver behaviour where each tick of the wheel
     * is a fresh press+release of the corresponding button. */
    if ((button_mask & 8) && !(previous & 8))
        vnc_push_wheel_xy(0, 1);
    if ((button_mask & 16) && !(previous & 16))
        vnc_push_wheel_xy(0, -1);
    if ((button_mask & 32) && !(previous & 32))
        vnc_push_wheel_xy(-1, 0);
    if ((button_mask & 64) && !(previous & 64))
        vnc_push_wheel_xy(1, 0);
}

static int vnc_thread_func(void *unused)
{
    (void)unused;
    while (!SDL_AtomicGet(&vnc_thread_quit)) {
        bool have_frame = false;
        int dirty_x = 0, dirty_y = 0, dirty_w = 0, dirty_h = 0;
        SDL_LockMutex(vnc_mutex);
        if (!vnc_pending_frame && !SDL_AtomicGet(&vnc_thread_quit))
            SDL_CondWaitTimeout(vnc_cond, vnc_mutex, 16);
        if (SDL_AtomicGet(&vnc_thread_quit)) {
            SDL_UnlockMutex(vnc_mutex);
            break;
        }
        if (vnc_pending_frame) {
            /* Compute the tile-aligned bounding box of pixels that actually
             * differ between the new snapshot and the live framebuffer
             * libvncserver already holds, so we only mark and re-encode the
             * rect that changed.  Marking the full screen on every frame
             * (the old behaviour) forced libvncserver to re-encode the
             * entire framebuffer even if only the mouse cursor moved,
             * which dominated VNC CPU/bandwidth use during idle. */
            int min_x = vnc_width, min_y = vnc_height;
            int max_x = -1, max_y = -1;
            const int tile = VNC_DIRTY_TILE;
            for (int ty = 0; ty < vnc_height; ty += tile) {
                int th = (ty + tile <= vnc_height) ? tile : (vnc_height - ty);
                for (int tx = 0; tx < vnc_width; tx += tile) {
                    int tw = (tx + tile <= vnc_width) ? tile : (vnc_width - tx);
                    bool changed = false;
                    for (int row = 0; row < th && !changed; row++) {
                        size_t off = (size_t)(ty + row) * (size_t)vnc_pitch +
                                     (size_t)tx * 4u;
                        if (memcmp(vnc_framebuffer + off,
                                   vnc_snapshot + off,
                                   (size_t)tw * 4u) != 0)
                            changed = true;
                    }
                    if (changed) {
                        if (tx < min_x) min_x = tx;
                        if (ty < min_y) min_y = ty;
                        if (tx + tw > max_x) max_x = tx + tw;
                        if (ty + th > max_y) max_y = ty + th;
                    }
                }
            }
            if (max_x > 0 && max_y > 0) {
                dirty_x = min_x;
                dirty_y = min_y;
                dirty_w = max_x - min_x;
                dirty_h = max_y - min_y;
                /* Copy only the dirty rect from snapshot into the live
                 * framebuffer that libvncserver renders from. */
                for (int row = dirty_y; row < dirty_y + dirty_h; row++) {
                    size_t off = (size_t)row * (size_t)vnc_pitch +
                                 (size_t)dirty_x * 4u;
                    memcpy(vnc_framebuffer + off,
                           vnc_snapshot + off,
                           (size_t)dirty_w * 4u);
                }
                have_frame = true;
            }
            vnc_pending_frame = false;
        }
        SDL_UnlockMutex(vnc_mutex);

        if (have_frame && vnc_server)
            rfbMarkRectAsModified(vnc_server, dirty_x, dirty_y,
                                  dirty_x + dirty_w, dirty_y + dirty_h);
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
    vnc_server->newClientHook = vnc_new_client_hook;
    /* Use the dispatch loop in vnc_thread_func() rather than libvncserver's
     * background thread; we already serialise framebuffer access on vnc_mutex. */
    vnc_server->deferUpdateTime = 0;

    rfbInitServer(vnc_server);

    vnc_mutex = SDL_CreateMutex();
    vnc_cond = SDL_CreateCond();
    SDL_AtomicSet(&vnc_thread_quit, 0);
    vnc_pending_frame = false;
    vnc_pointer_x = 0;
    vnc_pointer_y = 0;
    vnc_pointer_buttons = 0;
    vnc_mod_state = KMOD_NONE;
    vnc_synth_shift_count = 0;
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

#ifdef HAVE_LIBVNCSERVER
static enum rfbNewClientAction vnc_new_client_hook(rfbClientPtr cl)
{
    if (!cl)
        return RFB_CLIENT_ACCEPT;
    /* Set sensible Tight encoding quality defaults if the client selects
     * Tight.  libvncserver's bare defaults produce blurry output for
     * emulator-style screens with lots of solid panels and crisp text.
     * Pick high quality + moderate compression for a desktop workload. */
#if defined(LIBVNCSERVER_HAVE_LIBJPEG) || defined(LIBVNCSERVER_HAVE_LIBPNG)
    cl->tightQualityLevel = 9;      /* highest non-lossless quality */
    cl->tightCompressLevel = 4;     /* moderate zlib compression */
#ifdef LIBVNCSERVER_HAVE_LIBJPEG
    cl->turboQualityLevel = 95;     /* libjpeg-turbo quality (1-100) */
    cl->turboSubsampLevel = 0;      /* TURBO_SUBSAMP_1X: no chroma subsampling */
#endif
#endif
    /* useCopyRect is reset by every SetEncodings; enable it pre-emptively so
     * clients that advertise CopyRect get the scroll/window-move
     * optimisation from the first frame. */
    cl->useCopyRect = TRUE;
    return RFB_CLIENT_ACCEPT;
}
#endif
