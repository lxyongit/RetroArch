/* Native desktop game tools, shared by Cocoa and Win32. */
#ifndef RARCH_DESKTOP_GAME_TOOLS_H
#define RARCH_DESKTOP_GAME_TOOLS_H

#include <stddef.h>
#include <boolean.h>
#include <retro_common_api.h>
#include <retro_miscellaneous.h>

#if ((defined(HAVE_COCOA) || defined(HAVE_COCOA_METAL)) && \
     !defined(HAVE_COCOATOUCH)) || \
    (defined(_WIN32) && !defined(_XBOX) && !defined(__WINRT__))
#define HAVE_DESKTOP_GAME_TOOLS 1
#endif

RETRO_BEGIN_DECLS

typedef struct desktop_cheat_option
{
   char *name;
   char *code;
   size_t value_index;
} desktop_cheat_option_t;

typedef struct desktop_cheat
{
   char *title;
   char *code;
   desktop_cheat_option_t *options;
   size_t num_options;
   size_t index;
   size_t off_index;
   int selected;
   bool enabled;
} desktop_cheat_t;

typedef struct desktop_save_slot
{
   char path[PATH_MAX_LENGTH];
   char thumbnail[PATH_MAX_LENGTH];
   bool exists;
} desktop_save_slot_t;

/* Prepare FBNeo's definition before retro_load_game(), then install JSON
 * cheats after RetroArch has loaded its existing per-game CHT choices. */
void desktop_game_tools_prepare_content(const char *path);
void desktop_game_tools_init_cheats(void);
unsigned desktop_game_tools_generation(void);
bool desktop_game_tools_running(void);
bool desktop_game_tools_is_arcade(void);
const char *desktop_game_tools_error(void);
const char *desktop_game_tools_cheat_error(void);
const desktop_cheat_t *desktop_game_tools_cheats(size_t *count);
bool desktop_game_tools_set_cheat(size_t index, int choice);
bool desktop_game_tools_save_slot(unsigned slot, desktop_save_slot_t *state);
bool desktop_game_tools_write_save(unsigned slot);
void desktop_game_tools_capture_thumbnail(unsigned slot);
bool desktop_game_tools_load_save(unsigned slot);
bool desktop_game_tools_delete_save(unsigned slot);
bool desktop_game_tools_save_busy(void);

RETRO_END_DECLS
#endif
