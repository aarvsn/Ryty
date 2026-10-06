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
- (void)setClearColorRed:(double)r green:(double)g blue:(double)b alpha:(double)a;
- (BOOL)setupDefaultPipeline;
- (BOOL)setupDepthStencilState;

@property (nonatomic, readonly, nullable) id<MTLDevice> device;
@property (nonatomic, readonly, nullable) id<MTLCommandQueue> commandQueue;
@property (nonatomic, readonly, nullable) id<MTLRenderPipelineState> pipelineState;
@property (nonatomic, readonly, nullable) id<MTLDepthStencilState> depthStencilState;

@end

#endif // __OBJC__

#ifdef __cplusplus
extern "C" {
#endif

void RytyMetalInitializeDevice(void);
void RytyMetalRenderFrame(void);
int RytyMetalIsSupported(void);
const char* _Nullable RytyMetalGetDeviceName(void);
void RytyMetalSetClearColor(double r, double g, double b, double a);
void RytyMetalSetDepthStencilEnabled(int enabled);
void RytyMetalViewHostCreate(int w, int h, const char* _Nullable title);

#ifdef __cplusplus
}
#endif

#endif // RYTY_METAL_RENDERER_H
