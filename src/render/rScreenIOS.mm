/*
 * rScreenIOS.mm — iOS-specific display resolution helpers.
 *
 * On iOS, UIScreen.bounds may reflect "Display Zoom" mode, returning a
 * compatibility resolution smaller than native hardware. This file provides
 * helpers to query the true native resolution and force landscape orientation.
 */

#import <UIKit/UIKit.h>

void sr_GetNativeScreenPixels(int* outWidth, int* outHeight)
{
    @autoreleasepool {
        UIScreen *screen = [UIScreen mainScreen];
        CGRect nb = screen.nativeBounds;  // always portrait, always native pixels
        *outWidth  = (int)nb.size.width;
        *outHeight = (int)nb.size.height;
    }
}

// Track the actual keyboard height via UIKit notifications.
// The fraction is updated on every keyboard show/hide event.
static float s_keyboardHeightFraction = 0.0f;

float sr_iOSKeyboardHeightFraction(void)
{
    return s_keyboardHeightFraction;
}

static void sr_UpdateKeyboardFraction(CGFloat kbHeight)
{
    @autoreleasepool {
        CGFloat screenH = [UIScreen mainScreen].bounds.size.height;
        if (screenH > 0 && kbHeight > 0)
            s_keyboardHeightFraction = (float)(kbHeight / screenH);
        else
            s_keyboardHeightFraction = 0.0f;
    }
}

// One-time observer registration (call from sr_ForceLandscapeOrientation or screen init)
static void sr_RegisterKeyboardObservers(void)
{
    static bool registered = false;
    if (registered) return;
    registered = true;

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserverForName:UIKeyboardDidShowNotification object:nil queue:nil
                usingBlock:^(NSNotification *note) {
        CGRect frame = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
        sr_UpdateKeyboardFraction(frame.size.height);
    }];
    [nc addObserverForName:UIKeyboardDidHideNotification object:nil queue:nil
                usingBlock:^(NSNotification *note) {
        s_keyboardHeightFraction = 0.0f;
    }];
}

// Force the app to landscape orientation.
// On iOS 16+, returning landscape from supportedInterfaceOrientations is not enough —
// you must explicitly request a geometry update on the window scene.
// Call this after SDL has created and shown the window.
void sr_ForceLandscapeOrientation(void)
{
    sr_RegisterKeyboardObservers();
    @autoreleasepool {
        if (@available(iOS 16.0, *)) {
            // Find the first connected scene
            for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
                UIWindowScene *windowScene = (UIWindowScene *)scene;
                if (![windowScene isKindOfClass:[UIWindowScene class]]) continue;

                UIWindowSceneGeometryPreferencesIOS *prefs =
                    [[UIWindowSceneGeometryPreferencesIOS alloc]
                        initWithInterfaceOrientation:UIInterfaceOrientationLandscapeRight];
                [windowScene requestGeometryUpdateWithPreferences:prefs
                                                    errorHandler:^(NSError *error) {
                    // Ignore — landscape is declared in Info.plist, should succeed.
                    (void)error;
                }];

                // Also tell the view controller to re-evaluate supported orientations
                for (UIWindow *win in windowScene.windows) {
                    [win.rootViewController setNeedsUpdateOfSupportedInterfaceOrientations];
                }
                break;
            }
        } else {
            // iOS 15 and earlier: rotation is automatic based on Info.plist.
            // Force it via the deprecated status bar orientation API as a nudge.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [[UIApplication sharedApplication]
                setStatusBarOrientation:UIInterfaceOrientationLandscapeRight animated:NO];
#pragma clang diagnostic pop
        }
    }
}

// Recursively remove a directory using NSFileManager (sandbox-safe).
extern "C" void sr_iOSRemoveDirectoryRecursive(const char* path)
{
    @autoreleasepool {
        NSString *nsPath = [NSString stringWithUTF8String:path];
        NSFileManager *fm = [NSFileManager defaultManager];
        if ([fm fileExistsAtPath:nsPath])
        {
            NSError *error = nil;
            [fm removeItemAtPath:nsPath error:&error];
        }
    }
}
