// Runs SDL's patched Android surface callbacks and the event loop's EGL context
// backup/restore against a recording boundary. The test inserts the production
// code at PRODUCTION_SURFACE_CALLBACKS and PRODUCTION_CONTEXT_STEPS.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define SDL_VIDEO_OPENGL_EGL 1
#define JNIEXPORT
#define JNICALL
#define SDL_JAVA_INTERFACE(function) Java_org_libsdl_app_SDLActivity_##function
#define EGL_NO_SURFACE ((EGLSurface)0)
#define SDL_LOG_CATEGORY_VIDEO 3
#define SDL_RENDER_DEVICE_RESET 0x2001

typedef struct JNIEnv JNIEnv;
typedef void *jclass;
typedef void *EGLSurface;
typedef void *EGLContext;
typedef void *NativeWindowType;
typedef struct ANativeWindow ANativeWindow;
typedef struct SDL_mutex SDL_mutex;
typedef enum { SDL_FALSE, SDL_TRUE } SDL_bool;
typedef unsigned int Uint32;
typedef void *SDL_GLContext;
typedef struct { Uint32 type; } SDL_Event;

enum { SDL_WINDOW_OPENGL = 0x2, SDL_WINDOW_VULKAN = 0x10000000 };

typedef struct {
    Uint32 flags;
    void *driverdata;
} SDL_Window;

typedef struct {
    EGLSurface egl_surface;
    EGLContext egl_context;
    SDL_bool backup_done;
    ANativeWindow *native_window;
} SDL_WindowData;

typedef struct SDL_VideoDevice SDL_VideoDevice;

static SDL_VideoDevice *video = (SDL_VideoDevice *)0x1000;
static SDL_mutex *Android_ActivityMutex = (SDL_mutex *)0x2000;
static SDL_Window *Android_Window;
static SDL_bool external_context;
static int locks, egl_creates, egl_destroys, delays, released, errors;
static int backup_after_delays = -1;
static int gl_calls, reset_events;
static SDL_bool egl_loaded;
static char log_text[1024];

static SDL_VideoDevice *SDL_GetVideoDevice(void) { return video; }
static SDL_bool SDL_IsVideoContextExternal(void) { return external_context; }
static void SDL_LockMutex(SDL_mutex *mutex) { (void)mutex; locks++; }
static void SDL_UnlockMutex(SDL_mutex *mutex) { (void)mutex; locks--; }
static int SDL_SetError(const char *format, ...) { (void)format; errors++; return -1; }
static void ANativeWindow_release(ANativeWindow *window) { (void)window; released++; }

static EGLSurface SDL_EGL_CreateSurface(SDL_VideoDevice *device, NativeWindowType window)
{
    (void)device;
    (void)window;
    egl_creates++;
    return (EGLSurface)0x3000;
}

static void SDL_EGL_DestroySurface(SDL_VideoDevice *device, EGLSurface surface)
{
    (void)device;
    (void)surface;
    egl_destroys++;
}

static void SDL_Delay(Uint32 ms)
{
    (void)ms;
    // The main thread's event pump sets backup_done while the UI thread waits.
    if (++delays == backup_after_delays) {
        ((SDL_WindowData *)Android_Window->driverdata)->backup_done = SDL_TRUE;
    }
}

static void SDL_LogDebug(int category, const char *format, ...)
{
    size_t used = strlen(log_text);
    va_list arguments;
    (void)category;
    va_start(arguments, format);
    vsnprintf(log_text + used, sizeof(log_text) - used, format, arguments);
    va_end(arguments);
    strncat(log_text, "\n", sizeof(log_text) - strlen(log_text) - 1);
}

// Without EGL loaded, as for the Vulkan window, every GL call fails.
static SDL_GLContext SDL_GL_GetCurrentContext(void) { gl_calls++; return NULL; }
static int SDL_GL_MakeCurrent(SDL_Window *window, SDL_GLContext context)
{
    (void)window;
    (void)context;
    gl_calls++;
    return egl_loaded ? 0 : -1;
}
static SDL_GLContext SDL_GL_CreateContext(SDL_Window *window)
{
    gl_calls++;
    return (window->flags & SDL_WINDOW_OPENGL) ? (SDL_GLContext)0x5000 : NULL;
}
static int SDL_PushEvent(SDL_Event *event)
{
    reset_events += event->type == SDL_RENDER_DEVICE_RESET;
    return 1;
}

// PRODUCTION_CONTEXT_STEPS

// PRODUCTION_SURFACE_CALLBACKS

