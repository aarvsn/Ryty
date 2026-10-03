#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINECACHE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINECACHE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace AgcDriver::Graphics {

class PipelineCache {
public:
    static constexpr int SaveIntervalSeconds = 10;

    PipelineCache(const Context& context, const VkPhysicalDeviceProperties& properties);
    ~PipelineCache();

    PipelineCache(const PipelineCache&) = delete;
    PipelineCache& operator=(const PipelineCache&) = delete;
    VkPipelineCache Handle() const { return cache; }

private:
    void load(std::vector<std::byte>& initialData);
    void save(bool final);
    void run();

    Context context;
    VkPhysicalDeviceProperties properties{};
    VkPipelineCache cache = VK_NULL_HANDLE;
    std::filesystem::path path;
    std::size_t savedBytes = 0;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::thread saver;
};

}

#endif
