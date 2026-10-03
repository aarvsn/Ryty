#import "RytyMetalRenderer.h"
#import <stdio.h>

@implementation RytyMetalRenderer {
    id<MTLDevice> _device;
    id<MTLCommandQueue> _commandQueue;
    id<MTLRenderPipelineState> _pipelineState;
}

- (nonnull instancetype)initWithMetalKitView:(nonnull MTKView *)mtkView {
    self = [super init];
    if (self) {
        _device = mtkView.device;
        if (!_device) {
            _device = MTLCreateSystemDefaultDevice();
            mtkView.device = _device;
        }
        if (_device) {
            _commandQueue = [_device newCommandQueue];
            printf("[Ryty Metal] Initialized Metal device: %s\n", [[_device name] UTF8String]);
        } else {
            printf("[Ryty Metal] Error: Metal is not supported on this device.\n");
        }
    }
    return self;
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size {
    (void)view;
    (void)size;
}

- (void)drawInMTKView:(nonnull MTKView *)view {
    if (!_commandQueue) return;

    MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;
    if (renderPassDescriptor != nil) {
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
        printf("[Ryty Metal] Initialized default Metal device: %s\n", [[device name] UTF8String]);
    } else {
        printf("[Ryty Metal] Metal device initialization stub called.\n");
    }
}

void RytyMetalRenderFrame(void) {
    printf("[Ryty Metal] Metal frame render step executed.\n");
}
