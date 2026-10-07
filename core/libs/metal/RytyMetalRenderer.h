#ifndef RYTY_METAL_RENDERER_H
#define RYTY_METAL_RENDERER_H

#import <Foundation/Foundation.h>

typedef void* RytyMetalTextureHandle;
typedef void* RytyMetalBufferHandle;

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

- (RytyMetalTextureHandle _Nullable)createTextureWidth:(NSUInteger)w height:(NSUInteger)h pixelFormat:(NSUInteger)pf;
- (void)updateTexture:(RytyMetalTextureHandle _Nonnull)handle bytes:(const void* _Nonnull)bytes bytesPerRow:(NSUInteger)bpr;
- (void)destroyTexture:(RytyMetalTextureHandle _Nonnull)handle;

- (RytyMetalBufferHandle _Nullable)createBufferData:(const void* _Nullable)data length:(NSUInteger)len;
- (void)updateBuffer:(RytyMetalBufferHandle _Nonnull)handle data:(const void* _Nonnull)data length:(NSUInteger)len offset:(NSUInteger)off;
- (void)destroyBuffer:(RytyMetalBufferHandle _Nonnull)handle;

- (void)setViewportX:(double)x y:(double)y width:(double)w height:(double)h znear:(double)zn zfar:(double)zf;
- (void)setScissorRectX:(NSUInteger)x y:(NSUInteger)y width:(NSUInteger)w height:(NSUInteger)h;
- (void)setBlendMode:(int)blendMode;

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

RytyMetalTextureHandle _Nullable RytyMetalCreateTexture2D(int width, int height, int pixelFormat);
void RytyMetalUpdateTexture2D(RytyMetalTextureHandle _Nonnull handle, const void* _Nonnull bytes, int bytesPerRow);
void RytyMetalDestroyTexture2D(RytyMetalTextureHandle _Nonnull handle);

RytyMetalBufferHandle _Nullable RytyMetalCreateBuffer(const void* _Nullable data, unsigned long length);
void RytyMetalUpdateBuffer(RytyMetalBufferHandle _Nonnull handle, const void* _Nonnull data, unsigned long length, unsigned long offset);
void RytyMetalDestroyBuffer(RytyMetalBufferHandle _Nonnull handle);

void RytyMetalSetViewport(double x, double y, double width, double height, double znear, double zfar);
void RytyMetalSetScissorRect(unsigned int x, unsigned int y, unsigned int width, unsigned int height);
void RytyMetalSetBlendMode(int blendMode);
void RytyMetalDrawPrimitives(int primitiveType, unsigned int start, unsigned int count);
void RytyMetalDrawIndexedPrimitives(int primitiveType, unsigned int indexCount, int indexType, RytyMetalBufferHandle _Nonnull indexBuffer, unsigned int indexBufferOffset);

unsigned long RytyMetalGetMaxThreadsPerThreadgroup(void);
int RytyMetalSupportsFeatureSet(int featureSet);

void RytyMetalViewHostCreate(int w, int h, const char* _Nullable title);

#ifdef __cplusplus
}
#endif

#endif // RYTY_METAL_RENDERER_H
