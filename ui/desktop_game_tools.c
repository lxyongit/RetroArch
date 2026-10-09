/* RetroArch native desktop game tools. Licensed under GPLv3 or later. */
#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <compat/strl.h>
#include <compat/posix_string.h>
#include <file/file_path.h>
#include <formats/rjson.h>
#include <streams/file_stream.h>
#include <string/stdstring.h>

#include "desktop_game_tools.h"
#include "../configuration.h"
#include "../paths.h"
#include "../core.h"
#include "../runloop.h"
#include "../content.h"
#include "../command.h"
#include "../tasks/tasks_internal.h"
#include "../gfx/video_driver.h"
#include "../verbosity.h"
#ifdef HAVE_CHEATS
#include "../cheat_manager.h"
#endif

typedef struct dgt_json
{
   enum rjson_type type;
   char *key;
   char *text;
   struct dgt_json *child;
   struct dgt_json *next;
} dgt_json_t;

static desktop_cheat_t *dgt_cheats;
static size_t dgt_count;
static unsigned dgt_generation;
static bool dgt_arcade;
static bool dgt_cheats_ready;
static char dgt_content[PATH_MAX_LENGTH];
static char dgt_error[256];
static char dgt_cheat_error[256];

static bool dgt_fail(const char *message)
{
   strlcpy(dgt_error, message, sizeof(dgt_error));
   return false;
}

static bool dgt_cheat_fail(const char *message)
{
   strlcpy(dgt_cheat_error, message, sizeof(dgt_cheat_error));
   return dgt_fail(message);
}

static char *dgt_trimmed(const char *text)
{
   size_t length;
   char *copy;
   if (!text)
      text = "";
   while (*text && isspace((unsigned char)*text))
      text++;
   length = strlen(text);
   while (length && isspace((unsigned char)text[length - 1]))
      length--;
   copy = (char*)malloc(length + 1);
   if (copy)
   {
      memcpy(copy, text, length);
      copy[length] = '\0';
   }
   return copy;
}

static void dgt_free_cheats(void)
{
   size_t i, j;
   for (i = 0; i < dgt_count; i++)
   {
      free(dgt_cheats[i].title);
      free(dgt_cheats[i].code);
      for (j = 0; j < dgt_cheats[i].num_options; j++)
      {
         free(dgt_cheats[i].options[j].name);
         free(dgt_cheats[i].options[j].code);
      }
      free(dgt_cheats[i].options);
   }
   free(dgt_cheats);
   dgt_cheats = NULL;
   dgt_count  = 0;
}

static desktop_cheat_t *dgt_add_cheat(const char *title)
{
   desktop_cheat_t *items = (desktop_cheat_t*)realloc(dgt_cheats,
         (dgt_count + 1) * sizeof(*items));
   desktop_cheat_t *cheat;
   if (!items)
      return NULL;
   dgt_cheats = items;
   cheat     = &items[dgt_count++];
   memset(cheat, 0, sizeof(*cheat));
   cheat->title    = dgt_trimmed(title ? title : "Cheat");
   cheat->selected = -1;
   return cheat;
}

static bool dgt_add_option(desktop_cheat_t *cheat, const char *name,
      const char *code, size_t value_index)
{
   desktop_cheat_option_t *items = (desktop_cheat_option_t*)realloc(
         cheat->options, (cheat->num_options + 1) * sizeof(*items));
   desktop_cheat_option_t *option;
   if (!items)
      return false;
   cheat->options = items;
   option = &items[cheat->num_options++];
   option->name        = dgt_trimmed(name ? name : "Option");
   option->code        = strdup(code ? code : "");
   option->value_index = value_index;
   return option->name && option->code;
}

static void dgt_json_free(dgt_json_t *node)
{
   while (node)
   {
      dgt_json_t *next = node->next;
      dgt_json_free(node->child);
      free(node->key);
      free(node->text);
      free(node);
      node = next;
   }
}

