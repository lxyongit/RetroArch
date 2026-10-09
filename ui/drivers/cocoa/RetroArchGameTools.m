/* RetroArch native game panels. Licensed under GPLv3 or later. */
#import "RetroArchGameTools.h"
#include <stdlib.h>
#include <defines/cocoa_defines.h>
#include "../../desktop_game_tools.h"

@interface RAGameToolsDocument : NSView
@end
@implementation RAGameToolsDocument
- (BOOL)isFlipped { return YES; }
@end

/* Keep the ivars in a subclass so old Obj-C runtimes can build these panels. */
@interface RAGameToolsController : RetroArchGameTools
{
   NSPanel *_cheatPanel;
   NSPanel *_savePanel;
   NSView *_cheatRows;
   NSView *_saveRows;
   NSTextField *_cheatStatus;
   NSTextField *_saveStatus;
   NSAlert *_saveAlert;
   unsigned _cheatGeneration;
   unsigned _saveGeneration;
   BOOL _cheatsRunning;
   BOOL _savesRunning;
   int _pendingSlot;
   NSTimeInterval _pendingModified;
   NSTimeInterval _pendingDeadline;
}
- (void)openCheats;
- (void)openSaves;
- (void)refreshCheats;
- (void)refreshSaves;
- (void)poll:(NSTimer *)timer;
- (void)changeCheat:(id)sender;
- (void)saveAction:(id)sender;
- (void)performSaveForSlot:(unsigned)slot action:(unsigned)action;
- (void)saveAlertDidEnd:(NSAlert *)alert returnCode:(NSInteger)response
      contextInfo:(void *)contextInfo;
@end

typedef struct rat_confirmation
{
   unsigned slot;
   unsigned action;
   unsigned generation;
} rat_confirmation_t;

static NSString *rat_string(const char *text)
{
   NSString *result = text ? [NSString stringWithUTF8String:text] : nil;
   return result ? result : @"";
}

static NSTextField *rat_label(NSView *parent, NSString *text, NSRect frame)
{
   NSTextField *label = [[NSTextField alloc] initWithFrame:frame];
   [label setStringValue:text];
   [label setEditable:NO];
   [label setSelectable:NO];
   [label setBezeled:NO];
   [label setDrawsBackground:NO];
   [parent addSubview:label];
   RARCH_AUTORELEASE(label);
   return label;
}

static void rat_clear_rows(NSView *view)
{
   NSArray *children = [[view subviews] copy];
   for (NSView *child in children)
      [child removeFromSuperview];
   RARCH_RELEASE(children);
}

static NSTimeInterval rat_modified(const char *path)
{
   NSDictionary *attributes = [[NSFileManager defaultManager]
         attributesOfItemAtPath:rat_string(path) error:NULL];
   return [[attributes objectForKey:NSFileModificationDate] timeIntervalSince1970];
}

@implementation RetroArchGameTools
+ (RAGameToolsController *)sharedTools
{
   static RAGameToolsController *tools = nil;
   if (!tools)
      tools = [[RAGameToolsController alloc] init];
   return tools;
}
+ (void)showCheats { [[self sharedTools] openCheats]; }
+ (void)showSaves { [[self sharedTools] openSaves]; }
@end

@implementation RAGameToolsController
- (id)init
{
   self = [super init];
   if (self)
   {
      NSTimer *timer;
      _pendingSlot = -1;
      timer = [NSTimer timerWithTimeInterval:0.25 target:self
            selector:@selector(poll:) userInfo:nil repeats:YES];
      [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
   }
   return self;
}

- (NSPanel *)makePanel:(NSString *)title saves:(BOOL)saves
{
   NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 640, 500)
         styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
               NSNonactivatingPanelMask
         backing:NSBackingStoreBuffered defer:NO];
   NSView *content = [panel contentView];
   NSScrollView *scroll = [[NSScrollView alloc]
         initWithFrame:NSMakeRect(20, 20, 596, 394)];
   NSTextField *heading;
   NSTextField *status;
   NSView *rows;
   [panel setTitle:title];
   [panel setReleasedWhenClosed:NO];
   [panel setHidesOnDeactivate:NO];
   [panel setLevel:NSStatusWindowLevel];
