#import <objc/message.h>

/* Reattach Apple's complete HUD controls after UIKit builds the host menu bar.
   The callbacks, presets, live menu delegate, and configuration panel belong to
   macOS 27 libMTLHud; no system code or game rendering state is patched. */
static id metal_hud_handler;

static NSMenuItem *hud_item(NSMenu *menu, NSString *title, NSString *action) {
    NSMenuItem *item = [menu addItemWithTitle:title action:action ? NSSelectorFromString(action) : NULL keyEquivalent:@""];
    if (action) item.target = metal_hud_handler;
    return item;
}

static NSMenu *hud_submenu(NSMenu *parent, NSString *title, NSString *identifier) {
    NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
    menu.identifier = identifier;
    hud_item(parent, title, nil).submenu = menu;
    return menu;
}

static void install_metal_hud_menu(void) {
    NSMenu *main = NSApp.mainMenu;
    for (NSMenuItem *item in main.itemArray) {
        if ([item.title isEqual:@"Metal HUD"]) {
            fprintf(output, "metal_hud_menu=system_present\n");
            return;
        }
    }
    Class handlerClass = NSClassFromString(@"HUDKeyboardEventHandler");
    NSArray *actions = @[@"toggleEnabled:", @"toggleLoggingEnabled:", @"setAlignment:", @"setLayout:",
                         @"resetMetrics:", @"generateReport:", @"showConfigurationPanel:", @"copyConfig:",
                         @"setMainLayer:", @"menuNeedsUpdate:"];
    for (NSString *name in actions) {
        Method method = class_getInstanceMethod(handlerClass, NSSelectorFromString(name));
        if (!method || method_getNumberOfArguments(method) != 3) {
            fprintf(output, "metal_hud_menu=unsupported action=%s\n", name.UTF8String);
            return;
        }
    }
    if (!main) { fprintf(output, "metal_hud_menu=main_menu_missing\n"); return; }
    metal_hud_handler = [handlerClass new];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"Metal HUD"];
    menu.identifier = @"hud";
    menu.delegate = metal_hud_handler;
    hud_item(menu, @"Show HUD", @"toggleEnabled:").tag = 0;
    hud_item(menu, @"Enable Logging", @"toggleLoggingEnabled:").tag = 1;
    [menu addItem:NSMenuItem.separatorItem];

    NSMenu *position = hud_submenu(menu, @"Position", nil);
    NSArray *titles = @[@"Top-Left", @"Top-Center", @"Top-Right", @"Center-Left", @"Centered", @"Center-Right", @"Bottom-Right", @"Bottom-Center", @"Bottom-Left"];
    NSArray *alignments = @[@10, @14, @12, @26, @30, @28, @20, @22, @18];
    for (NSUInteger i = 0; i < titles.count; i++)
        hud_item(position, titles[i], @"setAlignment:").representedObject = alignments[i];

    NSMenu *presets = hud_submenu(menu, @"Metric Presets", nil);
    NSArray *presetNames = @[@"FPS Only", @"Default", @"Rich", @"Full"];
    NSArray *layouts = @[@0x10ULL, @0x8102803fULL, @0x810788bfULL, @0x81c79affULL];
    for (NSUInteger i = 0; i < presetNames.count; i++)
        hud_item(presets, presetNames[i], @"setLayout:").representedObject = layouts[i];

    [menu addItem:NSMenuItem.separatorItem];
    hud_item(menu, @"Reset Metrics", @"resetMetrics:");
    [menu addItem:NSMenuItem.separatorItem];
    NSMenu *reports = hud_submenu(menu, @"Generate Performance Report", @"perfreport");
    reports.autoenablesItems = NO;
    reports.delegate = metal_hud_handler;
    [menu addItem:NSMenuItem.separatorItem];
    hud_item(menu, @"Show Configuration Panel", @"showConfigurationPanel:");
    hud_item(menu, @"Copy HUD Configuration", @"copyConfig:");
    [menu addItem:NSMenuItem.separatorItem];
    hud_submenu(menu, @"Set Main Layer", @"mainlayer").delegate = metal_hud_handler;

    NSMenuItem *root = [[NSMenuItem alloc] initWithTitle:@"Metal HUD" action:NULL keyEquivalent:@""];
    root.identifier = @"endfield.metal-hud.native-menu";
    root.submenu = menu;
    [main addItem:root];
    fprintf(output, "metal_hud_menu=full_native_actions_installed items=%ld\n", (long)menu.numberOfItems);
}
