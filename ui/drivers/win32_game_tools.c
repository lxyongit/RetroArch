/* RetroArch native game panels. Licensed under GPLv3 or later. */
#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <compat/posix_string.h>
#include <encodings/utf.h>
#include <formats/image.h>
#include "win32_game_tools.h"
#include "../desktop_game_tools.h"

#if defined(__WINRT__) || defined(_XBOX)
void win32_game_tools_show(HWND owner, bool saves)
{
   (void)owner;
   (void)saves;
}
bool win32_game_tools_process_message(MSG *message)
{
   (void)message;
   return false;
}
void win32_game_tools_destroy(void) { }
#else

enum { DGTW_LIST = 6100, DGTW_PICKER, DGTW_TOGGLE,
       DGTW_LOAD, DGTW_WRITE, DGTW_DELETE };

typedef struct dgtw_panel
{
   HWND window, list, status, picker, toggle, preview, placeholder, time;
   HFONT font;
   HBITMAP bitmap;
   unsigned generation;
   int selected, dpi, pending_slot;
   FILETIME pending_modified;
   DWORD pending_deadline;
   bool saves, was_running;
} dgtw_panel_t;

static dgtw_panel_t dgtw_saves, dgtw_cheats;
static const wchar_t dgtw_class[] = L"RetroArchGameTools";

static void dgtw_text(HWND control, const char *text)
{
   wchar_t *wide = utf8_to_utf16_string_alloc(text ? text : "");
   if (wide)
   {
      SetWindowTextW(control, wide);
      free(wide);
   }
}

static void dgtw_add(HWND list, const char *text)
{
   wchar_t *wide = utf8_to_utf16_string_alloc(text ? text : "");
   if (wide)
   {
      SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wide);
      free(wide);
   }
}

