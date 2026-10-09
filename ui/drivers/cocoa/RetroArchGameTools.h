#ifndef RETROARCH_GAME_TOOLS_COCOA_H
#define RETROARCH_GAME_TOOLS_COCOA_H
#import <AppKit/AppKit.h>

@interface RetroArchGameTools : NSObject
+ (void)showCheats;
+ (void)showSaves;
@end
#endif
