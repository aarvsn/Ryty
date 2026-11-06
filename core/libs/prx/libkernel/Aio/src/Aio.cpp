#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include "SceTypes.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/KernelErrors.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <limits>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

constexpr std::int32_t AioProcessing = 2;
constexpr std::int32_t AioCompleted = 3;
constexpr std::int32_t AioAborted = 4;
constexpr std::int32_t AioMaxQueues = 512;

std::mutex g_mutex;
std::array<std::int32_t, AioMaxQueues> g_states{};
std::int32_t g_nextId = 1;

bool ValidId(std::int32_t id) {
    return id > 0 && id < AioMaxQueues;
}

std::int32_t AllocateId() {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::int32_t id = g_nextId;
    g_nextId = (g_nextId + 1) % AioMaxQueues;
    if (g_nextId == 0) g_nextId = 1;
    g_states[id] = AioProcessing;
    return id;
}

void SetState(std::int32_t id, std::int32_t state) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_states[id] = state;
}

std::int64_t NativePread(std::int32_t fd, void* buf, std::size_t nbyte, std::int64_t offset) {
#ifdef _WIN32
    if (nbyte > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        throw std::runtime_error("sceKernelAioSubmitReadCommands: nbytes exceeds platform limit");
    }
    const int duped = ::_dup(fd);
    if (duped < 0) return -1;
    if (::_lseeki64(duped, offset, SEEK_SET) < 0) {
        const int error = errno;
        ::_close(duped);
        errno = error;
        return -1;
    }
    const int result = ::_read(duped, buf, static_cast<unsigned int>(nbyte));
    const int error = errno;
    ::_close(duped);
    errno = error;
    return result;
#else
    return static_cast<std::int64_t>(::pread(fd, buf, nbyte, static_cast<off_t>(offset)));
#endif
}

std::int64_t NativePwrite(std::int32_t fd, const void* buf, std::size_t nbyte, std::int64_t offset) {
#ifdef _WIN32
    if (nbyte > static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())) {
        throw std::runtime_error("sceKernelAioSubmitWriteCommands: nbytes exceeds platform limit");
    }
    const int duped = ::_dup(fd);
    if (duped < 0) return -1;
    if (::_lseeki64(duped, offset, SEEK_SET) < 0) {
        const int error = errno;
        ::_close(duped);
        errno = error;
        return -1;
    }
    const int result = ::_write(duped, buf, static_cast<unsigned int>(nbyte));
    const int error = errno;
    ::_close(duped);
    errno = error;
    return result;
#else
    return static_cast<std::int64_t>(::pwrite(fd, buf, nbyte, static_cast<off_t>(offset)));
#endif
}

int SubmitCommands(KernelAioRwRequest* req, std::int32_t size, bool write, std::int32_t* id) {
    if (req == nullptr || id == nullptr) return SCE_KERNEL_ERROR_EFAULT;
    if (size <= 0) return SCE_KERNEL_ERROR_EINVAL;
    for (std::int32_t i = 0; i < size; ++i) {
        if (req[i].result == nullptr) return SCE_KERNEL_ERROR_EFAULT;
    }
    const std::int32_t queue = AllocateId();
    bool aborted = false;
    for (std::int32_t i = 0; i < size; ++i) {
        const std::int64_t done = write
            ? NativePwrite(req[i].fd, req[i].buf, req[i].nbyte, req[i].offset)
            : NativePread(req[i].fd, req[i].buf, req[i].nbyte, req[i].offset);
        if (done < 0) {
            const int error = errno;
            req[i].result->return_value = static_cast<std::int64_t>(SCE_KERNEL_ERROR_EIO);
            if (error == EBADF) req[i].result->return_value = static_cast<std::int64_t>(SCE_KERNEL_ERROR_EBADF);
            req[i].result->state = AioAborted;
            aborted = true;
        } else {
            req[i].result->return_value = done;
            req[i].result->state = AioCompleted;
        }
    }
    SetState(queue, aborted ? AioAborted : AioCompleted);
    *id = queue;
    return 0;
}

}

extern "C" {

int APS5_VABI sceKernelAioDeleteRequest(int32_t id, int32_t* ret) {
    if (ret == nullptr) return SCE_KERNEL_ERROR_EFAULT;
    if (!ValidId(id)) return SCE_KERNEL_ERROR_EINVAL;
    SetState(id, AioAborted);
    *ret = 0;
    return 0;
}

int APS5_VABI sceKernelAioInitializeImpl(void* param, int32_t size) {
    (void)param;
    (void)size;
    return 0;
}

void APS5_VABI sceKernelAioInitializeParam(void* param) {
    if (param == nullptr) throw std::invalid_argument("sceKernelAioInitializeParam: param is null");
}

int APS5_VABI sceKernelAioSubmitReadCommands(KernelAioRwRequest* req, int32_t size, int32_t prio, int32_t* id) {
    (void)prio;
    return SubmitCommands(req, size, false, id);
}

int APS5_VABI sceKernelAioSubmitWriteCommands(KernelAioRwRequest* req, int32_t size, int32_t prio, int32_t* id) {
    (void)prio;
    return SubmitCommands(req, size, true, id);
}

int APS5_VABI sceKernelAioPollRequest(int32_t id, int32_t* state) {
    if (state == nullptr) return SCE_KERNEL_ERROR_EFAULT;
    if (!ValidId(id)) return SCE_KERNEL_ERROR_EINVAL;
    std::lock_guard<std::mutex> lock(g_mutex);
    *state = g_states[id];
    return 0;
}

int APS5_VABI sceKernelAioWaitRequest(int32_t id, int32_t* state, uint32_t* usec) {
    if (state == nullptr) return SCE_KERNEL_ERROR_EFAULT;
    if (!ValidId(id)) return SCE_KERNEL_ERROR_EINVAL;
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        std::int32_t current;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            current = g_states[id];
        }
        if (current != AioProcessing) {
            *state = current;
            return 0;
        }
        if (usec != nullptr && *usec != 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            if (elapsed > static_cast<std::int64_t>(*usec)) {
                *state = current;
                return SCE_KERNEL_ERROR_ETIMEDOUT;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
}

}
