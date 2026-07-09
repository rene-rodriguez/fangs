// desktop_notify_mac — native UNUserNotificationCenter delivery for agent
// rings, attributed to Fangs' own bundle identity so clicking a banner
// activates Fangs (see the comment above the extern decls in
// desktop_notify.c for why osascript's `display notification` can't do this).
#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>
#include <stdbool.h>

@interface FangsNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation FangsNotificationDelegate

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
 didReceiveNotificationResponse:(UNNotificationResponse *)response
          withCompletionHandler:(void (^)(void))completionHandler
{
    (void)center;
    (void)response;
    // The delegate callback fires on an arbitrary queue; AppKit calls must
    // hop to the main queue.
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp activateIgnoringOtherApps:YES];
        for (NSWindow *win in NSApp.windows) {
            if (win.isMiniaturized) [win deminiaturize:nil];
            [win makeKeyAndOrderFront:nil];
        }
    });
    completionHandler();
}

@end

static FangsNotificationDelegate *g_notify_delegate = nil;

void desktop_notify_native_startup(void)
{
    // UNUserNotificationCenter requires a real bundle identity. Running the
    // bare build/fangs binary (no Info.plist) leaves this nil; requesting
    // authorization would just fail, so skip it and let
    // desktop_notify_agent_ring() fall back to osascript.
    if (!NSBundle.mainBundle.bundleIdentifier)
        return;

    g_notify_delegate = [FangsNotificationDelegate new];
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    center.delegate = g_notify_delegate;
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError *error) {
        (void)granted;
        (void)error;
    }];
}

bool desktop_notify_native_ring(const char *workspace, const char *message)
{
    if (!NSBundle.mainBundle.bundleIdentifier)
        return false;

    NSString *ws  = (workspace && workspace[0]) ? @(workspace) : @"shell";
    NSString *msg = (message && message[0]) ? @(message) : @"needs attention";

    UNMutableNotificationContent *content = [UNMutableNotificationContent new];
    content.title = @"Fangs";
    content.subtitle = ws;
    content.body = msg;

    UNNotificationRequest *request =
        [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString
                                              content:content
                                              trigger:nil];
    [UNUserNotificationCenter.currentNotificationCenter
        addNotificationRequest:request
         withCompletionHandler:^(NSError *error) {
        (void)error;
    }];
    // Authorization may still be denied — in that case the request above is
    // a silent no-op, which is the correct behavior (respect the denial
    // rather than falling back to the misattributed osascript path).
    return true;
}
