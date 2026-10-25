#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>

#define MUTTER_LIST_WINDOWS_API __attribute__((visibility("default")))

typedef void* MetaDisplay;
typedef void* MetaWindow;
typedef void* MetaWorkspace;
typedef int MetaListWindowsFlags;
typedef int MetaWindowType;
typedef int MetaWindowClientType;
typedef int MetaStackLayer;
typedef int MetaMaximizeFlags;
typedef int MetaFrameType;
typedef int MetaTileMode;
typedef int MetaGravity;

typedef GSList* meta_display_list_windows_fn(MetaDisplay* display, MetaListWindowsFlags flags);

// Basic getters (already in TODO.c)
typedef uint64_t (*meta_window_get_id_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_title_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_description_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_wm_class_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_wm_class_instance_fn) (MetaWindow *window);

// Rect getters
typedef void (*meta_window_get_buffer_rect_fn) (const MetaWindow *window, void *rect);
typedef void (*meta_window_get_frame_rect_fn) (const MetaWindow *window, void *rect);
typedef void (*meta_window_get_client_area_rect_fn) (MetaWindow *window, void *rect);
typedef gboolean (*meta_window_get_icon_geometry_fn) (MetaWindow *window, void *rect);
typedef void (*meta_window_get_work_area_current_monitor_fn) (MetaWindow *window, void *rect);
typedef void (*meta_window_get_work_area_for_monitor_fn) (MetaWindow *window, int monitor, void *rect);
typedef void (*meta_window_get_work_area_all_monitors_fn) (MetaWindow *window, void *rect);
typedef void (*meta_window_get_tile_area_fn) (MetaWindow *window, int tile_mode, void *rect);
typedef gboolean (*meta_window_get_min_size_fn) (MetaWindow *window, int *width, int *height);
typedef gboolean (*meta_window_get_max_size_fn) (MetaWindow *window, int *width, int *height);

// String getters
typedef const char* (*meta_window_get_startup_id_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_sandboxed_app_id_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_gtk_application_id_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_gtk_unique_bus_name_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_gtk_application_object_path_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_gtk_window_object_path_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_gtk_app_menu_object_path_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_gtk_menubar_object_path_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_role_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_mutter_hints_fn) (MetaWindow *window);
typedef const char* (*meta_window_get_tag_fn) (MetaWindow *window);

// Integer/ID getters
typedef int (*meta_window_get_monitor_fn) (MetaWindow *window);
typedef int (*meta_window_get_current_tile_monitor_number_fn) (MetaWindow *window);
typedef pid_t (*meta_window_get_pid_fn) (MetaWindow *window);
typedef MetaWorkspace* (*meta_window_get_workspace_fn) (MetaWindow *window);
typedef MetaDisplay* (*meta_window_get_display_fn) (MetaWindow *window);
typedef MetaWindow* (*meta_window_get_tile_match_fn) (MetaWindow *window);

// Boolean getters
typedef gboolean (*meta_window_is_maximized_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_fullscreen_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_screen_sized_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_monitor_sized_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_on_primary_monitor_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_on_all_workspaces_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_always_on_all_workspaces_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_override_redirect_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_skip_taskbar_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_hidden_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_in_stack_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_remote_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_attached_dialog_fn) (MetaWindow *window);
typedef gboolean (*meta_window_has_attached_dialogs_fn) (MetaWindow *window);
typedef gboolean (*meta_window_has_modals_fn) (MetaWindow *window);
typedef gboolean (*meta_window_has_fullscreen_monitors_fn) (MetaWindow *window);
typedef gboolean (*meta_window_appears_focused_fn) (MetaWindow *window);
typedef gboolean (*meta_window_has_focus_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_mapped_inhibited_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_focusable_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_stackable_fn) (MetaWindow *window);
typedef gboolean (*meta_window_get_alive_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_above_fn) (MetaWindow *window);
typedef gboolean (*meta_window_allows_move_fn) (MetaWindow *window);
typedef gboolean (*meta_window_allows_resize_fn) (MetaWindow *window);
typedef gboolean (*meta_window_can_maximize_fn) (MetaWindow *window);
typedef gboolean (*meta_window_can_minimize_fn) (MetaWindow *window);
typedef gboolean (*meta_window_can_close_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_tiled_side_by_side_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_tiled_left_fn) (MetaWindow *window);
typedef gboolean (*meta_window_is_tiled_right_fn) (MetaWindow *window);

