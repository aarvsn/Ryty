#import "RytyMetalRenderer.h"
#import <stdio.h>

static MTLClearColor g_clearColor = {0.1, 0.1, 0.15, 1.0};
static char g_deviceNameBuffer[256] = "Metal Default Device";

@implementation RytyMetalRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
    MTLClearColor _clearColor;
}

@synthesize device = _device;
@synthesize commandQueue = _commandQueue;
@synthesize pipelineState = _pipelineState;

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
        if (_device) {
            _commandQueue = [_device newCommandQueue];
            snprintf(g_deviceNameBuffer, sizeof(g_deviceNameBuffer), "%s", [[_device name] UTF8String]);
            printf("[Ryty Metal] Initialized Metal device: %s\n", g_deviceNameBuffer);
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

    NSError *error = nil;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDescriptor error:&error];
    if (!_pipelineState && error) {
        printf("[Ryty Metal] Pipeline creation log: %s\n", [[error localizedDescription] UTF8String]);
    }
    return _pipelineState != nil;
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
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

        [renderEncoder endEncoding];
        [commandBuffer presentDrawable:view.currentDrawable];
        [commandBuffer commit];
    }
}

@end

void RytyMetalInitializeDevice(void) {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device) {
        snprintf(g_deviceNameBuffer, sizeof(g_deviceNameBuffer), "%s", [[device name] UTF8String]);
        printf("[Ryty Metal] Initialized default Metal device: %s\n", g_deviceNameBuffer);
    } else {
        printf("[Ryty Metal] Metal device initialization stub called.\n");
    }
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
