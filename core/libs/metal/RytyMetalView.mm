#import "RytyMetalRenderer.h"
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>
#include <iostream>

namespace Ryty::MacOs {

class MetalViewHost {
public:
    MetalViewHost() = default;

    bool InitializeWindow(int width, int height, const char* title) {
        @autoreleasepool {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (!device) {
                std::cerr << "[Ryty Metal C++] Metal device not supported." << std::endl;
                return false;
            }

            NSRect frame = NSMakeRect(0, 0, width, height);
            NSUInteger styleMask = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                   NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable;

            NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                           styleMask:styleMask
                                                             backing:NSBackingStoreBuffered
                                                               defer:NO];
            [window setTitle:[NSString stringWithUTF8String:title]];

            MTKView* mtkView = [[MTKView alloc] initWithFrame:frame device:device];
            [mtkView setClearColor:MTLClearColorMake(0.1, 0.1, 0.15, 1.0)];

            RytyMetalRenderer* renderer = [[RytyMetalRenderer alloc] initWithMetalKitView:mtkView];
            [renderer setupDefaultPipeline];
            [mtkView setDelegate:renderer];

            [window setContentView:mtkView];
            [window makeKeyAndOrderFront:nil];

            std::cout << "[Ryty Metal C++] Metal window and view initialized (" << width << "x" << height << ")." << std::endl;
            return true;
        }
    }
};

} // namespace Ryty::MacOs

extern "C" void RytyMetalViewHostCreate(int w, int h, const char* title) {
    Ryty::MacOs::MetalViewHost host;
    host.InitializeWindow(w, h, title);
}

extern "C" void RytyMetalViewHostSetClearColor(double r, double g, double b, double a) {
    RytyMetalSetClearColor(r, g, b, a);
}

extern "C" void RytyMetalViewHostEnableDepthStencil(int enabled) {
    RytyMetalSetDepthStencilEnabled(enabled);
}

extern "C" RytyMetalTextureHandle RytyMetalViewHostCreateTexture(int w, int h, int format) {
    return RytyMetalCreateTexture2D(w, h, format);
}

extern "C" void RytyMetalViewHostUpdateTexture(RytyMetalTextureHandle handle, const void* bytes, int bytesPerRow) {
    RytyMetalUpdateTexture2D(handle, bytes, bytesPerRow);
}

extern "C" void RytyMetalViewHostSetViewport(double x, double y, double w, double h) {
    RytyMetalSetViewport(x, y, w, h, 0.0, 1.0);
}
