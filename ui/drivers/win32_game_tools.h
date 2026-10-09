#ifndef RETROARCH_GAME_TOOLS_WIN32_H
#define RETROARCH_GAME_TOOLS_WIN32_H
#include <windows.h>
#include <boolean.h>

void win32_game_tools_show(HWND owner, bool saves);
bool win32_game_tools_process_message(MSG *message);
void win32_game_tools_destroy(void);
#endif