#if defined(MAC_OS_X_VERSION_10_7)
   [panel setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
         NSWindowCollectionBehaviorFullScreenAuxiliary];
#endif
   heading = rat_label(content, title, NSMakeRect(20, 452, 240, 26));
   [heading setFont:[NSFont boldSystemFontOfSize:18]];
   rat_label(content, [title isEqualToString:@"存档管理"]
         ? @"固定 10 个槽位（0–9）" : @"修改后立即在游戏中生效",
         NSMakeRect(340, 454, 276, 20));
   status = rat_label(content, @"", NSMakeRect(20, 426, 596, 20));
   [status setTextColor:[NSColor disabledControlTextColor]];
   rows = [[RAGameToolsDocument alloc] initWithFrame:NSMakeRect(0, 0, 578, 1)];
   [scroll setDocumentView:rows];
   if (saves)
   {
      _saveRows = rows;
      _saveStatus = status;
   }
   else
   {
      _cheatRows = rows;
      _cheatStatus = status;
   }
   RARCH_RELEASE(rows);
   [scroll setHasVerticalScroller:YES];
   [scroll setBorderType:NSBezelBorder];
   [content addSubview:scroll];
   RARCH_RELEASE(scroll);
   [panel center];
   return panel;
}

- (void)openCheats
{
   if (!_cheatPanel)
      _cheatPanel = [self makePanel:@"金手指" saves:NO];
   [self refreshCheats];
   [_cheatPanel makeKeyAndOrderFront:nil];
   [_cheatPanel orderFrontRegardless];
}

- (void)openSaves
{
   if (!_savePanel)
      _savePanel = [self makePanel:@"存档管理" saves:YES];
   [self refreshSaves];
   [_savePanel makeKeyAndOrderFront:nil];
   [_savePanel orderFrontRegardless];
}

- (void)refreshCheats
{
   size_t i, count = 0;
   const desktop_cheat_t *cheats = desktop_game_tools_cheats(&count);
   _cheatGeneration = desktop_game_tools_generation();
   _cheatsRunning = desktop_game_tools_running();
   rat_clear_rows(_cheatRows);
   [_cheatStatus setStringValue:!desktop_game_tools_running()
         ? @"请先启动游戏后再管理金手指。"
         : (*desktop_game_tools_cheat_error() ? rat_string(desktop_game_tools_cheat_error())
            : (count ? @"" : @"当前游戏没有可用金手指。"))];
   for (i = 0; i < count; i++)
   {
      NSView *row = [[NSView alloc] initWithFrame:NSMakeRect(0, i * 50, 578, 48)];
      rat_label(row, rat_string(cheats[i].title), NSMakeRect(14, 13, 330, 22));
      if (cheats[i].num_options)
      {
         size_t j;
         NSPopUpButton *picker = [[NSPopUpButton alloc]
               initWithFrame:NSMakeRect(350, 9, 210, 28) pullsDown:NO];
         [picker addItemWithTitle:@"关闭"];
         for (j = 0; j < cheats[i].num_options; j++)
            [picker addItemWithTitle:rat_string(cheats[i].options[j].name)];
         [picker selectItemAtIndex:cheats[i].enabled ? cheats[i].selected + 1 : 0];
         [picker setTag:(NSInteger)i];
         [picker setTarget:self];
         [picker setAction:@selector(changeCheat:)];
         [row addSubview:picker];
         RARCH_RELEASE(picker);
      }
      else
      {
         NSButton *toggle = [[NSButton alloc] initWithFrame:NSMakeRect(482, 9, 78, 28)];
         [toggle setButtonType:NSSwitchButton];
         [toggle setTitle:@"启用"];
         [toggle setState:cheats[i].enabled ? NSOnState : NSOffState];
         [toggle setTag:(NSInteger)i];
         [toggle setTarget:self];
         [toggle setAction:@selector(changeCheat:)];
         [row addSubview:toggle];
         RARCH_RELEASE(toggle);
      }
      [_cheatRows addSubview:row];
      RARCH_RELEASE(row);
   }
   [_cheatRows setFrameSize:NSMakeSize(578, count ? count * 50 : 1)];
}

