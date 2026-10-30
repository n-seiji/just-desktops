#import <AppKit/AppKit.h>
#include "menubar_priv.h"
#include "app.h"

// The menu of a right or control click on the item, and of every click in icon mode. The settings
// rewrite one line of the config file and restart (app_set); the bare binary keeps its one-item menu.

@interface JDMenuActions : NSObject
- (void)quit:(id)sender;
- (void)set:(NSMenuItem *)sender;
- (void)toggleLogin:(id)sender;
- (void)openConfig:(id)sender;
- (void)about:(id)sender;
@end

@implementation JDMenuActions
- (void)quit:(id)sender {
    (void)sender;
    jd_quit();
}
// representedObject is @[key, value]: write that one line of the config file and restart.
- (void)set:(NSMenuItem *)sender {
    NSArray<NSString *> *kv = sender.representedObject;
    app_set(kv[0].UTF8String, kv[1].UTF8String);
}
- (void)toggleLogin:(id)sender {
    (void)sender;
    app_toggle_login();
}
- (void)openConfig:(id)sender {
    (void)sender;
    app_open_config();
}
- (void)about:(id)sender {
    (void)sender;
    app_open_about();
}
@end

static JDMenuActions *actions;

static void add_item(NSMenu *m, NSString *title, SEL action, NSControlStateValue state, NSString *key, NSString *value) {
    NSMenuItem *it = [m addItemWithTitle:title action:action keyEquivalent:@""];
    it.target = actions;
    it.state = state;
    if (key) it.representedObject = @[ key, value ];
}

static void add_choice(NSMenu *m, NSString *title, bool current, NSString *key, NSString *value) {
    add_item(m, title, @selector(set:), current ? NSControlStateValueOn : NSControlStateValueOff, key, value);
}

// Built on every open, so the checks match the settings in force and the login item's state.
NSMenu *menubar_menu(menubar_mode_t mode) {
    if (!actions) actions = [JDMenuActions new];
    NSMenu *m = [NSMenu new];
    if (jd_app_mode()) {
        sw_style_t sw = config_switcher_style();
        NSMenu *sws = [NSMenu new];
        add_choice(sws, @"List", sw == SW_LIST, @"switcher", @"list");
        add_choice(sws, @"Square", sw == SW_SQUARE, @"switcher", @"square");
        add_choice(sws, @"Instant (off)", sw == SW_OFF, @"switcher", @"off");
        [m setSubmenu:sws forItem:[m addItemWithTitle:@"Switcher" action:nil keyEquivalent:@""]];

        add_choice(m, @"Focus border", config_border(), @"border", config_border() ? @"off" : @"on");

        NSMenu *mbs = [NSMenu new];
        add_choice(mbs, @"Windows strip", mode == MB_STRIP, @"menubar", @"on");
        add_choice(mbs, @"Icon only", mode == MB_ICON, @"menubar", @"icon");
        [m setSubmenu:mbs forItem:[m addItemWithTitle:@"Menu bar" action:nil keyEquivalent:@""]];

        [m addItem:NSMenuItem.separatorItem];
        int login = app_login_state();
        add_item(m, @"Launch at Login", @selector(toggleLogin:),
                 login == 1 ? NSControlStateValueOn : login == 2 ? NSControlStateValueMixed : NSControlStateValueOff, nil, nil);
        add_item(m, @"Open Config File", @selector(openConfig:), NSControlStateValueOff, nil, nil);
        [m addItem:NSMenuItem.separatorItem];
        add_item(m, [NSString stringWithFormat:@"About just-desktops %s", JD_VERSION], @selector(about:), NSControlStateValueOff, nil, nil);
    }
    add_item(m, @"Quit just-desktops", @selector(quit:), NSControlStateValueOff, nil, nil);
    return m;
}