static dgt_json_t *dgt_json_read(rjson_t *parser, enum rjson_type type,
      unsigned depth)
{
   dgt_json_t *node, **tail;
   if (depth > 32 || type == RJSON_ERROR || type == RJSON_DONE)
      return NULL;
   node = (dgt_json_t*)calloc(1, sizeof(*node));
   if (!node)
      return NULL;
   node->type = type;
   if (type == RJSON_STRING || type == RJSON_NUMBER)
   {
      node->text = strdup(rjson_get_string(parser, NULL));
      if (!node->text)
         goto error;
   }
   tail = &node->child;
   if (type == RJSON_OBJECT || type == RJSON_ARRAY)
   {
      enum rjson_type end = type == RJSON_OBJECT
            ? RJSON_OBJECT_END : RJSON_ARRAY_END;
      enum rjson_type token;
      while ((token = rjson_next(parser)) != end)
      {
         char *key = NULL;
         dgt_json_t *child;
         if (type == RJSON_OBJECT)
         {
            if (token != RJSON_STRING)
               goto error;
            key = strdup(rjson_get_string(parser, NULL));
            if (!key)
               goto error;
            token = rjson_next(parser);
         }
         child = dgt_json_read(parser, token, depth + 1);
         if (!child)
         {
            free(key);
            goto error;
         }
         child->key = key;
         *tail = child;
         tail = &child->next;
      }
   }
   return node;
error:
   dgt_json_free(node);
   return NULL;
}

static const dgt_json_t *dgt_member(const dgt_json_t *node, const char *key)
{
   const dgt_json_t *child;
   for (child = node ? node->child : NULL; child; child = child->next)
      if (child->key && !strcmp(child->key, key))
         return child;
   return NULL;
}

static const dgt_json_t *dgt_field(const dgt_json_t *node,
      const char *first, const char *second)
{
   const dgt_json_t *value = dgt_member(node, first);
   return value && value->type != RJSON_NULL
         ? value : dgt_member(node, second);
}

static char *dgt_code(const dgt_json_t *node)
{
   const dgt_json_t *part;
   char *code;
   size_t size = 1;
   if (!node || node->type == RJSON_NULL)
      return strdup("");
   if (node->type != RJSON_ARRAY)
      return dgt_trimmed(node->text);
   for (part = node->child; part; part = part->next)
      if (part->text)
         size += strlen(part->text) + 1;
   code = (char*)calloc(size, 1);
   if (!code)
      return NULL;
   for (part = node->child; part; part = part->next)
      if (part->text && *part->text)
      {
         char *line = dgt_trimmed(part->text);
         if (!line)
         {
            free(code);
            return NULL;
         }
         if (*line)
         {
            if (*code)
               strlcat(code, "\n", size);
            strlcat(code, line, size);
         }
         free(line);
      }
   return code;
}

static bool dgt_parse_json_cheat(const dgt_json_t *node)
{
   const dgt_json_t *name, *options, *def, *option;
   desktop_cheat_t *cheat;
   unsigned source_index = 0;
   if (!node || node->type != RJSON_OBJECT)
      return true;
   name    = dgt_field(node, "title", "name");
   options = dgt_member(node, "options");
   def     = dgt_member(node, "default");
   cheat   = dgt_add_cheat(name && name->text ? name->text : "Cheat");
   if (!cheat || !cheat->title)
      return false;
   if (options && options->type == RJSON_ARRAY && options->child)
   {
      for (option = options->child; option;
            option = option->next, source_index++)
      {
         char *code;
         const dgt_json_t *label;
         if (option->type != RJSON_OBJECT)
            continue;
         code = dgt_code(dgt_field(option, "value", "code"));
         label = dgt_field(option, "description", "name");
         if (!code)
            return false;
         /* The native picker supplies its own fixed off item. */
         if (*code)
         {
            if (!dgt_add_option(cheat, label && label->text
                  ? label->text : code, code, 0))
            {
               free(code);
               return false;
            }
            if (def && def->type == RJSON_NUMBER && def->text &&
                  atoi(def->text) == (int)source_index)
               cheat->selected = (int)cheat->num_options - 1;
         }
         free(code);
      }
      cheat->enabled = cheat->selected >= 0;
      if (!cheat->num_options)
      {
         free(cheat->title);
         free(cheat->options);
         dgt_count--;
      }
   }
   else
   {
      cheat->code = dgt_code(dgt_field(node, "value", "code"));
      if (!cheat->code)
         return false;
      cheat->enabled = def && (def->type == RJSON_TRUE ||
            (def->text && (!strcmp(def->text, "1") ||
             string_is_equal_noncase(def->text, "true"))));
   }
   return true;
}