// Enum getters
typedef MetaMaximizeFlags (*meta_window_get_maximize_flags_fn) (MetaWindow *window);
typedef MetaWindowType (*meta_window_get_window_type_fn) (MetaWindow *window);
typedef MetaWindowClientType (*meta_window_get_client_type_fn) (MetaWindow *window);
typedef MetaStackLayer (*meta_window_get_layer_fn) (MetaWindow *window);
typedef MetaFrameType (*meta_window_get_frame_type_fn) (MetaWindow *window);
typedef MetaTileMode (*meta_window_get_tile_mode_fn) (MetaWindow *window);
typedef MetaGravity (*meta_window_get_gravity_fn) (MetaWindow *window);

// Special getters
typedef uint64_t (*meta_window_get_stable_sequence_fn) (MetaWindow *window);
typedef guint32 (*meta_window_get_user_time_fn) (MetaWindow *window);
typedef gboolean (*meta_window_get_a11y_properties_fn) (MetaWindow *window, const char **a11y_dbus_name, const char **toplevel_object_path);
typedef void* (*meta_window_get_wayland_surface_fn) (MetaWindow *window);
typedef void* (*meta_window_get_unit_cgroup_fn) (MetaWindow *window);

static bool sInitialized = false;
static void initialize();
static void dump(GSList* items);

