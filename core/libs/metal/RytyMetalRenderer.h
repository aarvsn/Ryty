#ifndef RYTY_METAL_RENDERER_H
#define RYTY_METAL_RENDERER_H

#import <Foundation/Foundation.h>

#if defined(__OBJC__)
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

@interface RytyMetalRenderer : NSObject <MTKViewDelegate>

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView;
- (void)drawInMTKView:(nonnull MTKView *)view;
- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size;

@property (nonatomic, readonly, nullable) id<MTLDevice> device;
@property (nonatomic, readonly, nullable) id<MTLCommandQueue> commandQueue;

@end

#endif // __OBJC__

#ifdef __cplusplus
extern "C" {
#endif

void RytyMetalInitializeDevice(void);
void RytyMetalRenderFrame(void);

#ifdef __cplusplus
}
#endif

#endif // RYTY_METAL_RENDERER_H