- (void)changeCheat:(id)sender
{
   NSString *error = nil;
   int choice;
   if (_cheatGeneration != desktop_game_tools_generation())
   {
      [self refreshCheats];
      return;
   }
   choice = [sender isKindOfClass:[NSPopUpButton class]]
         ? (int)[sender indexOfSelectedItem] - 1
         : ([sender state] == NSOnState ? 0 : -1);
   if (!desktop_game_tools_set_cheat((size_t)[sender tag], choice))
      error = rat_string(desktop_game_tools_error());
   [self refreshCheats];
   if (error)
      [_cheatStatus setStringValue:error];
}

- (void)refreshSaves
{
   unsigned slot;
   NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
   [formatter setDateFormat:@"yyyy-MM-dd HH:mm:ss"];
   _saveGeneration = desktop_game_tools_generation();
   _savesRunning = desktop_game_tools_running();
   rat_clear_rows(_saveRows);
   [_saveStatus setStringValue:@""];
   if (!desktop_game_tools_running())
   {
      [_saveStatus setStringValue:@"请先启动游戏后再管理存档。"];
      [_saveRows setFrameSize:NSMakeSize(578, 1)];
      RARCH_RELEASE(formatter);
      return;
   }
   for (slot = 0; slot < 10; slot++)
   {
      unsigned action;
      desktop_save_slot_t state;
      NSView *row;
      NSImageView *preview;
      NSImage *image;
      NSDictionary *attrs;
      NSDate *modified;
      if (!desktop_game_tools_save_slot(slot, &state))
         continue;
      row = [[NSView alloc] initWithFrame:NSMakeRect(0, slot * 84, 578, 78)];
      preview = [[NSImageView alloc] initWithFrame:NSMakeRect(8, 7, 104, 64)];
      image = state.exists ? [[NSImage alloc] initWithContentsOfFile:rat_string(state.thumbnail)] : nil;
      [preview setImage:image];
      [preview setImageScaling:NSImageScaleProportionallyUpOrDown];
      [row addSubview:preview];
      RARCH_RELEASE(image);
      RARCH_RELEASE(preview);
      if (!image)
         rat_label(row, @"暂无缩略图", NSMakeRect(14, 27, 98, 22));
      rat_label(row, [NSString stringWithFormat:state.exists
            ? @"即时存档 · 槽位 %u" : @"空槽位 · 槽位 %u", slot], NSMakeRect(124, 43, 230, 20));
      attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:rat_string(state.path) error:NULL];
      modified = [attrs objectForKey:NSFileModificationDate];
      rat_label(row, modified ? [formatter stringFromDate:modified] : @"未保存",
            NSMakeRect(124, 20, 230, 20));
      for (action = 0; action < 3; action++)
      {
         NSButton *button = [[NSButton alloc]
               initWithFrame:NSMakeRect(372 + action * 64, 24, 58, 30)];
         [button setTitle:action == 0 ? @"读取" : (action == 2
               ? @"删除" : (state.exists ? @"覆盖" : @"保存"))];
         [button setBezelStyle:NSRoundedBezelStyle];
         [button setEnabled:!desktop_game_tools_save_busy() &&
               (action == 1 || state.exists)];
         [button setTag:slot * 3 + action];
         [button setTarget:self];
         [button setAction:@selector(saveAction:)];
         [row addSubview:button];
         RARCH_RELEASE(button);
      }
      [_saveRows addSubview:row];
      RARCH_RELEASE(row);
   }
   [_saveRows setFrameSize:NSMakeSize(578, 840)];
   RARCH_RELEASE(formatter);
}