// Function pointer storage - uses same naming as the functions for FUNCTION_ENTRY macro
static meta_window_get_id_fn meta_window_get_id;
static meta_window_get_title_fn meta_window_get_title;
static meta_window_get_description_fn meta_window_get_description;
static meta_window_get_wm_class_fn meta_window_get_wm_class;
static meta_window_get_wm_class_instance_fn meta_window_get_wm_class_instance;
static meta_window_get_buffer_rect_fn meta_window_get_buffer_rect;
static meta_window_get_frame_rect_fn meta_window_get_frame_rect;
static meta_window_get_client_area_rect_fn meta_window_get_client_area_rect;
static meta_window_get_icon_geometry_fn meta_window_get_icon_geometry;
static meta_window_get_work_area_current_monitor_fn meta_window_get_work_area_current_monitor;
static meta_window_get_work_area_for_monitor_fn meta_window_get_work_area_for_monitor;
static meta_window_get_work_area_all_monitors_fn meta_window_get_work_area_all_monitors;
static meta_window_get_tile_area_fn meta_window_get_tile_area;
static meta_window_get_min_size_fn meta_window_get_min_size;
static meta_window_get_max_size_fn meta_window_get_max_size;
static meta_window_get_startup_id_fn meta_window_get_startup_id;
static meta_window_get_sandboxed_app_id_fn meta_window_get_sandboxed_app_id;
static meta_window_get_gtk_application_id_fn meta_window_get_gtk_application_id;
static meta_window_get_gtk_unique_bus_name_fn meta_window_get_gtk_unique_bus_name;
static meta_window_get_gtk_application_object_path_fn meta_window_get_gtk_application_object_path;
static meta_window_get_gtk_window_object_path_fn meta_window_get_gtk_window_object_path;
static meta_window_get_gtk_app_menu_object_path_fn meta_window_get_gtk_app_menu_object_path;
static meta_window_get_gtk_menubar_object_path_fn meta_window_get_gtk_menubar_object_path;
static meta_window_get_role_fn meta_window_get_role;
static meta_window_get_mutter_hints_fn meta_window_get_mutter_hints;
static meta_window_get_tag_fn meta_window_get_tag;
static meta_window_get_monitor_fn meta_window_get_monitor;
static meta_window_get_current_tile_monitor_number_fn meta_window_get_current_tile_monitor_number;
static meta_window_get_pid_fn meta_window_get_pid;
static meta_window_get_workspace_fn meta_window_get_workspace;
static meta_window_get_display_fn meta_window_get_display;
static meta_window_get_tile_match_fn meta_window_get_tile_match;
static meta_window_is_maximized_fn meta_window_is_maximized;
static meta_window_is_fullscreen_fn meta_window_is_fullscreen;
static meta_window_is_screen_sized_fn meta_window_is_screen_sized;
static meta_window_is_monitor_sized_fn meta_window_is_monitor_sized;
static meta_window_is_on_primary_monitor_fn meta_window_is_on_primary_monitor;
static meta_window_is_on_all_workspaces_fn meta_window_is_on_all_workspaces;
static meta_window_is_always_on_all_workspaces_fn meta_window_is_always_on_all_workspaces;
static meta_window_is_override_redirect_fn meta_window_is_override_redirect;
static meta_window_is_skip_taskbar_fn meta_window_is_skip_taskbar;
static meta_window_is_hidden_fn meta_window_is_hidden;
static meta_window_is_in_stack_fn meta_window_is_in_stack;
static meta_window_is_remote_fn meta_window_is_remote;
static meta_window_is_attached_dialog_fn meta_window_is_attached_dialog;
static meta_window_has_attached_dialogs_fn meta_window_has_attached_dialogs;
static meta_window_has_modals_fn meta_window_has_modals;
static meta_window_has_fullscreen_monitors_fn meta_window_has_fullscreen_monitors;
static meta_window_appears_focused_fn meta_window_appears_focused;
static meta_window_has_focus_fn meta_window_has_focus;
static meta_window_is_mapped_inhibited_fn meta_window_is_mapped_inhibited;
static meta_window_is_focusable_fn meta_window_is_focusable;
static meta_window_is_stackable_fn meta_window_is_stackable;
static meta_window_get_alive_fn meta_window_get_alive;
static meta_window_is_above_fn meta_window_is_above;
static meta_window_allows_move_fn meta_window_allows_move;
static meta_window_allows_resize_fn meta_window_allows_resize;
static meta_window_can_maximize_fn meta_window_can_maximize;
static meta_window_can_minimize_fn meta_window_can_minimize;
static meta_window_can_close_fn meta_window_can_close;
static meta_window_is_tiled_side_by_side_fn meta_window_is_tiled_side_by_side;
static meta_window_is_tiled_left_fn meta_window_is_tiled_left;
static meta_window_is_tiled_right_fn meta_window_is_tiled_right;
static meta_window_get_maximize_flags_fn meta_window_get_maximize_flags;
static meta_window_get_window_type_fn meta_window_get_window_type;
static meta_window_get_client_type_fn meta_window_get_client_type;
static meta_window_get_layer_fn meta_window_get_layer;
static meta_window_get_frame_type_fn meta_window_get_frame_type;
static meta_window_get_gravity_fn meta_window_get_gravity;
static meta_window_get_stable_sequence_fn meta_window_get_stable_sequence;
static meta_window_get_user_time_fn meta_window_get_user_time;
static meta_window_get_a11y_properties_fn meta_window_get_a11y_properties;
static meta_window_get_wayland_surface_fn meta_window_get_wayland_surface;
static meta_window_get_unit_cgroup_fn meta_window_get_unit_cgroup;

#define FUNCTION_ENTRY(name) do { \
    meta_window_##name = (typeof(meta_window_##name))dlsym(RTLD_DEFAULT, "meta_window_" #name); \
} while(0)

MUTTER_LIST_WINDOWS_API GSList* meta_display_list_window_detour(MetaDisplay* display, MetaListWindowsFlags flags, meta_display_list_windows_fn orig)
{
    if (!sInitialized)
    {
        initialize();
        sInitialized = true;
    }

    GSList* content = orig(display, flags);
    dump(content);
    return content;
}