static int failures;

static void expect(int actual, int expected, const char *what)
{
    if (actual != expected) {
        fprintf(stderr, "%s: got %d, expected %d\n", what, actual, expected);
        failures++;
    }
}

static void expect_log(const char *text)
{
    if (!strstr(log_text, text)) {
        fprintf(stderr, "log lacks \"%s\":\n%s", text, log_text);
        failures++;
    }
}

int main(int argc, char **argv)
{
    const char *scenario = argc > 1 ? argv[1] : "";
    SDL_WindowData data = { EGL_NO_SURFACE, 0, SDL_FALSE, (ANativeWindow *)0x4000 };
    SDL_Window window = { SDL_WINDOW_VULKAN, &data };
    Android_Window = &window;

    if (strcmp(scenario, "vulkan") == 0) {
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged(NULL, NULL);
        expect(egl_creates, 0, "EGL surfaces created for a Vulkan window");
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(NULL, NULL);
        expect(delays, 0, "UI-thread backup waits for a Vulkan window");
        expect(errors, 0, "errors");
        expect_log("onNativeSurfaceChanged: window flags 0x10000000, EGL surface skipped");
        expect_log("onNativeSurfaceDestroyed: window flags 0x10000000, EGL backup wait skipped, 0 ms");
    } else if (strcmp(scenario, "opengl") == 0) {
        window.flags = SDL_WINDOW_OPENGL;
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged(NULL, NULL);
        expect(egl_creates, 1, "EGL surfaces created for an OpenGL window");
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged(NULL, NULL);
        expect(egl_creates, 1, "EGL surfaces created while one exists");
        backup_after_delays = 3;
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(NULL, NULL);
        expect(delays, 3, "UI-thread waits until the context backup");
        expect(egl_destroys, 1, "EGL surfaces destroyed");
        expect(data.egl_surface == EGL_NO_SURFACE, 1, "EGL surface cleared");
        expect(errors, 0, "errors");
        expect_log("onNativeSurfaceChanged: window flags 0x2, EGL surface created");
        expect_log("onNativeSurfaceChanged: window flags 0x2, EGL surface kept");
        expect_log("onNativeSurfaceDestroyed: window flags 0x2, EGL backup wait required, 30 ms");
    } else if (strcmp(scenario, "opengl-timeout") == 0) {
        window.flags = SDL_WINDOW_OPENGL;
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(NULL, NULL);
        expect(delays, 49, "bounded UI-thread waits without a backup");
        expect(errors, 1, "timeout errors");
        expect_log("EGL backup wait required, 490 ms");
    } else if (strcmp(scenario, "external-opengl") == 0) {
        // The event pump never backs up an external context, so nothing
        // would set backup_done. The window's own EGL surface is still SDL's.
        window.flags = SDL_WINDOW_OPENGL;
        external_context = SDL_TRUE;
        data.egl_surface = (EGLSurface)0x3000;
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(NULL, NULL);
        expect(delays, 0, "UI-thread waits for an external context");
        expect(egl_destroys, 1, "EGL surfaces destroyed");
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceChanged(NULL, NULL);
        expect(egl_creates, 1, "EGL surfaces recreated as at window creation");
        expect_log("EGL backup wait skipped, 0 ms");
    } else if (strcmp(scenario, "vulkan-pause-resume") == 0) {
        android_egl_context_backup(&window);
        android_egl_context_restore(&window);
        expect(gl_calls, 0, "GL calls for a Vulkan window");
        expect(reset_events, 0, "render device resets for a Vulkan window");
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(NULL, NULL);
    } else if (strcmp(scenario, "opengl-pause-resume") == 0) {
        window.flags = SDL_WINDOW_OPENGL;
        egl_loaded = SDL_TRUE;
        android_egl_context_backup(&window);
        expect(data.backup_done, SDL_TRUE, "backup recorded");
        Java_org_libsdl_app_SDLActivity_onNativeSurfaceDestroyed(NULL, NULL);
        expect(delays, 0, "UI-thread waits after the backup");
        android_egl_context_restore(&window);
        expect(data.backup_done, SDL_FALSE, "backup cleared on resume");
        expect(gl_calls > 0, 1, "GL calls for an OpenGL window");
        expect(reset_events, 0, "render device resets for a valid context");
    } else {
        fprintf(stderr, "unknown scenario %s\n", scenario);
        return 2;
    }
    expect(released, 1, "native windows released");
    expect(locks, 0, "mutex balance");
    return failures != 0;
}
