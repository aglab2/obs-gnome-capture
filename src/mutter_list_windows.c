#include <glib.h>
#include <stdio.h>

#define MUTTER_LIST_WINDOWS_API __attribute__((visibility("default")))

typedef void* MetaDisplay;
typedef int MetaListWindowsFlags;

typedef GSList* meta_display_list_windows_fn(MetaDisplay* display, MetaListWindowsFlags flags);

MUTTER_LIST_WINDOWS_API GSList* meta_display_list_window_detour(MetaDisplay* display, MetaListWindowsFlags flags, meta_display_list_windows_fn orig)
{
    GSList* content = orig(display, flags);
    printf("%p\n", content);
    return content;
}