static void initialize()
{
    // Load all symbols from the running mutter process
    void* handle = dlopen(NULL, RTLD_NOW | RTLD_NOLOAD);
    if (!handle) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return;
    }

    // Basic getters
    FUNCTION_ENTRY(get_id);
    FUNCTION_ENTRY(get_title);
    FUNCTION_ENTRY(get_description);
    FUNCTION_ENTRY(get_wm_class);
    FUNCTION_ENTRY(get_wm_class_instance);

    // Rect getters
    FUNCTION_ENTRY(get_buffer_rect);
    FUNCTION_ENTRY(get_frame_rect);
    FUNCTION_ENTRY(get_client_area_rect);
    FUNCTION_ENTRY(get_icon_geometry);
    FUNCTION_ENTRY(get_work_area_current_monitor);
    FUNCTION_ENTRY(get_work_area_for_monitor);
    FUNCTION_ENTRY(get_work_area_all_monitors);
    FUNCTION_ENTRY(get_tile_area);
    FUNCTION_ENTRY(get_min_size);
    FUNCTION_ENTRY(get_max_size);

    // String getters
    FUNCTION_ENTRY(get_startup_id);
    FUNCTION_ENTRY(get_sandboxed_app_id);
    FUNCTION_ENTRY(get_gtk_application_id);
    FUNCTION_ENTRY(get_gtk_unique_bus_name);
    FUNCTION_ENTRY(get_gtk_application_object_path);
    FUNCTION_ENTRY(get_gtk_window_object_path);
    FUNCTION_ENTRY(get_gtk_app_menu_object_path);
    FUNCTION_ENTRY(get_gtk_menubar_object_path);
    FUNCTION_ENTRY(get_role);
    FUNCTION_ENTRY(get_mutter_hints);
    FUNCTION_ENTRY(get_tag);

    // Integer/ID getters
    FUNCTION_ENTRY(get_monitor);
    FUNCTION_ENTRY(get_current_tile_monitor_number);
    FUNCTION_ENTRY(get_pid);
    FUNCTION_ENTRY(get_workspace);
    FUNCTION_ENTRY(get_display);
    FUNCTION_ENTRY(get_tile_match);

    // Boolean getters
    FUNCTION_ENTRY(is_maximized);
    FUNCTION_ENTRY(is_fullscreen);
    FUNCTION_ENTRY(is_screen_sized);
    FUNCTION_ENTRY(is_monitor_sized);
    FUNCTION_ENTRY(is_on_primary_monitor);
    FUNCTION_ENTRY(is_on_all_workspaces);
    FUNCTION_ENTRY(is_always_on_all_workspaces);
    FUNCTION_ENTRY(is_override_redirect);
    FUNCTION_ENTRY(is_skip_taskbar);
    FUNCTION_ENTRY(is_hidden);
    FUNCTION_ENTRY(is_in_stack);
    FUNCTION_ENTRY(is_remote);
    FUNCTION_ENTRY(is_attached_dialog);
    FUNCTION_ENTRY(has_attached_dialogs);
    FUNCTION_ENTRY(has_modals);
    FUNCTION_ENTRY(has_fullscreen_monitors);
    FUNCTION_ENTRY(appears_focused);
    FUNCTION_ENTRY(has_focus);
    FUNCTION_ENTRY(is_mapped_inhibited);
    FUNCTION_ENTRY(is_focusable);
    FUNCTION_ENTRY(is_stackable);
    FUNCTION_ENTRY(get_alive);
    FUNCTION_ENTRY(is_above);
    FUNCTION_ENTRY(allows_move);
    FUNCTION_ENTRY(allows_resize);
    FUNCTION_ENTRY(can_maximize);
    FUNCTION_ENTRY(can_minimize);
    FUNCTION_ENTRY(can_close);
    FUNCTION_ENTRY(is_tiled_side_by_side);
    FUNCTION_ENTRY(is_tiled_left);
    FUNCTION_ENTRY(is_tiled_right);

    // Enum getters
    FUNCTION_ENTRY(get_maximize_flags);
    FUNCTION_ENTRY(get_window_type);
    FUNCTION_ENTRY(get_client_type);
    FUNCTION_ENTRY(get_layer);
    FUNCTION_ENTRY(get_frame_type);
    FUNCTION_ENTRY(get_gravity);
    FUNCTION_ENTRY(get_stable_sequence);
    FUNCTION_ENTRY(get_user_time);
    FUNCTION_ENTRY(get_a11y_properties);
    FUNCTION_ENTRY(get_wayland_surface);
    FUNCTION_ENTRY(get_unit_cgroup);

    dlclose(handle);
}