void desktop_game_tools_prepare_content(const char *path)
{
   char outer[PATH_MAX_LENGTH], directory[PATH_MAX_LENGTH];
   char filename[PATH_MAX_LENGTH], *separator, *raw = NULL;
   int64_t length = 0;
   struct retro_system_info info;
   dgt_free_cheats();
   dgt_generation++;
   dgt_cheats_ready = false;
   dgt_error[0] = '\0';
   dgt_cheat_error[0] = '\0';
   strlcpy(dgt_content, path ? path : "", sizeof(dgt_content));
   memset(&info, 0, sizeof(info));
   core_get_system_info(&info);
   dgt_arcade = (info.library_name &&
         (string_is_equal_noncase(info.library_name, "FinalBurn Neo") ||
          string_is_equal_noncase(info.library_name, "FBNeo"))) ||
         strstr(path_get(RARCH_PATH_CORE), "fbneo") != NULL;
   if (!*dgt_content)
      return;
   strlcpy(outer, dgt_content, sizeof(outer));
   separator = strchr(outer, '#');
   if (separator)
      *separator = '\0';
   fill_pathname_basedir(directory, outer, sizeof(directory));
   fill_pathname_join_special(filename, directory, "cheat.txt", sizeof(filename));
   if (!path_is_valid(filename))
      return;
   if (filestream_read_file(filename, (void**)&raw, &length) < 0 || !raw)
   {
      dgt_cheat_fail("无法读取 ROM 同目录下的 cheat.txt。");
      return;
   }
   if (dgt_arcade)
   {
      char base[PATH_MAX_LENGTH], system[PATH_MAX_LENGTH];
      const char *system_dir = config_get_ptr()->paths.directory_system;
      /* FBNeo creates its dynamic core options while loading the ROM. */
      if (string_is_empty(system_dir) || config_get_ptr()->bools.systemfiles_in_content_dir)
         system_dir = directory;
      fill_pathname_join_special(system, system_dir, "fbneo", sizeof(system));
      fill_pathname_join_special(directory, system, "cheats", sizeof(directory));
      fill_pathname(base, path_basename(outer), ".ini", sizeof(base));
      fill_pathname_join_special(filename, directory, base, sizeof(filename));
      separator = raw;
      if (length >= 3 && (unsigned char)raw[0] == 0xef &&
            (unsigned char)raw[1] == 0xbb && (unsigned char)raw[2] == 0xbf)
      {
         separator += 3;
         length -= 3;
      }
      if (!path_mkdir(directory) ||
            !filestream_write_file(filename, separator, length))
         dgt_cheat_fail("无法保存 FBNeo 金手指定义文件。");
   }
   else
   {
      rjson_t *parser = rjson_open_buffer(raw, (size_t)length);
      dgt_json_t *root = NULL;
      const dgt_json_t *values, *value;
      bool valid = false;
      if (parser)
      {
         rjson_set_options(parser, RJSON_OPTION_ALLOW_UTF8BOM);
         root = dgt_json_read(parser, rjson_next(parser), 0);
         valid = root && rjson_next(parser) == RJSON_DONE;
      }
      values = root;
      if (root && root->type == RJSON_OBJECT &&
            dgt_member(root, "cheat") &&
            dgt_member(root, "cheat")->type == RJSON_ARRAY)
         values = dgt_member(root, "cheat");
      if (valid && values && values->type == RJSON_ARRAY)
      {
         for (value = values->child; value; value = value->next)
            if (!dgt_parse_json_cheat(value))
               valid = false;
      }
      else if (valid)
         valid = dgt_parse_json_cheat(values);
      if (!valid)
      {
         dgt_free_cheats();
         dgt_cheat_fail("cheat.txt 的 JSON 格式无效，无法读取金手指。");
      }
      dgt_json_free(root);
      if (parser)
         rjson_free(parser);
   }
   free(raw);
}

