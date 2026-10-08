#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include "tray.h"
#include "tray_icon.h"
#include <stdlib.h>

@interface TrayTarget : NSObject
- (void)clicked:(NSMenuItem *)sender;
@end

static void (*click_cb)(int);
static TrayTarget *target;
static NSStatusItem *status_item;
static NSMutableArray<NSMenuItem *> *menu_items; /* NSNull for separators */

@implementation TrayTarget
- (void)clicked:(NSMenuItem *)sender { if (click_cb) click_cb((int)sender.tag); }
@end

int tray_available(void) { return 1; }

static void apply(const tray_menu *menu) {
    for (int i = 0; i < menu->count && i < (int)menu_items.count; i++) {
        NSMenuItem *item = menu_items[i];
        if ((id)item == [NSNull null]) continue;
        item.title = [NSString stringWithUTF8String:menu->items[i].label];
        item.enabled = menu->items[i].enabled != 0;
        item.hidden = menu->items[i].visible == 0;
    }
}

int tray_init(const char *tooltip, const tray_menu *menu, void (*on_click)(int)) {
    @autoreleasepool {
        click_cb = on_click;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        target = [TrayTarget new];
        status_item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
        if (!status_item) return -1;
        NSData *data = [NSData dataWithBytes:tray_icon_mac length:sizeof(tray_icon_mac)];
        NSImage *image = [[NSImage alloc] initWithData:data];
        [image setSize:NSMakeSize(18, 18)];
        [image setTemplate:YES];
        status_item.button.image = image;
        status_item.button.toolTip = [NSString stringWithUTF8String:tooltip];

        NSMenu *ns_menu = [NSMenu new];
        ns_menu.autoenablesItems = NO;
        menu_items = [NSMutableArray new];
        for (int i = 0; i < menu->count; i++) {
            if (!strcmp(menu->items[i].label, "-")) {
                [ns_menu addItem:[NSMenuItem separatorItem]];
                [menu_items addObject:(id)[NSNull null]];
                continue;
            }
            NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:@"" action:@selector(clicked:) keyEquivalent:@""];
            item.target = target;
            item.tag = menu->items[i].id;
            [ns_menu addItem:item];
            [menu_items addObject:item];
        }
        status_item.menu = ns_menu;
        apply(menu);
    }
    return 0;
}

void tray_set_menu(const tray_menu *menu) {
    tray_menu *copy = malloc(sizeof(*copy));
    if (!copy) return;
    *copy = *menu;
    dispatch_async(dispatch_get_main_queue(), ^{ apply(copy); free(copy); });
}

void tray_run(void) { [NSApp run]; }

void tray_set_label(const char *text) { (void)text; }

void tray_quit(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[NSStatusBar systemStatusBar] removeStatusItem:status_item];
        [NSApp stop:nil];
        /* stop: only takes effect after an event; post a dummy one to wake the loop. */
        [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
                                       modifierFlags:0 timestamp:0 windowNumber:0 context:nil
                                             subtype:0 data1:0 data2:0] atStart:YES];
    });
}
#endif