static HWND dgtw_control(dgtw_panel_t *panel, const wchar_t *type,
      const wchar_t *title, DWORD style, int x, int y, int w, int h, int id)
{
   HWND control = CreateWindowExW(0, type, title, WS_CHILD | WS_VISIBLE | style,
         MulDiv(x, panel->dpi, 96), MulDiv(y, panel->dpi, 96),
         MulDiv(w, panel->dpi, 96), MulDiv(h, panel->dpi, 96),
         panel->window, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
   SendMessageW(control, WM_SETFONT, (WPARAM)panel->font, TRUE);
   return control;
}

static FILETIME dgtw_modified(const char *path)
{
   FILETIME time = {0, 0};
   WIN32_FILE_ATTRIBUTE_DATA data;
   wchar_t *wide = utf8_to_utf16_string_alloc(path);
   if (wide && GetFileAttributesExW(wide, GetFileExInfoStandard, &data))
      time = data.ftLastWriteTime;
   free(wide);
   return time;
}

static void dgtw_preview(dgtw_panel_t *panel)
{
   desktop_save_slot_t state;
   FILETIME modified, local;
   SYSTEMTIME time;
   char caption[128];
   struct texture_image image;
   BITMAPINFO info;
   void *pixels;
   HDC dc, target;
   HGDIOBJ old;
   int selected = (int)SendMessageW(panel->list, LB_GETCURSEL, 0, 0);
   int w = MulDiv(160, panel->dpi, 96), h = MulDiv(100, panel->dpi, 96);
   SendMessageW(panel->preview, STM_SETIMAGE, IMAGE_BITMAP, 0);
   if (panel->bitmap)
      DeleteObject(panel->bitmap);
   panel->bitmap = NULL;
   ShowWindow(panel->placeholder, SW_SHOWNA);
   SetWindowTextW(panel->time, L"\u4fdd\u5b58\u65f6\u95f4\uff1a\u672a\u4fdd\u5b58");
   memset(&state, 0, sizeof(state));
   if (selected >= 0)
      desktop_game_tools_save_slot((unsigned)selected, &state);
   EnableWindow(GetDlgItem(panel->window, DGTW_WRITE),
         selected >= 0 && desktop_game_tools_running() && !desktop_game_tools_save_busy());
   EnableWindow(GetDlgItem(panel->window, DGTW_LOAD),
         state.exists && !desktop_game_tools_save_busy());
   EnableWindow(GetDlgItem(panel->window, DGTW_DELETE),
         state.exists && !desktop_game_tools_save_busy());
   if (!state.exists)
      return;
   modified = dgtw_modified(state.path);
   if (FileTimeToLocalFileTime(&modified, &local) && FileTimeToSystemTime(&local, &time))
   {
      snprintf(caption, sizeof(caption),
            "%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth,
            time.wDay, time.wHour, time.wMinute, time.wSecond);
      dgtw_text(panel->time, caption);
   }
   memset(&image, 0, sizeof(image));
   if (!image_texture_load(&image, state.thumbnail) || !image.pixels ||
         !image.width || !image.height)
   {
      image_texture_free(&image);
      return;
   }
   memset(&info, 0, sizeof(info));
   info.bmiHeader.biSize = sizeof(info.bmiHeader);
   info.bmiHeader.biWidth = w;
   info.bmiHeader.biHeight = -h;
   info.bmiHeader.biPlanes = 1;
   info.bmiHeader.biBitCount = 32;
   info.bmiHeader.biCompression = BI_RGB;
   dc = GetDC(panel->preview);
   panel->bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
   target = CreateCompatibleDC(dc);
   if (panel->bitmap && target)
   {
      int draw_w = w, draw_h = h;
      RECT rect = {0, 0, w, h};
      old = SelectObject(target, panel->bitmap);
      FillRect(target, &rect, (HBRUSH)(COLOR_WINDOW + 1));
      if ((double)image.width / image.height > (double)w / h)
         draw_h = (int)((double)w * image.height / image.width);
      else
         draw_w = (int)((double)h * image.width / image.height);
      info.bmiHeader.biWidth = (LONG)image.width;
      info.bmiHeader.biHeight = -(LONG)image.height;
      SetStretchBltMode(target, HALFTONE);
      StretchDIBits(target, (w - draw_w) / 2, (h - draw_h) / 2, draw_w, draw_h,
            0, 0, image.width, image.height, image.pixels, &info, DIB_RGB_COLORS, SRCCOPY);
      SelectObject(target, old);
      ShowWindow(panel->placeholder, SW_HIDE);
      SendMessageW(panel->preview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)panel->bitmap);
   }
   if (target)
      DeleteDC(target);
   ReleaseDC(panel->preview, dc);
   image_texture_free(&image);
}

static void dgtw_picker(dgtw_panel_t *panel)
{
   size_t count, j;
   const desktop_cheat_t *cheats = desktop_game_tools_cheats(&count);
   int selected = (int)SendMessageW(panel->list, LB_GETCURSEL, 0, 0);
   ShowWindow(panel->picker, SW_HIDE);
   ShowWindow(panel->toggle, SW_HIDE);
   if (selected < 0 || (size_t)selected >= count)
      return;
   panel->selected = selected;
   if (!cheats[selected].num_options)
   {
      SendMessageW(panel->toggle, BM_SETCHECK,
            cheats[selected].enabled ? BST_CHECKED : BST_UNCHECKED, 0);
      ShowWindow(panel->toggle, SW_SHOWNA);
      return;
   }
   SendMessageW(panel->picker, LB_RESETCONTENT, 0, 0);
   SendMessageW(panel->picker, LB_ADDSTRING, 0, (LPARAM)L"\u5173\u95ed");
   for (j = 0; j < cheats[selected].num_options; j++)
      dgtw_add(panel->picker, cheats[selected].options[j].name);
   SendMessageW(panel->picker, LB_SETCURSEL,
         cheats[selected].enabled ? cheats[selected].selected + 1 : 0, 0);
   ShowWindow(panel->picker, SW_SHOWNA);
}