void desktop_game_tools_init_cheats(void)
{
#ifdef HAVE_CHEATS
   size_t i, j, total = 0;
   cheat_manager_t *manager = &cheat_manager_state;
   bool same;
   if (dgt_arcade || !dgt_count ||
         runloop_state_get_ptr()->current_core_type == CORE_TYPE_DUMMY ||
         !string_is_equal(dgt_content, path_get(RARCH_PATH_CONTENT)))
      return;
   for (i = 0; i < dgt_count; i++)
   {
      dgt_cheats[i].index = total;
      total += dgt_cheats[i].num_options ? dgt_cheats[i].num_options : 1;
   }
   if (total > UINT_MAX)
      return;
   same = manager->size == total && manager->cheats;
   for (i = 0; same && i < dgt_count; i++)
   {
      desktop_cheat_t *cheat = &dgt_cheats[i];
      size_t count = cheat->num_options ? cheat->num_options : 1;
      for (j = 0; same && j < count; j++)
      {
         const char *code = cheat->num_options
               ? cheat->options[j].code : cheat->code;
         same = string_is_equal(manager->cheats[cheat->index + j].code,
               code ? code : "");
      }
   }
   /* Retain an existing CHT's choices when it describes these same entries. */
   if (!same)
   {
      if (!cheat_manager_realloc((unsigned)total, CHEAT_HANDLER_TYPE_EMU))
      {
         dgt_cheat_fail("无法加载金手指。");
         return;
      }
      for (i = 0; i < dgt_count; i++)
      {
         desktop_cheat_t *cheat = &dgt_cheats[i];
         size_t count = cheat->num_options ? cheat->num_options : 1;
         for (j = 0; j < count; j++)
         {
            struct item_cheat *entry = &manager->cheats[cheat->index + j];
            const char *code = cheat->num_options
                  ? cheat->options[j].code : cheat->code;
            free(entry->desc);
            free(entry->code);
            entry->desc    = strdup(cheat->title);
            entry->code    = strdup(code ? code : "");
            entry->handler = CHEAT_HANDLER_TYPE_EMU;
            entry->state   = cheat->enabled && (!cheat->num_options ||
                  cheat->selected == (int)j);
         }
      }
   }
   dgt_cheats_ready = true;
   cheat_manager_apply_cheats(false);
#endif
}

unsigned desktop_game_tools_generation(void) { return dgt_generation; }
bool desktop_game_tools_is_arcade(void) { return dgt_arcade; }
const char *desktop_game_tools_error(void) { return dgt_error; }
const char *desktop_game_tools_cheat_error(void) { return dgt_cheat_error; }
bool desktop_game_tools_running(void)
{
   return (runloop_get_flags() & RUNLOOP_FLAG_CORE_RUNNING) &&
         runloop_state_get_ptr()->current_core_type != CORE_TYPE_DUMMY &&
         *dgt_content && string_is_equal(dgt_content, path_get(RARCH_PATH_CONTENT));
}

const desktop_cheat_t *desktop_game_tools_cheats(size_t *count)
{
   size_t i, j;
   core_option_manager_t *manager = runloop_state_get_ptr()->core_options;
   if (!desktop_game_tools_running())
   {
      *count = 0;
      return NULL;
   }
   if (dgt_arcade && !dgt_cheats_ready && manager)
   {
      dgt_free_cheats();
      for (i = 0; i < manager->size; i++)
      {
         struct core_option *option = &manager->opts[i];
         desktop_cheat_t *cheat;
         if (!option->key || strncmp(option->key, "fbneo-cheat-", 12))
            continue;
         cheat = dgt_add_cheat(option->desc);
         if (!cheat)
            break;
         cheat->index = i;
         for (j = 0; option->vals && j < option->vals->size; j++)
         {
            const char *value = option->vals->elems[j].data;
            const char *label = strstr(value, " - ");
            if (strtol(value, NULL, 10) == 0)
               cheat->off_index = j;
            else if (!dgt_add_option(cheat, label ? label + 3 : value, "", j))
               break;
         }
      }
      dgt_cheats_ready = true;
   }
   for (i = 0; i < dgt_count; i++)
   {
      desktop_cheat_t *cheat = &dgt_cheats[i];
      cheat->enabled  = false;
      cheat->selected = -1;
      if (dgt_arcade && manager && cheat->index < manager->size)
      {
         for (j = 0; j < cheat->num_options; j++)
            if (manager->opts[cheat->index].index == cheat->options[j].value_index)
            {
               cheat->enabled  = true;
               cheat->selected = (int)j;
            }
      }
#ifdef HAVE_CHEATS
      else if (!dgt_arcade && dgt_cheats_ready && cheat_manager_state.cheats)
      {
         size_t n = cheat->num_options ? cheat->num_options : 1;
         for (j = 0; j < n && cheat->index + j < cheat_manager_state.size; j++)
            if (cheat_manager_state.cheats[cheat->index + j].state)
            {
               cheat->enabled  = true;
               cheat->selected = (int)j;
            }
      }
#endif
   }
   *count = dgt_count;
   return dgt_cheats;
}

