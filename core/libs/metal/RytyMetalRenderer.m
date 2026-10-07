#import "RytyMetalRenderer.h"
#import <stdio.h>

static MTLClearColor g_clearColor = {0.1, 0.1, 0.15, 1.0};
static char g_deviceNameBuffer[256] = "Metal Default Device";
static int g_depthStencilEnabled = 1;

@implementation RytyMetalRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLDepthStencilState> _depthStencilState;
    MTLClearColor _clearColor;
}

@synthesize device = _device;
@synthesize commandQueue = _commandQueue;
@synthesize pipelineState = _pipelineState;
@synthesize depthStencilState = _depthStencilState;

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView {
    self = [super init];
    if (self) {
        _device = mtkView.device;
        if (!_device) {
            _device = MTLCreateSystemDefaultDevice();
            mtkView.device = _device;
        }
        _clearColor = g_clearColor;
        [mtkView setClearColor:_clearColor];
        if (g_depthStencilEnabled) {
            [mtkView setDepthStencilPixelFormat:MTLPixelFormatDepth32Float];
        }
        if (_device) {
            _commandQueue = [_device newCommandQueue];
            snprintf(g_deviceNameBuffer, sizeof(g_deviceNameBuffer), "%s", [[_device name] UTF8String]);
            printf("[Ryty Metal] Initialized Metal device: %s\n", g_deviceNameBuffer);
            [self setupDepthStencilState];
        } else {
            printf("[Ryty Metal] Error: Metal is not supported on this device.\n");
        }
    }
    return self;
}

- (void)setClearColorRed:(double)r green:(double)g blue:(double)b alpha:(double)a {
    _clearColor = MTLClearColorMake(r, g, b, a);
    g_clearColor = _clearColor;
}

- (BOOL)setupDefaultPipeline {
    if (!_device) return NO;
    MTLRenderPipelineDescriptor *pipelineDescriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipelineDescriptor.label = @"RytyDefaultPipeline";
    pipelineDescriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipelineDescriptor.colorAttachments[0].blendingEnabled = YES;
    pipelineDescriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipelineDescriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    pipelineDescriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    if (g_depthStencilEnabled) {
        pipelineDescriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    }

    NSError *error = nil;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!_pipelineState && error) {
        printf("[Ryty Metal] Pipeline creation log: %s\n", [[error localizedDescription] UTF8String]);
    }
    return _pipelineState != nil;
}

- (BOOL)setupDepthStencilState {
    if (!_device) return NO;
    MTLDepthStencilDescriptor *dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
    dsDesc.depthWriteEnabled = YES;
    _depthStencilState = [_device newDepthStencilStateWithDescriptor:dsDesc];
    return _depthStencilState != nil;
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (RytyMetalTextureHandle _Nullable)createTextureWidth:(NSUInteger)w height:(NSUInteger)h pixelFormat:(NSUInteger)pf {
    if (!_device) return NULL;
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(MTLPixelFormat)pf
                                                                                      width:w
                                                                                     height:h
                                                                                  mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    id<MTLTexture> tex = [_device newTextureWithDescriptor:desc];
    return (__bridge_retained RytyMetalTextureHandle)tex;
}

- (void)updateTexture:(RytyMetalTextureHandle _Nonnull)handle bytes:(const void* _Nonnull)bytes bytesPerRow:(NSUInteger)bpr {
    if (!handle) return;
    id<MTLTexture> tex = (__bridge id<MTLTexture>)handle;
    MTLRegion region = MTLRegionMake2D(0, 0, [tex width], [tex height]);
    [tex replaceRegion:region mipmapLevel:0 withBytes:bytes bytesPerRow:bpr];
}

- (void)destroyTexture:(RytyMetalTextureHandle _Nonnull)handle {
    if (!handle) return;
    id<MTLTexture> tex = (__bridge_transfer id<MTLTexture>)handle;
    (void)tex;
}

- (RytyMetalBufferHandle _Nullable)createBufferData:(const void* _Nullable)data length:(NSUInteger)len {
    if (!_device || len == 0) return NULL;
    id<MTLBuffer> buf = nil;
    if (data) {
        buf = [_device newBufferWithBytes:data length:len options:MTLResourceStorageModeShared];
    } else {
        buf = [_device newBufferWithLength:len options:MTLResourceStorageModeShared];
    }
    return (__bridge_retained RytyMetalBufferHandle)buf;
}

- (void)updateBuffer:(RytyMetalBufferHandle _Nonnull)handle data:(const void* _Nonnull)data length:(NSUInteger)len offset:(NSUInteger)off {
    if (!handle || !data) return;
    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)handle;
    if (off + len <= [buf length]) {
        std::memcpy((uint8_t*)[buf contents] + off, data, len);
    }
}

- (void)destroyBuffer:(RytyMetalBufferHandle _Nonnull)handle {
    if (!handle) return;
    id<MTLBuffer> buf = (__bridge_transfer id<MTLBuffer>)handle;
    (void)buf;
}

