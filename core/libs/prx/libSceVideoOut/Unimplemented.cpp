#include <cstdint>
#include <cstddef>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libSceVideoOut/include/Output.hpp"
#include <stdexcept>

extern "C" {

int APS5_VABI sceVideoOutAdjustColor_(int handle, const VideoOutColorSettings* settings, uint32_t settings_size) {
    if (settings_size < sizeof(VideoOutColorSettings)) throw std::runtime_error("sceVideoOutAdjustColor_: VIDEO_OUT_ERROR_INVALID_VALUE");
    return sceVideoOutAdjustColor(handle, settings);
}

int APS5_VABI sceVideoOutColorSettingsSetGamma_(VideoOutColorSettings* settings, float gamma, uint32_t settings_size) {
    if (settings_size < sizeof(VideoOutColorSettings)) throw std::runtime_error("sceVideoOutColorSettingsSetGamma_: VIDEO_OUT_ERROR_INVALID_VALUE");
    return sceVideoOutColorSettingsSetGamma(settings, gamma);
}

struct VideoOutOutputMode {
    int aspect;
    int scanMode;
    int format;
    int refreshRate;
    int resolution;
};

int APS5_VABI sceVideoOutGetOutputMode(int handle, VideoOutOutputMode* mode) {
    (void)handle;
    if (mode) {
        mode->aspect = 0; // 16:9
        mode->scanMode = 0; // progressive
        mode->format = 0; // RGB
        mode->refreshRate = 60;
        mode->resolution = 0; // 1080p
    }
    return 0;
}

}