static void dgtw_refresh(dgtw_panel_t *panel)
{
   size_t count = 0, i;
   const desktop_cheat_t *cheats;
   int selected = (int)SendMessageW(panel->list, LB_GETCURSEL, 0, 0);
   if (panel->generation != desktop_game_tools_generation())
      selected = 0;
   panel->generation = desktop_game_tools_generation();
   panel->was_running = desktop_game_tools_running();
   SendMessageW(panel->list, LB_RESETCONTENT, 0, 0);
   if (panel->saves)
   {
      if (panel->was_running)
         for (i = 0; i < 10; i++)
         {
            desktop_save_slot_t state;
            char caption[96];
            desktop_game_tools_save_slot((unsigned)i, &state);
            snprintf(caption, sizeof(caption), state.exists
                  ? "即时存档 · 槽位 %u" : "空槽位 · 槽位 %u", (unsigned)i);
            dgtw_add(panel->list, caption);
         }
      SendMessageW(panel->list, LB_SETCURSEL, selected < 0 ? 0 : selected, 0);
      dgtw_preview(panel);
   }
   else
   {
      cheats = desktop_game_tools_cheats(&count);
      for (i = 0; i < count; i++)
         dgtw_add(panel->list, cheats[i].title);
      SendMessageW(panel->list, LB_SETCURSEL,
            selected >= 0 && (size_t)selected < count ? selected : 0, 0);
      dgtw_picker(panel);
   }
   dgtw_text(panel->status, !panel->was_running ? "请先启动游戏。"
         : (!panel->saves && *desktop_game_tools_cheat_error()
            ? desktop_game_tools_cheat_error() : (!panel->saves && !count
               ? "当前游戏没有可用金手指。" : "")));
}

static void dgtw_action(dgtw_panel_t *panel, unsigned action)
{
   int selected = (int)SendMessageW(panel->list, LB_GETCURSEL, 0, 0);
   desktop_save_slot_t state;
   bool ok;
   if (panel->generation != desktop_game_tools_generation())
   {
      dgtw_refresh(panel);
      return;
   }
   if (panel->pending_slot >= 0)
   {
      dgtw_text(panel->status, "正在保存存档，请稍后再试。");
      return;
   }
   if (selected < 0 || !desktop_game_tools_save_slot((unsigned)selected, &state))
      return;
   if (state.exists && action != DGTW_LOAD &&
         MessageBoxW(panel->window, action == DGTW_DELETE
               ? L"\u786e\u8ba4\u5220\u9664\u9009\u4e2d\u69fd\u4f4d\u7684\u5b58\u6863\u5417\uff1f\u6b64\u64cd\u4f5c\u65e0\u6cd5\u64a4\u9500\u3002"
               : L"\u786e\u8ba4\u7528\u5f53\u524d\u6e38\u620f\u8fdb\u5ea6\u8986\u76d6\u9009\u4e2d\u69fd\u4f4d\u5417\uff1f\u6b64\u64cd\u4f5c\u65e0\u6cd5\u64a4\u9500\u3002",
               action == DGTW_DELETE ? L"\u5220\u9664\u5b58\u6863" : L"\u8986\u76d6\u5b58\u6863",
               MB_OKCANCEL | MB_ICONQUESTION | MB_TOPMOST) != IDOK)
      return;
   panel->pending_modified = dgtw_modified(state.path);
   ok = action == DGTW_LOAD ? desktop_game_tools_load_save((unsigned)selected)
         : (action == DGTW_DELETE ? desktop_game_tools_delete_save((unsigned)selected)
            : desktop_game_tools_write_save((unsigned)selected));
   dgtw_refresh(panel);
   dgtw_text(panel->status, ok ? (action == DGTW_WRITE ? "正在保存…"
         : (action == DGTW_LOAD ? "已请求读取存档。" : "已删除存档。"))
         : desktop_game_tools_error());
   if (ok && action == DGTW_WRITE)
   {
      panel->pending_slot = selected;
      panel->pending_deadline = GetTickCount() + 8000;
   }
}