bool desktop_game_tools_set_cheat(size_t index, int choice)
{
   desktop_cheat_t *cheat;
   size_t i;
   dgt_error[0] = '\0';
   if (!desktop_game_tools_running() || index >= dgt_count || !dgt_cheats_ready)
      return dgt_fail("请先启动游戏后再修改金手指。");
   cheat = &dgt_cheats[index];
   if (choice < -1 || (cheat->num_options && choice >= (int)cheat->num_options) ||
         (!cheat->num_options && choice > 0))
      return dgt_fail("金手指选项无效。");
   if (dgt_arcade)
   {
      core_option_manager_t *manager = runloop_state_get_ptr()->core_options;
      size_t value;
      if (choice >= 0 && !cheat->num_options)
         return dgt_fail("当前金手指没有可用的启用选项。");
      value = choice < 0 ? cheat->off_index : cheat->options[choice].value_index;
      if (!manager || cheat->index >= manager->size)
         return dgt_fail("当前核心没有可用的金手指选项。");
      core_option_manager_set_val(manager, cheat->index, value, true);
      return true;
   }
#ifdef HAVE_CHEATS
   {
      size_t count = cheat->num_options ? cheat->num_options : 1;
      if (!cheat_manager_state.cheats || cheat->index + count > cheat_manager_state.size)
         return dgt_fail("金手指列表已变化，请重新打开窗口。");
      for (i = 0; i < count; i++)
         cheat_manager_state.cheats[cheat->index + i].state =
               choice >= 0 && (!cheat->num_options || choice == (int)i);
      cheat_manager_apply_cheats(false);
      cheat_manager_save_game_specific_cheats(config_get_ptr()->paths.path_cheat_database);
      return true;
   }
#else
   (void)i;
   return dgt_fail("当前版本未启用金手指功能。");
#endif
}

bool desktop_game_tools_save_busy(void)
{
   return content_save_state_in_progress(NULL);
}

bool desktop_game_tools_save_slot(unsigned slot, desktop_save_slot_t *state)
{
   memset(state, 0, sizeof(*state));
   if (slot > 9 || !desktop_game_tools_running() ||
         !runloop_get_savestate_path(state->path, sizeof(state->path), (int)slot))
      return false;
   strlcpy(state->thumbnail, state->path, sizeof(state->thumbnail));
   strlcat(state->thumbnail, ".png", sizeof(state->thumbnail));
   state->exists = path_is_valid(state->path);
   return true;
}

bool desktop_game_tools_write_save(unsigned slot)
{
   desktop_save_slot_t state;
   char directory[PATH_MAX_LENGTH];
   dgt_error[0] = '\0';
   if (!desktop_game_tools_save_slot(slot, &state))
      return dgt_fail("请先启动游戏后再保存存档。");
   if (desktop_game_tools_save_busy())
      return dgt_fail("正在保存存档，请稍后再试。");
   fill_pathname_basedir(directory, state.path, sizeof(directory));
   if (!path_mkdir(directory) || !content_save_state(state.path, true))
      return dgt_fail("无法创建即时存档，请确认核心支持存档且游戏正在运行。");
   return true;
}

void desktop_game_tools_capture_thumbnail(unsigned slot)
{
#ifdef HAVE_SCREENSHOTS
   desktop_save_slot_t state;
   if (desktop_game_tools_save_slot(slot, &state) && state.exists)
      take_screenshot(config_get_ptr()->paths.directory_screenshot,
            state.thumbnail, true, video_driver_cached_frame_is_hw_render(), true, false);
#else
   (void)slot;
#endif
}

bool desktop_game_tools_load_save(unsigned slot)
{
   desktop_save_slot_t state;
   dgt_error[0] = '\0';
   if (!desktop_game_tools_save_slot(slot, &state) || !state.exists)
      return dgt_fail("选择的槽位还没有存档。");
   if (desktop_game_tools_save_busy())
      return dgt_fail("正在保存存档，请稍后再试。");
   if (!command_event_load_state_path(state.path))
      return dgt_fail("读取存档失败。");
   return true;
}

bool desktop_game_tools_delete_save(unsigned slot)
{
   desktop_save_slot_t state;
   dgt_error[0] = '\0';
   if (!desktop_game_tools_save_slot(slot, &state) || !state.exists)
      return dgt_fail("选择的槽位还没有存档。");
   if (desktop_game_tools_save_busy())
      return dgt_fail("正在保存存档，请稍后再试。");
   if (filestream_delete(state.path) != 0)
      return dgt_fail("删除存档失败。");
   if (path_is_valid(state.thumbnail) && filestream_delete(state.thumbnail) != 0)
      return dgt_fail("存档已删除，但缩略图删除失败。");
   return true;
}