// Helper to get window type name
static const char* window_type_name(MetaWindowType type) {
    switch (type) {
        case 0: return "NORMAL";
        case 1: return "DIALOG";
        case 2: return "MODAL_DIALOG";
        case 3: return "MENU";
        case 4: return "UTILITY";
        case 5: return "TOOLBAR";
        case 6: return "SPLASHSCREEN";
        case 7: return "DESKTOP";
        case 8: return "Dock";
        case 9: return "DROPDOWN_MENU";
        case 10: return "POPUP_MENU";
        case 11: return "TOOLTIP";
        case 12: return "NOTIFICATION";
        case 13: return "COMBO";
        case 14: return "DND";
        case 15: return "OVERIDE_OTHER";
        default: return "UNKNOWN";
    }
}

// Helper to get client type name
static const char* client_type_name(MetaWindowClientType type) {
    switch (type) {
        case 0: return "WAYLAND";
        case 1: return "X11";
        default: return "UNKNOWN";
    }
}

// Helper to get layer name
static const char* layer_name(MetaStackLayer layer) {
    switch (layer) {
        case 0: return "BELOW";
        case 1: return "NORMAL";
        case 2: return "TOP";
        case 3: return "FULLSCREEN";
        case 4: return "Above";
        case 5: return "Dock";
        case 6: return "Desktop";
        default: return "UNKNOWN";
    }
}

// Helper to get frame type name
static const char* frame_type_name(MetaFrameType type) {
    switch (type) {
        case 0: return "NORMAL";
        case 1: return "DIALOG";
        case 2: return "MODAL_DIALOG";
        case 3: return "MENU";
        case 4: return "UTILITY";
        case 5: return "BORDER";
        case 6: return "ATTACHED";
        default: return "NONE";
    }
}

// Helper to get gravity name
static const char* gravity_name(MetaGravity gravity) {
    switch (gravity) {
        case 0: return "NORTH_WEST";
        case 1: return "NORTH";
        case 2: return "NORTH_EAST";
        case 3: return "WEST";
        case 4: return "EAST";
        case 5: return "SOUTH_WEST";
        case 6: return "SOUTH";
        case 7: return "SOUTH_EAST";
        case 8: return "CENTER";
        default: return "UNKNOWN";
    }
}