- (void)saveAction:(id)sender
{
   unsigned slot = (unsigned)[sender tag] / 3;
   unsigned action = (unsigned)[sender tag] % 3;
   desktop_save_slot_t state;
   if (_saveGeneration != desktop_game_tools_generation())
   {
      [self refreshSaves];
      return;
   }
   if (_pendingSlot >= 0)
   {
      [_saveStatus setStringValue:@"正在保存存档，请稍后再试。"];
      return;
   }
   if (!desktop_game_tools_save_slot(slot, &state))
      return;
   if (state.exists && action != 0)
   {
      rat_confirmation_t *confirmation;
      if (_saveAlert)
         return;
      confirmation = (rat_confirmation_t*)malloc(sizeof(*confirmation));
      if (!confirmation)
         return;
      confirmation->slot       = slot;
      confirmation->action     = action;
      confirmation->generation = _saveGeneration;
      _saveAlert = [[NSAlert alloc] init];
      [_saveAlert setMessageText:action == 2 ? @"删除存档" : @"覆盖存档"];
      [_saveAlert setInformativeText:[NSString stringWithFormat:action == 2
            ? @"确认删除槽位 %u 的存档吗？此操作无法撤销。"
            : @"确认用当前游戏进度覆盖槽位 %u 吗？此操作无法撤销。", slot]];
      [_saveAlert addButtonWithTitle:@"确认"];
      [_saveAlert addButtonWithTitle:@"取消"];
      [_saveAlert beginSheetModalForWindow:_savePanel modalDelegate:self
            didEndSelector:@selector(saveAlertDidEnd:returnCode:contextInfo:)
            contextInfo:confirmation];
      return;
   }
   [self performSaveForSlot:slot action:action];
}

- (void)saveAlertDidEnd:(NSAlert *)alert returnCode:(NSInteger)response
      contextInfo:(void *)contextInfo
{
   rat_confirmation_t confirmation = *(rat_confirmation_t*)contextInfo;
   free(contextInfo);
   [[alert window] orderOut:nil];
   RARCH_RELEASE(_saveAlert);
   _saveAlert = nil;
   if (response == NSAlertFirstButtonReturn &&
         confirmation.generation == desktop_game_tools_generation())
      [self performSaveForSlot:confirmation.slot action:confirmation.action];
   else if (response == NSAlertFirstButtonReturn)
      [_saveStatus setStringValue:@"当前游戏已切换，请重新选择存档槽位。"];
}

- (void)performSaveForSlot:(unsigned)slot action:(unsigned)action
{
   desktop_save_slot_t state;
   bool ok;
   if (_pendingSlot >= 0 || !desktop_game_tools_save_slot(slot, &state))
      return;
   _pendingModified = rat_modified(state.path);
   ok = action == 0 ? desktop_game_tools_load_save(slot)
         : (action == 2 ? desktop_game_tools_delete_save(slot)
            : desktop_game_tools_write_save(slot));
   [self refreshSaves];
   [_saveStatus setStringValue:ok ? (action == 1 ? @"正在保存…"
         : (action == 0 ? @"已请求读取存档。" : @"已删除存档。"))
         : rat_string(desktop_game_tools_error())];
   if (ok && action == 1)
   {
      _pendingSlot = (int)slot;
      _pendingDeadline = [NSDate timeIntervalSinceReferenceDate] + 8;
   }
}

- (void)poll:(NSTimer *)timer
{
   (void)timer;
   if ((_cheatGeneration != desktop_game_tools_generation() ||
         _cheatsRunning != desktop_game_tools_running()) && [_cheatPanel isVisible])
      [self refreshCheats];
   if (_saveGeneration != desktop_game_tools_generation() ||
         _savesRunning != desktop_game_tools_running())
   {
      _pendingSlot = -1;
      if ([_savePanel isVisible])
         [self refreshSaves];
   }
   if (_pendingSlot >= 0 && !desktop_game_tools_save_busy())
   {
      desktop_save_slot_t state;
      bool updated = desktop_game_tools_save_slot((unsigned)_pendingSlot, &state) &&
            state.exists && rat_modified(state.path) != _pendingModified;
      if (updated || [NSDate timeIntervalSinceReferenceDate] >= _pendingDeadline)
      {
         if (updated)
            desktop_game_tools_capture_thumbnail((unsigned)_pendingSlot);
         _pendingSlot = -1;
         [self refreshSaves];
         [_saveStatus setStringValue:updated ? @"已保存存档。" : @"未能创建存档，请查看 RetroArch 的错误提示。"];
      }
   }
}
@end