static LRESULT CALLBACK dgtw_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
   dgtw_panel_t *panel = (dgtw_panel_t*)GetWindowLongPtrW(window, GWLP_USERDATA);
   if (message == WM_CREATE)
   {
      panel = (dgtw_panel_t*)((CREATESTRUCTW*)lparam)->lpCreateParams;
      panel->window = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)panel);
      panel->font = CreateFontW(-MulDiv(10, panel->dpi, 72), 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      dgtw_control(panel, L"STATIC", panel->saves ? L"\u5373\u65f6\u5b58\u6863" : L"\u91d1\u624b\u6307",
            0, 20, 18, 190, 24, 0);
      dgtw_control(panel, L"STATIC", panel->saves
            ? L"\u56fa\u5b9a 10 \u4e2a\u69fd\u4f4d\uff080\u20139\uff09"
            : L"\u4fee\u6539\u540e\u7acb\u5373\u5728\u6e38\u620f\u4e2d\u751f\u6548",
            0, 330, 18, 300, 24, 0);
      panel->status = dgtw_control(panel, L"STATIC", L"", 0, 20, 48, 600, 24, 0);
      panel->list = dgtw_control(panel, L"LISTBOX", L"",
            WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            20, 76, panel->saves ? 410 : 620, panel->saves ? 350 : 270, DGTW_LIST);
      if (panel->saves)
      {
         panel->preview = dgtw_control(panel, L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE,
               465, 100, 160, 100, 0);
         panel->placeholder = dgtw_control(panel, L"STATIC", L"\u6682\u65e0\u7f29\u7565\u56fe",
               SS_CENTER | SS_CENTERIMAGE, 465, 100, 160, 100, 0);
         dgtw_control(panel, L"STATIC", L"\u5b58\u6863\u7f29\u7565\u56fe",
               0, 465, 76, 160, 20, 0);
         panel->time = dgtw_control(panel, L"STATIC", L"", 0, 465, 214, 170, 48, 0);
         dgtw_control(panel, L"BUTTON", L"\u8bfb\u53d6", WS_TABSTOP,
               120, 448, 90, 32, DGTW_LOAD);
         dgtw_control(panel, L"BUTTON", L"\u4fdd\u5b58 / \u8986\u76d6", WS_TABSTOP,
               230, 448, 110, 32, DGTW_WRITE);
         dgtw_control(panel, L"BUTTON", L"\u5220\u9664", WS_TABSTOP,
               360, 448, 90, 32, DGTW_DELETE);
      }
      else
      {
         dgtw_control(panel, L"STATIC", L"\u5f53\u524d\u9009\u9879", 0, 20, 370, 100, 24, 0);
         panel->picker = dgtw_control(panel, L"LISTBOX", L"",
               WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
               130, 366, 470, 110, DGTW_PICKER);
         panel->toggle = dgtw_control(panel, L"BUTTON", L"\u542f\u7528", WS_TABSTOP | BS_AUTOCHECKBOX,
               510, 366, 90, 28, DGTW_TOGGLE);
      }
      SetTimer(window, 1, 250, NULL);
      dgtw_refresh(panel);
      return 0;
   }
   if (!panel)
      return DefWindowProcW(window, message, wparam, lparam);
   switch (message)
   {
      case WM_COMMAND:
         if (panel->generation != desktop_game_tools_generation())
         {
            dgtw_refresh(panel);
            return 0;
         }
         if (LOWORD(wparam) == DGTW_LIST && HIWORD(wparam) == LBN_SELCHANGE)
         {
            if (panel->saves)
               dgtw_preview(panel);
            else
               dgtw_picker(panel);
         }
         else if (LOWORD(wparam) >= DGTW_LOAD && LOWORD(wparam) <= DGTW_DELETE)
            dgtw_action(panel, LOWORD(wparam));
         else if ((LOWORD(wparam) == DGTW_PICKER && HIWORD(wparam) == LBN_SELCHANGE) ||
               (LOWORD(wparam) == DGTW_TOGGLE && HIWORD(wparam) == BN_CLICKED))
         {
            int choice = LOWORD(wparam) == DGTW_PICKER
                  ? (int)SendMessageW(panel->picker, LB_GETCURSEL, 0, 0) - 1
                  : (SendMessageW(panel->toggle, BM_GETCHECK, 0, 0) == BST_CHECKED ? 0 : -1);
            bool ok = desktop_game_tools_set_cheat((size_t)panel->selected, choice);
            dgtw_refresh(panel);
            if (!ok)
               dgtw_text(panel->status, desktop_game_tools_error());
         }
         return 0;
      case WM_TIMER:
         if (panel->generation != desktop_game_tools_generation() ||
               panel->was_running != desktop_game_tools_running())
         {
            panel->pending_slot = -1;
            dgtw_refresh(panel);
         }
         if (panel->pending_slot >= 0 && !desktop_game_tools_save_busy())
         {
            desktop_save_slot_t state;
            FILETIME modified;
            bool updated = desktop_game_tools_save_slot((unsigned)panel->pending_slot, &state) && state.exists;
            modified = dgtw_modified(state.path);
            updated = updated && CompareFileTime(&modified, &panel->pending_modified) != 0;
            if (updated || (LONG)(GetTickCount() - panel->pending_deadline) >= 0)
            {
               if (updated)
                  desktop_game_tools_capture_thumbnail((unsigned)panel->pending_slot);
               panel->pending_slot = -1;
               dgtw_refresh(panel);
               dgtw_text(panel->status, updated ? "已保存存档。" : "未能创建存档，请查看 RetroArch 的错误提示。");
            }
         }
         return 0;
      case WM_CLOSE:
         ShowWindow(window, SW_HIDE);
         return 0;
      case WM_DESTROY:
         KillTimer(window, 1);
         if (panel->bitmap)
            DeleteObject(panel->bitmap);
         if (panel->font)
            DeleteObject(panel->font);
         memset(panel, 0, sizeof(*panel));
         return 0;
   }
   return DefWindowProcW(window, message, wparam, lparam);
}