static void dump(GSList* windows)
{
    GSList *l;

    char path_tmp[128];
    sprintf(path_tmp, "/run/user/%d/dump_tmp.txt", getuid());

    char path_dmp[128];
    sprintf(path_dmp, "/run/user/%d/dump.txt", getuid());

    {
        FILE* f = fopen(path_tmp, "w");
        if (!f) {
            return;
        }

        for (l = windows; l; l = l->next)
        {
            MetaWindow window = l->data;
            if (!window) continue;

            // Get window ID - primary identifier
            uint64_t id = meta_window_get_id(window);
            
            fprintf(f, "=== WINDOW: %lu ===\n", (unsigned long)id);
            
            // --- Basic info ---
            if (meta_window_get_title) fprintf(f, "title: %s\n", meta_window_get_title(window));
            if (meta_window_get_description) fprintf(f, "description: %s\n", meta_window_get_description(window));
            if (meta_window_get_wm_class) fprintf(f, "wm_class: %s\n", meta_window_get_wm_class(window));
            if (meta_window_get_wm_class_instance) fprintf(f, "wm_class_instance: %s\n", meta_window_get_wm_class_instance(window));
            if (meta_window_get_role) fprintf(f, "role: %s\n", meta_window_get_role(window));
            if (meta_window_get_tag) fprintf(f, "tag: %s\n", meta_window_get_tag(window));
            if (meta_window_get_startup_id) fprintf(f, "startup_id: %s\n", meta_window_get_startup_id(window));
            if (meta_window_get_mutter_hints) fprintf(f, "mutter_hints: %s\n", meta_window_get_mutter_hints(window));
            if (meta_window_get_sandboxed_app_id) fprintf(f, "sandboxed_app_id: %s\n", meta_window_get_sandboxed_app_id(window));
            if (meta_window_get_gtk_application_id) fprintf(f, "gtk_application_id: %s\n", meta_window_get_gtk_application_id(window));
            if (meta_window_get_gtk_unique_bus_name) fprintf(f, "gtk_unique_bus_name: %s\n", meta_window_get_gtk_unique_bus_name(window));
            if (meta_window_get_gtk_application_object_path) fprintf(f, "gtk_application_object_path: %s\n", meta_window_get_gtk_application_object_path(window));
            if (meta_window_get_gtk_window_object_path) fprintf(f, "gtk_window_object_path: %s\n", meta_window_get_gtk_window_object_path(window));
            if (meta_window_get_gtk_app_menu_object_path) fprintf(f, "gtk_app_menu_object_path: %s\n", meta_window_get_gtk_app_menu_object_path(window));
            if (meta_window_get_gtk_menubar_object_path) fprintf(f, "gtk_menubar_object_path: %s\n", meta_window_get_gtk_menubar_object_path(window));

            // --- Type info ---
            if (meta_window_get_window_type) fprintf(f, "window_type: %s\n", window_type_name(meta_window_get_window_type(window)));
            if (meta_window_get_client_type) fprintf(f, "client_type: %s\n", client_type_name(meta_window_get_client_type(window)));
            if (meta_window_get_layer) fprintf(f, "layer: %s\n", layer_name(meta_window_get_layer(window)));
            if (meta_window_get_frame_type) fprintf(f, "frame_type: %s\n", frame_type_name(meta_window_get_frame_type(window)));
            if (meta_window_get_gravity) fprintf(f, "gravity: %s\n", gravity_name(meta_window_get_gravity(window)));

            // --- Geometry ---
            if (meta_window_get_monitor != NULL) fprintf(f, "monitor: %d\n", meta_window_get_monitor(window));
            if (meta_window_get_current_tile_monitor_number != NULL) fprintf(f, "tile_monitor: %d\n", meta_window_get_current_tile_monitor_number(window));
            
            if (meta_window_get_frame_rect) {
                struct { int x, y, width, height; } rect;
                meta_window_get_frame_rect(window, &rect);
                fprintf(f, "frame_rect: %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
            }
            if (meta_window_get_buffer_rect) {
                struct { int x, y, width, height; } rect;
                meta_window_get_buffer_rect(window, &rect);
                fprintf(f, "buffer_rect: %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
            }
            if (meta_window_get_client_area_rect) {
                struct { int x, y, width, height; } rect;
                meta_window_get_client_area_rect(window, &rect);
                fprintf(f, "client_area_rect: %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
            }
            if (meta_window_get_work_area_current_monitor) {
                struct { int x, y, width, height; } rect;
                meta_window_get_work_area_current_monitor(window, &rect);
                fprintf(f, "work_area_current_monitor: %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
            }
            if (meta_window_get_work_area_all_monitors) {
                struct { int x, y, width, height; } rect;
                meta_window_get_work_area_all_monitors(window, &rect);
                fprintf(f, "work_area_all_monitors: %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
            }
            if (meta_window_get_icon_geometry) {
                struct { int x, y, width, height; } rect;
                if (meta_window_get_icon_geometry(window, &rect))
                    fprintf(f, "icon_geometry: %d %d %d %d\n", rect.x, rect.y, rect.width, rect.height);
            }

            // --- Size hints ---
            if (meta_window_get_min_size) {
                int min_w, min_h;
                if (meta_window_get_min_size(window, &min_w, &min_h))
                    fprintf(f, "min_size: %d x %d\n", min_w, min_h);
            }
            if (meta_window_get_max_size) {
                int max_w, max_h;
                if (meta_window_get_max_size(window, &max_w, &max_h))
                    fprintf(f, "max_size: %d x %d\n", max_w, max_h);
            }

            // --- State flags ---
            if (meta_window_is_maximized) fprintf(f, "maximized: %d\n", meta_window_is_maximized(window));
            if (meta_window_get_maximize_flags) fprintf(f, "maximize_flags: %d\n", meta_window_get_maximize_flags(window));
            if (meta_window_is_fullscreen) fprintf(f, "fullscreen: %d\n", meta_window_is_fullscreen(window));
            if (meta_window_is_screen_sized) fprintf(f, "screen_sized: %d\n", meta_window_is_screen_sized(window));
            if (meta_window_is_monitor_sized) fprintf(f, "monitor_sized: %d\n", meta_window_is_monitor_sized(window));
            if (meta_window_is_above) fprintf(f, "above: %d\n", meta_window_is_above(window));

            // --- Tiling ---
            if (meta_window_is_tiled_side_by_side) fprintf(f, "tiled_side_by_side: %d\n", meta_window_is_tiled_side_by_side(window));
            if (meta_window_is_tiled_left) fprintf(f, "tiled_left: %d\n", meta_window_is_tiled_left(window));
            if (meta_window_is_tiled_right) fprintf(f, "tiled_right: %d\n", meta_window_is_tiled_right(window));

            // --- Workspace ---
            if (meta_window_is_on_all_workspaces) fprintf(f, "on_all_workspaces: %d\n", meta_window_is_on_all_workspaces(window));
            if (meta_window_is_always_on_all_workspaces) fprintf(f, "always_on_all_workspaces: %d\n", meta_window_is_always_on_all_workspaces(window));

            // --- Focus ---
            if (meta_window_appears_focused) fprintf(f, "appears_focused: %d\n", meta_window_appears_focused(window));
            if (meta_window_has_focus) fprintf(f, "has_focus: %d\n", meta_window_has_focus(window));

            // --- Capability flags ---
            if (meta_window_can_maximize) fprintf(f, "can_maximize: %d\n", meta_window_can_maximize(window));
            if (meta_window_can_minimize) fprintf(f, "can_minimize: %d\n", meta_window_can_minimize(window));
            if (meta_window_can_close) fprintf(f, "can_close: %d\n", meta_window_can_close(window));
            if (meta_window_allows_move) fprintf(f, "allows_move: %d\n", meta_window_allows_move(window));
            if (meta_window_allows_resize) fprintf(f, "allows_resize: %d\n", meta_window_allows_resize(window));

            // --- Skip hints ---
            if (meta_window_is_override_redirect) fprintf(f, "override_redirect: %d\n", meta_window_is_override_redirect(window));
            if (meta_window_is_skip_taskbar) fprintf(f, "skip_taskbar: %d\n", meta_window_is_skip_taskbar(window));
            if (meta_window_is_hidden) fprintf(f, "hidden: %d\n", meta_window_is_hidden(window));

            // --- Identity ---
            if (meta_window_get_pid) fprintf(f, "pid: %d\n", meta_window_get_pid(window));
            if (meta_window_get_stable_sequence) fprintf(f, "stable_sequence: %u\n", (unsigned)meta_window_get_stable_sequence(window));
            if (meta_window_get_user_time) fprintf(f, "user_time: %u\n", (unsigned)meta_window_get_user_time(window));
            if (meta_window_is_remote) fprintf(f, "remote: %d\n", meta_window_is_remote(window));
            if (meta_window_get_alive) fprintf(f, "alive: %d\n", meta_window_get_alive(window));
            if (meta_window_is_in_stack) fprintf(f, "in_stack: %d\n", meta_window_is_in_stack(window));

            // --- Dialog state ---
            if (meta_window_is_attached_dialog) fprintf(f, "attached_dialog: %d\n", meta_window_is_attached_dialog(window));
            if (meta_window_has_fullscreen_monitors) fprintf(f, "has_fullscreen_monitors: %d\n", meta_window_has_fullscreen_monitors(window));

            // --- Other ---
            if (meta_window_is_mapped_inhibited) fprintf(f, "mapped_inhibited: %d\n", meta_window_is_mapped_inhibited(window));
            if (meta_window_is_focusable) fprintf(f, "focusable: %d\n", meta_window_is_focusable(window));
            if (meta_window_is_stackable) fprintf(f, "stackable: %d\n", meta_window_is_stackable(window));

            fprintf(f, "\n");
        }
        fclose(f);
    }

    rename(path_tmp, path_dmp);
}
