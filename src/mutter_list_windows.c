#define _GNU_SOURCE

#include <glib.h>
#include <glib/gstdio.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>

#define MUTTER_LIST_WINDOWS_API __attribute__((visibility("default")))

typedef void* MetaDisplay;
typedef void* MetaWindow;
typedef int MetaListWindowsFlags;

typedef GSList* meta_display_list_windows_fn(MetaDisplay* display, MetaListWindowsFlags flags);

typedef uint64_t (*meta_window_get_id_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_title_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_wm_class_fn) (MetaWindow *window);

static bool sInitialized = false;
static struct timespec sLastDumpTime = {0, 0};
static meta_window_get_id_fn meta_window_get_id;
static meta_window_get_title_fn meta_window_get_title;
static meta_window_get_wm_class_fn meta_window_get_wm_class;

static void dump_windows(GSList* windows);

static void initialize()
{
    void* handle = dlopen(NULL, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return;
    }

    #define LOAD(name) \
        meta_window_##name = (typeof(meta_window_##name))dlsym(RTLD_DEFAULT, "meta_window_" #name);

    LOAD(get_id);
    LOAD(get_title);
    LOAD(get_wm_class);

    #undef LOAD

    clock_gettime(CLOCK_MONOTONIC, &sLastDumpTime);
}

MUTTER_LIST_WINDOWS_API GSList* meta_display_list_window_detour(MetaDisplay* display, MetaListWindowsFlags flags, meta_display_list_windows_fn orig)
{
    if (!sInitialized) {
        initialize();
        sInitialized = true;
    }

    GSList* windows = orig(display, flags);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_sec = now.tv_sec - sLastDumpTime.tv_sec;
    long elapsed_nsec = now.tv_nsec - sLastDumpTime.tv_nsec;
    if (elapsed_sec > 1 || (elapsed_sec == 1 && elapsed_nsec >= 0)) {
        dump_windows(windows);
        sLastDumpTime = now;
    }
    return windows;
}

static void dump_windows(GSList* windows)
{
    char path_tmp[64];
    char path_out[64];
    snprintf(path_tmp, sizeof(path_tmp), "/run/user/%d/obs_windows_tmp.json", getuid());
    snprintf(path_out, sizeof(path_out), "/run/user/%d/obs_windows.json", getuid());

    GString* s = g_string_new("[");
    bool first = true;
    for (GSList* l = windows; l; l = l->next) {
        MetaWindow window = l->data;
        if (!window) continue;

        uint64_t id = meta_window_get_id(window);
        const char* title = meta_window_get_title ? meta_window_get_title(window) : NULL;
        const char* wm_class = meta_window_get_wm_class ? meta_window_get_wm_class(window) : NULL;

        if (!first) g_string_append_c(s, ',');
        first = false;

        g_string_append_printf(s, "{\"id\":%llu", (unsigned long long)id);

        if (title) {
            g_string_append(s, ",\"title\":\"");
            for (const char* p = title; *p; p++) {
                if (*p == '\\' || *p == '"') {
                    g_string_append_c(s, '\\');
                    g_string_append_c(s, *p);
                } else {
                    g_string_append_c(s, *p);
                }
            }
            g_string_append_c(s, '"');
        }

        if (wm_class) {
            g_string_append(s, ",\"class\":\"");
            for (const char* p = wm_class; *p; p++) {
                if (*p == '\\' || *p == '"') {
                    g_string_append_c(s, '\\');
                    g_string_append_c(s, *p);
                } else {
                    g_string_append_c(s, *p);
                }
            }
            g_string_append_c(s, '"');
        }

        g_string_append_c(s, '}');
    }
    g_string_append(s, "]\n");

    int fd = open(path_tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ssize_t written = write(fd, s->str, s->len);
        (void)written;
        close(fd);
        rename(path_tmp, path_out);
    }

    g_string_free(s, TRUE);
}