void win32_game_tools_show(HWND owner, bool saves)
{
   dgtw_panel_t *panel = saves ? &dgtw_saves : &dgtw_cheats;
   if (!panel->window)
   {
      WNDCLASSW type;
      HDC dc = GetDC(owner);
      RECT rect;
      memset(&type, 0, sizeof(type));
      type.lpfnWndProc = dgtw_proc;
      type.hInstance = GetModuleHandle(NULL);
      type.hCursor = LoadCursor(NULL, IDC_ARROW);
      type.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
      type.lpszClassName = dgtw_class;
      RegisterClassW(&type);
      panel->saves = saves;
      panel->pending_slot = -1;
      panel->dpi = GetDeviceCaps(dc, LOGPIXELSX);
      ReleaseDC(owner, dc);
      rect.left = rect.top = 0;
      rect.right = MulDiv(660, panel->dpi, 96);
      rect.bottom = MulDiv(500, panel->dpi, 96);
      AdjustWindowRectEx(&rect, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
      panel->window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            dgtw_class, saves ? L"\u5b58\u6863\u7ba1\u7406" : L"\u91d1\u624b\u6307",
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top, owner, NULL,
            GetModuleHandle(NULL), panel);
   }
   if (panel->window)
   {
      dgtw_refresh(panel);
      ShowWindow(panel->window, SW_SHOW);
      SetForegroundWindow(panel->window);
   }
}

bool win32_game_tools_process_message(MSG *message)
{
   return (dgtw_saves.window && IsWindowVisible(dgtw_saves.window) &&
         IsDialogMessageW(dgtw_saves.window, message)) ||
         (dgtw_cheats.window && IsWindowVisible(dgtw_cheats.window) &&
          IsDialogMessageW(dgtw_cheats.window, message));
}

void win32_game_tools_destroy(void)
{
   if (dgtw_saves.window)
      DestroyWindow(dgtw_saves.window);
   if (dgtw_cheats.window)
      DestroyWindow(dgtw_cheats.window);
}
#endif
