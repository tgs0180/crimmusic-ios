// iPhone versions of the platform functions. This is Objective-C++: normal
// C++ mixed with Objective-C, the language Apple's APIs (UIKit etc.) use.
// [object message:argument] is Objective-C's way of calling a method.
#include "Platform.hpp"
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <SDL3/SDL.h>
#include <atomic>

static std::atomic<int> imported{0};

namespace platform {

std::filesystem::path musicFolder() {
    // Every iOS app gets its own private Documents folder. Because Info.plist
    // sets UIFileSharingEnabled, it is visible in the Files app as
    // "On My iPhone > crimmusic".
    NSURL* documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    return std::filesystem::path(documents.path.UTF8String);
}

void setMusicFolder(const std::filesystem::path&) {}

std::filesystem::path assetsFolder() {
    // Inside the .app bundle (SDL_GetBasePath returns the bundle folder on iOS).
    const char* base = SDL_GetBasePath();
    return std::filesystem::path(base ? base : "") / "assets";
}

int takeImportedCount() {
    return imported.exchange(0);
}

}

// ------------------------------------------------------------------ importing
// Copies a picked file into Documents, adding " (2)" etc. if the name is taken.
static void copyIntoLibrary(NSURL* source) {
    NSFileManager* files = [NSFileManager defaultManager];
    NSURL* folder = [files URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
    NSString* stem = source.lastPathComponent.stringByDeletingPathExtension;
    NSString* extension = source.pathExtension;
    NSURL* target = [folder URLByAppendingPathComponent:source.lastPathComponent];
    for (int n = 2; [files fileExistsAtPath:target.path]; n++) {
        NSString* name = [NSString stringWithFormat:@"%@ (%d).%@", stem, n, extension];
        target = [folder URLByAppendingPathComponent:name];
    }

    // Files from iCloud Drive or other apps need permission while copying.
    BOOL scoped = [source startAccessingSecurityScopedResource];
    NSError* error = nil;
    if ([files copyItemAtURL:source toURL:target error:&error]) imported++;
    else NSLog(@"crimmusic: import failed for %@: %@", source, error);
    if (scoped) [source stopAccessingSecurityScopedResource];
}

// The file picker reports back to a "delegate" object.
@interface CrimImportDelegate : NSObject <UIDocumentPickerDelegate>
@end

@implementation CrimImportDelegate
- (void)documentPicker:(UIDocumentPickerViewController*)controller didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls {
    for (NSURL* url in urls) copyIntoLibrary(url);
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller {
}
@end

namespace platform {

void showImportPicker(SDL_Window* window) {
    // The picker only keeps a weak pointer to its delegate, so keep ours alive here.
    static CrimImportDelegate* delegate = [[CrimImportDelegate alloc] init];

    UIWindow* uiWindow = (__bridge UIWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    UIViewController* root = uiWindow.rootViewController;
    if (root == nil) return;

    UIDocumentPickerViewController* picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeMP3] asCopy:YES];
    picker.allowsMultipleSelection = YES;
    picker.delegate = delegate;
    picker.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    [root presentViewController:picker animated:YES completion:nil];
}

// ------------------------------------------------------------------ HTTP
int httpGet(const std::string& url, const std::vector<std::pair<std::string, std::string>>& query, std::string& body) {
    @autoreleasepool {
        NSURLComponents* components = [NSURLComponents componentsWithString:[NSString stringWithUTF8String:url.c_str()]];
        NSMutableArray<NSURLQueryItem*>* items = [NSMutableArray array];
        for (const auto& [key, value] : query) {
            [items addObject:[NSURLQueryItem queryItemWithName:[NSString stringWithUTF8String:key.c_str()]
                                                         value:[NSString stringWithUTF8String:value.c_str()]]];
        }
        components.queryItems = items;
        if (components.URL == nil) return 0;

        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:components.URL];
        request.timeoutInterval = 8.0;
        [request setValue:@"crimmusic-ios (https://github.com/tgs0180/crimmusic)" forHTTPHeaderField:@"User-Agent"];

        // NSURLSession is asynchronous, but this runs on the lyrics worker
        // thread, so it's fine to wait here until the answer arrives.
        __block int status = 0;
        __block NSData* data = nil;
        dispatch_semaphore_t finished = dispatch_semaphore_create(0);
        NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:request
            completionHandler:^(NSData* received, NSURLResponse* response, NSError* error) {
                if (error == nil && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                    status = (int)((NSHTTPURLResponse*)response).statusCode;
                    data = received;
                }
                dispatch_semaphore_signal(finished);
            }];
        [task resume];
        dispatch_semaphore_wait(finished, DISPATCH_TIME_FOREVER);

        if (data != nil) body.assign(static_cast<const char*>(data.bytes), data.length);
        return status;
    }
}

// ------------------------------------------------------------------ safe area
Insets safeArea(SDL_Window* window) {
    // SDL tells us which part of the window is not covered by the notch,
    // Dynamic Island or home indicator.
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    SDL_Rect safe;
    if (!SDL_GetWindowSafeArea(window, &safe)) return Insets{0, 0, 0, 0};
    return Insets{
        static_cast<float>(safe.y),
        static_cast<float>(height - (safe.y + safe.h)),
        static_cast<float>(safe.x),
        static_cast<float>(width - (safe.x + safe.w))
    };
}

}