- (void)setViewportX:(double)x y:(double)y width:(double)w height:(double)h znear:(double)zn zfar:(double)zf {
    (void)x; (void)y; (void)w; (void)h; (void)zn; (void)zf;
}

- (void)setScissorRectX:(NSUInteger)x y:(NSUInteger)y width:(NSUInteger)w height:(NSUInteger)h {
    (void)x; (void)y; (void)w; (void)h;
}

- (void)setBlendMode:(int)blendMode {
    (void)blendMode;
}

- (void)drawInMTKView:(nonnull MTKView *)view {
    if (!_commandQueue) return;

    MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor != nil) {
        renderPassDescriptor.colorAttachments[0].clearColor = _clearColor;
        id<MTLCommandBuffer> commandBuffer = [_commandQueue commandBuffer];
        commandBuffer.label = @"RytyMetalFrameCommand";

        id<MTLRenderCommandEncoder> renderEncoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        renderEncoder.label = @"RytyMetalRenderEncoder";

        if (_pipelineState) {
            [renderEncoder setRenderPipelineState:_pipelineState];
        }
        if (_depthStencilState) {
            [renderEncoder setDepthStencilState:_depthStencilState];
        }

        [renderEncoder endEncoding];
        [commandBuffer presentDrawable:view.currentDrawable];
        [commandBuffer commit];
    }
}

@end

void RytyMetalInitializeDevice(void) {
#if defined(__APPLE__)
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device) {
        snprintf(g_deviceNameBuffer, sizeof(g_deviceNameBuffer), "%s", [[device name] UTF8String]);
        printf("[Ryty Metal] Initialized default Metal device: %s\n", g_deviceNameBuffer);
    } else {
        printf("[Ryty Metal] Metal device initialization failed.\n");
    }
#else
    printf("[Ryty Metal] Metal device initialization stub called.\n");
#endif
}

void RytyMetalRenderFrame(void) {
    printf("[Ryty Metal] Metal frame render step executed.\n");
}

int RytyMetalIsSupported(void) {
#if defined(__APPLE__)
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return device != nil ? 1 : 0;
#else
    return 1; // Return 1 in stub mode for cross-platform compatibility
#endif
}

const char* RytyMetalGetDeviceName(void) {
    return g_deviceNameBuffer;
}

void RytyMetalSetClearColor(double r, double g, double b, double a) {
    g_clearColor = MTLClearColorMake(r, g, b, a);
}

void RytyMetalSetDepthStencilEnabled(int enabled) {
    g_depthStencilEnabled = enabled;
}

RytyMetalTextureHandle RytyMetalCreateTexture2D(int width, int height, int pixelFormat) {
    (void)width; (void)height; (void)pixelFormat;
    static int dummyTexture = 1;
    return (RytyMetalTextureHandle)&dummyTexture;
}

void RytyMetalUpdateTexture2D(RytyMetalTextureHandle handle, const void* bytes, int bytesPerRow) {
    (void)handle; (void)bytes; (void)bytesPerRow;
}

void RytyMetalDestroyTexture2D(RytyMetalTextureHandle handle) {
    (void)handle;
}

RytyMetalBufferHandle RytyMetalCreateBuffer(const void* data, unsigned long length) {
    (void)data; (void)length;
    static int dummyBuffer = 1;
    return (RytyMetalBufferHandle)&dummyBuffer;
}

void RytyMetalUpdateBuffer(RytyMetalBufferHandle handle, const void* data, unsigned long length, unsigned long offset) {
    (void)handle; (void)data; (void)length; (void)offset;
}

void RytyMetalDestroyBuffer(RytyMetalBufferHandle handle) {
    (void)handle;
}

void RytyMetalSetViewport(double x, double y, double width, double height, double znear, double zfar) {
    (void)x; (void)y; (void)width; (void)height; (void)znear; (void)zfar;
}

void RytyMetalSetScissorRect(unsigned int x, unsigned int y, unsigned int width, unsigned int height) {
    (void)x; (void)y; (void)width; (void)height;
}

void RytyMetalSetBlendMode(int blendMode) {
    (void)blendMode;
}

void RytyMetalDrawPrimitives(int primitiveType, unsigned int start, unsigned int count) {
    (void)primitiveType; (void)start; (void)count;
}

void RytyMetalDrawIndexedPrimitives(int primitiveType, unsigned int indexCount, int indexType, RytyMetalBufferHandle indexBuffer, unsigned int indexBufferOffset) {
    (void)primitiveType; (void)indexCount; (void)indexType; (void)indexBuffer; (void)indexBufferOffset;
}

unsigned long RytyMetalGetMaxThreadsPerThreadgroup(void) {
    return 1024;
}

int RytyMetalSupportsFeatureSet(int featureSet) {
    (void)featureSet;
    return 1;
}
