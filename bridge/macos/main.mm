#import <AppKit/AppKit.h>
#include "bridge_server.hpp"
#include <memory>

@interface BridgeAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation BridgeAppDelegate {
    NSStatusItem* _statusItem;
    NSMenu* _menu;
    NSMenuItem* _summaryItem;
    NSMenuItem* _serverItem;
    NSMenuItem* _clientItem;
    NSMenuItem* _wheelItem;
    NSTimer* _timer;
    std::unique_ptr<BridgeServer> _server;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    _server = std::make_unique<BridgeServer>();
    _server->start();

    _statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    _statusItem.button.title = @"G923Mac";

    _menu = [[NSMenu alloc] initWithTitle:@"G923Mac"];

    _summaryItem = [[NSMenuItem alloc] initWithTitle:@"Starting bridge..." action:nil keyEquivalent:@""];
    _summaryItem.enabled = NO;
    [_menu addItem:_summaryItem];

    _serverItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    _serverItem.enabled = NO;
    [_menu addItem:_serverItem];

    _clientItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    _clientItem.enabled = NO;
    [_menu addItem:_clientItem];

    _wheelItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    _wheelItem.enabled = NO;
    [_menu addItem:_wheelItem];

    [_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* reconnectItem =
        [[NSMenuItem alloc] initWithTitle:@"Reconnect Wheel" action:@selector(reconnectWheel:) keyEquivalent:@""];
    reconnectItem.target = self;
    [_menu addItem:reconnectItem];

    NSMenuItem* quitItem =
        [[NSMenuItem alloc] initWithTitle:@"Quit" action:@selector(quitApp:) keyEquivalent:@""];
    quitItem.target = self;
    [_menu addItem:quitItem];

    _statusItem.menu = _menu;

    _timer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                              target:self
                                            selector:@selector(refreshStatus:)
                                            userInfo:nil
                                             repeats:YES];
    [self refreshStatus:nil];
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;

    [_timer invalidate];
    _timer = nil;

    if (_server) {
        _server->stop();
        _server.reset();
    }
}

- (void)refreshStatus:(NSTimer*)timer {
    (void)timer;

    if (!_server) {
        return;
    }

    const auto status = _server->status();
    NSString* summary =
        [NSString stringWithFormat:@"Bridge %@ on localhost:%hu",
                                   status.listening ? @"listening" : @"offline",
                                   status.port];
    _summaryItem.title = summary;
    _serverItem.title =
        [NSString stringWithFormat:@"Server: %@", status.listening ? @"Ready" : @"Not listening"];

    NSString* clientText =
        status.client_connected
            ? [NSString stringWithFormat:@"Game: %@", [NSString stringWithUTF8String:status.client_name.c_str()]]
            : @"Game: Not connected";
    _clientItem.title = clientText;

    NSString* wheelText = nil;
    if (!status.wheel_name.empty()) {
        wheelText = [NSString stringWithFormat:@"Wheel: %@",
                                               [NSString stringWithUTF8String:status.wheel_name.c_str()]];
    } else if (status.wheel_connected) {
        wheelText = @"Wheel: Logitech G923";
    } else {
        wheelText = @"Wheel: Not connected";
    }
    _wheelItem.title = wheelText;

    _statusItem.button.title = @"G923Mac";
}

- (void)reconnectWheel:(id)sender {
    (void)sender;
    if (_server) {
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
          _server->reconnect_wheel();
          dispatch_async(dispatch_get_main_queue(), ^{
            [self refreshStatus:nil];
          });
        });
    }
    [self refreshStatus:nil];
}

- (void)quitApp:(id)sender {
    (void)sender;
    [NSApp terminate:nil];
}

@end

int main(int argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        BridgeAppDelegate* delegate = [[BridgeAppDelegate alloc] init];
        application.delegate = delegate;
        [application run];
    }

    return 0;
}
