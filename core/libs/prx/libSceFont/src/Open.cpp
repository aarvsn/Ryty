// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "prx/libSceFont/include/FontInternal.hpp"

namespace {

using namespace Font;

constexpr std::array<std::uint32_t, 88> SYSTEM_FONT_SET_TYPES = {
    0x18070043u, 0x18070044u, 0x18070045u, 0x18070047u, 0x18070053u, 0x18070054u,
    0x18070055u, 0x18070057u, 0x180700C3u, 0x180700C4u, 0x180700C5u, 0x180700C7u,
    0x18070444u, 0x18070447u, 0x18070454u, 0x18070457u, 0x180704C4u, 0x180704C7u,
    0x18071053u, 0x18071054u, 0x18071055u, 0x18071057u, 0x18071454u, 0x18071457u,
    0x18072444u, 0x18072447u, 0x180724C4u, 0x180724C7u, 0x18073454u, 0x18073457u,
    0x180734D4u, 0x180734D7u, 0x18078044u, 0x180780C4u, 0x18079054u, 0x1807A044u,
    0x1807A0C4u, 0x1807A444u, 0x1807A4C4u, 0x1807AC44u, 0x1807ACC4u, 0x1807B054u,
    0x1807B0D4u, 0x1807B454u, 0x1807B4D4u, 0x1807BC54u, 0x1807BCD4u, 0x18080444u,
    0x18080447u, 0x18080454u, 0x18080457u, 0x180804C4u, 0x180804C7u, 0x18081454u,
    0x18081457u, 0x18082444u, 0x18082447u, 0x180824C4u, 0x180824C7u, 0x18083454u,
    0x18083457u, 0x180834D4u, 0x180834D7u, 0x1808A444u, 0x1808A4C4u, 0x1808B454u,
    0x1808B4D4u, 0x180C8044u, 0x180C80C4u, 0x180C9054u, 0x180CA044u, 0x180CA0C4u,
    0x180CAC44u, 0x180CACC4u, 0x180CB054u, 0x180CB0D4u, 0x180CBC54u, 0x180CBCD4u,
    0x18170043u, 0x18170044u, 0x18170045u, 0x18170047u, 0x18170444u, 0x18170447u,
    0x18370044u, 0x18370047u, 0x18370444u, 0x18370447u,
};

constexpr std::uint16_t HANDLE_OWNED = 0x10;

struct OpenRequest {
    FontLibNative* library;
    std::uint32_t modeLow;
    std::uint32_t driverMode;
    const void* source;
    std::uint32_t sourceSize;
    bool matchAddress;
    std::uint32_t subFontIndex;
    std::int32_t uniqueId;
};

FontHandle PrepareHandle(FontLibNative* library, FontHandle existing) {
    if (existing) {
        GetNativeFont(existing)->flags = 0;
        RemoveState(existing);
        return existing;
    }
    auto handle = static_cast<FontHandle>(library->iface->alloc(library->alloc_ctx, sizeof(FontHandleOpaque)));
    if (!handle) return nullptr;
    std::memset(handle, 0, sizeof(FontHandleOpaque));
    GetNativeFont(handle)->flags = HANDLE_OWNED;
    return handle;
}

void DiscardHandle(FontLibNative* library, FontHandle handle) {
    auto* font = GetNativeFont(handle);
    font->magic = 0;
    font->open_info = {};
    font->library = library;
    const std::uint16_t previousFlags = font->flags;
    font->flags = 0;
    RemoveState(handle);
    if ((previousFlags & HANDLE_OWNED) != 0) library->iface->dealloc(library->alloc_ctx, handle);
}

void InitializeOpenedHandle(FontHandleNative* font, FontLibNative* library, FontObj* obj, std::uint32_t modeLow, std::uint32_t packedId, std::uint32_t entryIndex, std::uint32_t subFontIndex) {
    const SysDriver* driver = library->sys_driver;
    font->magic = HANDLE_MAGIC;
    font->flags = static_cast<std::uint16_t>(font->flags | modeLow);
    font->open_info = {};
    font->open_info.unique_id_packed = packedId;
    font->open_info.ctx_entry_index = entryIndex;
    font->open_info.sub_font_index = subFontIndex;
    font->library = library;
    font->renderer = nullptr;
    std::memset(font->reserved_0x38, 0, sizeof(font->reserved_0x38));
    std::memset(&font->cached_style, 0, sizeof(font->cached_style));
    std::memset(font->reserved_0xcc, 0, sizeof(font->reserved_0xcc));
    std::memset(font->reserved_0xe8, 0, sizeof(font->reserved_0xe8));
    font->prevFont = nullptr;
    font->nextFont = nullptr;
    std::uint16_t metric[2] = {};
    driver->metric(obj, 0x0E00, metric);
    font->metricA = metric[0];
    driver->metric(obj, 0xEA00, metric);
    font->metricB = metric[0];
    float scale = 0.0f;
    driver->scale(obj, metric, &scale);
    font->style = {};
    font->style.dpi_x = 0x48;
    font->style.dpi_y = 0x48;
    font->style.scale_w = scale;
    font->style.scale_h = scale;
}

void SetOpenFlags(FontLibNative* library, std::uint32_t modeLow) {
    const std::uint32_t flags = library->flags;
    const std::uint32_t firstBit = modeLow == 2 ? 0x100000u : 0x200000u;
    const std::uint32_t secondBit = modeLow == 2 ? 0x10000u : 0x20000u;
    if ((flags & firstBit) != 0) library->sysfont_flags |= 1;
    if ((flags & secondBit) != 0) library->sysfont_flags |= 2;
}

std::uint32_t ClaimExternalEntry(FontCtxHeader* header, const OpenRequest& request) {
    AcquireWordLock(header->lock_word);
    auto* entries = static_cast<FontCtxEntry*>(header->base);
    std::uint32_t entryIndex = 0xFFFFFFFFu;
    if (header->max_entries != 0 && entries) {
        const auto uniqueId = static_cast<std::uint32_t>(request.uniqueId);
        const auto address = request.matchAddress ? reinterpret_cast<std::uint64_t>(request.source) : 0;
        std::uint32_t firstFree = 0xFFFFFFFFu;
        for (std::uint32_t i = 0; i < header->max_entries; ++i) {
            const auto& entry = entries[i];
            if ((entry.active != 0 && entry.unique_id == uniqueId) || (request.matchAddress && entry.font_address == address)) {
                entryIndex = i;
                break;
            }
            if (firstFree == 0xFFFFFFFFu && entry.active == 0) firstFree = i;
        }
        if (entryIndex == 0xFFFFFFFFu && firstFree != 0xFFFFFFFFu) {
            entryIndex = firstFree;
            auto& entry = entries[entryIndex];
            entry = {};
            entry.active = 1;
            entry.font_address = address;
            entry.unique_id = uniqueId != 0xFFFFFFFFu ? uniqueId : (entryIndex ^ 0x80000000u);
        }
    }
    std::atomic_ref<std::uint32_t>(header->lock_word).store(0, std::memory_order_release);
    return entryIndex;
}

int OpenExternalFont(const OpenRequest& request, std::uint32_t libraryLock, FontHandle* pFontHandle) {
    FontLibNative* library = request.library;
    FontHandle handle = PrepareHandle(library, *pFontHandle);
    if (!handle) {
        ReleaseLibraryLock(library, libraryLock);
        *pFontHandle = nullptr;
        return SCE_FONT_ERROR_ALLOCATION_FAILED;
    }
    const auto fail = [&](int rc) {
        DiscardHandle(library, handle);
        ReleaseLibraryLock(library, libraryLock);
        *pFontHandle = nullptr;
        return rc;
    };
    auto* header = static_cast<FontCtxHeader*>(library->external_fonts_ctx);
    const std::uint32_t entryIndex = ClaimExternalEntry(header, request);
    if (entryIndex == 0xFFFFFFFFu) return fail(SCE_FONT_ERROR_FONT_OPEN_MAX);
    const std::uint32_t packedId = request.uniqueId == -1 ? entryIndex + 0x40000000u : (static_cast<std::uint32_t>(request.uniqueId) | 0x80000000u);
    FontObj* fontObj = nullptr;
    std::uint32_t lockWord = 0;
    auto* entry = AcquireFontCtxEntry(header, entryIndex, request.modeLow, &fontObj, &lockWord);
    if (!entry) return fail(SCE_FONT_ERROR_FONT_OPEN_MAX);
    const SysDriver* driver = library->sys_driver;
    if (!driver->open || !driver->metric || !driver->scale) {
        ReleaseFontCtxEntryLock(entry, request.modeLow, lockWord);
        return fail(SCE_FONT_ERROR_INVALID_LIBRARY);
    }
    int rc = SCE_FONT_OK;
    bool needOpen = (lockWord & OPEN_BIT) == 0;
    if (!needOpen) {
        rc = SCE_FONT_ERROR_FONT_OPEN_MAX;
        if ((lockWord & COUNT_MASK) != COUNT_MASK) {
            if (FontObj* node = FindSubFont(fontObj, request.subFontIndex)) {
                ++node->refcount;
                fontObj = node;
                rc = SCE_FONT_OK;
            } else {
                needOpen = true;
            }
        }
    }
    if (needOpen) {
        SetOpenFlags(library, request.modeLow);
        rc = driver->open(library, request.driverMode, request.source, request.sourceSize, request.subFontIndex, packedId, &fontObj);
        library->sysfont_flags = 0;
        if (rc == SCE_FONT_OK) {
            *EntryObjectSlot(entry, request.modeLow) = fontObj;
            lockWord |= OPEN_BIT;
        }
    }
    if (rc == SCE_FONT_OK) {
        InitializeOpenedHandle(GetNativeFont(handle), library, fontObj, request.modeLow, packedId, entryIndex, request.subFontIndex);
        ++lockWord;
        *pFontHandle = handle;
    }
    ReleaseFontCtxEntryLock(entry, request.modeLow, lockWord);
    if (rc != SCE_FONT_OK) return fail(rc);
    if ((GetNativeFont(handle)->flags & HANDLE_OWNED) != 0) LinkFontToLibrary(library, handle);
    ReleaseLibraryLock(library, libraryLock);
    return SCE_FONT_OK;
}

int BeginOpen(FontLibrary library, FontHandle* pFontHandle, std::uint32_t& libraryLock) {
    auto* lib = static_cast<FontLibNative*>(library);
    if (!lib || lib->magic != LIBRARY_MAGIC || !AcquireLibraryLock(lib, libraryLock)) {
        if (pFontHandle) *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_LIBRARY;
    }
    int rc = SCE_FONT_OK;
    if (!lib->fontset_registry || !lib->sys_driver || !lib->iface || !lib->iface->alloc || !lib->iface->dealloc) {
        rc = SCE_FONT_ERROR_INVALID_LIBRARY;
    } else if (!lib->external_fonts_ctx) {
        rc = SCE_FONT_ERROR_NO_SUPPORT_FUNCTION;
    }
    if (rc != SCE_FONT_OK) {
        ReleaseLibraryLock(lib, libraryLock);
        if (pFontHandle) *pFontHandle = nullptr;
    }
    return rc;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

void CopyStyleToCache(FontHandleNative* font) {
    std::memcpy(&font->cached_style.state, &font->style, sizeof(StyleStateBlock));
}

}

#pragma GCC visibility push(default)

extern "C" {

int APS5_VABI sceFontOpenFontMemory(FontLibrary library, const void* fontAddress, std::uint32_t fontSize, const FontOpenDetail* openDetail, FontHandle* pFontHandle) {
    std::uint32_t libraryLock = 0;
    const int beginRc = BeginOpen(library, pFontHandle, libraryLock);
    if (beginRc != SCE_FONT_OK) return beginRc;
    auto* lib = static_cast<FontLibNative*>(library);
    const std::uint32_t subFontIndex = openDetail ? openDetail->subfont_index : 0u;
    const std::int32_t uniqueId = openDetail ? openDetail->unique_id : -1;
    if (!fontAddress || fontSize == 0 || uniqueId < -1 || !pFontHandle) {
        ReleaseLibraryLock(lib, libraryLock);
        if (pFontHandle) *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    const OpenRequest request{lib, 1, 1, fontAddress, fontSize, true, subFontIndex, uniqueId};
    const int rc = OpenExternalFont(request, libraryLock, pFontHandle);
    if (rc != SCE_FONT_OK) return rc;
    const auto* bytes = static_cast<const unsigned char*>(fontAddress);
    LoadStateFace(ResetState(*pFontHandle), std::vector<unsigned char>(bytes, bytes + fontSize), subFontIndex);
    return SCE_FONT_OK;
}

int APS5_VABI sceFontOpenFontFile(FontLibrary library, const char* path, std::uint32_t openMode, const FontOpenDetail* openDetail, FontHandle* pFontHandle) {
    if (!library) {
        if (pFontHandle) *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_LIBRARY;
    }
    if (!pFontHandle) return SCE_FONT_ERROR_INVALID_PARAMETER;
    const std::filesystem::path hostPath = path ? ResolvePath_nid_no_patch(path) : std::filesystem::path{};
    const std::string hostPathString = hostPath.string();
    std::uint32_t libraryLock = 0;
    const int beginRc = BeginOpen(library, pFontHandle, libraryLock);
    if (beginRc != SCE_FONT_OK) return beginRc;
    auto* lib = static_cast<FontLibNative*>(library);
    const std::uint32_t subFontIndex = openDetail ? openDetail->subfont_index : 0u;
    const std::int32_t uniqueId = openDetail ? openDetail->unique_id : -1;
    const std::uint32_t modeLow = openMode & 0x0Fu;
    if (!path || uniqueId < -1 || modeLow < 1 || modeLow > 3 || hostPathString.empty()) {
        ReleaseLibraryLock(lib, libraryLock);
        *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_PARAMETER;
    }
    const OpenRequest request{lib, modeLow, modeLow + 4, hostPathString.c_str(), 0, false, subFontIndex, uniqueId};
    const int rc = OpenExternalFont(request, libraryLock, pFontHandle);
    if (rc != SCE_FONT_OK) return rc;
    std::vector<unsigned char> bytes;
    FontState& state = ResetState(*pFontHandle);
    if (ReadFileBytes(hostPath, bytes)) LoadStateFace(state, std::move(bytes), subFontIndex);
    return SCE_FONT_OK;
}

int APS5_VABI sceFontOpenFontInstance(FontHandle fontHandle, FontHandle setupFont, FontHandle* pFontHandle) {
    if (!fontHandle) {
        if (pFontHandle) *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    }
    if (!setupFont && !pFontHandle) return SCE_FONT_ERROR_INVALID_PARAMETER;
    auto* source = GetNativeFont(fontHandle);
    std::uint32_t sourceLock = 0;
    if (source->magic != HANDLE_MAGIC || !AcquireFontLock(source, sourceLock)) {
        if (pFontHandle) *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    }
    const auto fail = [&](int rc) {
        ReleaseFontLock(source, sourceLock);
        if (pFontHandle) *pFontHandle = nullptr;
        return rc;
    };
    auto* library = static_cast<FontLibNative*>(source->library);
    if (!library || library->magic != LIBRARY_MAGIC) return fail(SCE_FONT_ERROR_INVALID_FONT_HANDLE);
    const std::uint32_t modeLow = source->flags & 0x0Fu;
    const std::uint32_t subFontIndex = source->open_info.sub_font_index;
    const std::uint32_t entryIndex = source->open_info.ctx_entry_index;
    auto* header = static_cast<FontCtxHeader*>(library->external_fonts_ctx);
    if (!header) return fail(SCE_FONT_ERROR_INVALID_FONT_HANDLE);
    FontHandle target = setupFont;
    bool owned = false;
    if (!target) {
        if (!library->iface || !library->iface->alloc || !library->iface->dealloc) return fail(SCE_FONT_ERROR_INVALID_FONT_HANDLE);
        target = static_cast<FontHandle>(library->iface->alloc(library->alloc_ctx, sizeof(FontHandleOpaque)));
        if (!target) return fail(SCE_FONT_ERROR_ALLOCATION_FAILED);
        owned = true;
    } else {
        RemoveState(target);
    }
    std::memcpy(target, fontHandle, sizeof(FontHandleOpaque));
    auto* font = GetNativeFont(target);
    font->lock_word = 0;
    font->cached_style.cache_lock_word = 0;
    font->flags = static_cast<std::uint16_t>((font->flags & 0xFF0Fu) | (owned ? HANDLE_OWNED : 0u));
    auto* entries = static_cast<FontCtxEntry*>(header->base);
    if (!entries || header->max_entries < 1) {
        font->magic = 0;
        if (owned) library->iface->dealloc(library->alloc_ctx, target);
        return fail(SCE_FONT_ERROR_INVALID_PARAMETER);
    }
    int rc = SCE_FONT_OK;
    if (static_cast<std::int32_t>(entryIndex) >= 0) {
        if (entryIndex >= header->max_entries || modeLow < 1 || modeLow > 3) {
            rc = SCE_FONT_ERROR_INVALID_PARAMETER;
        } else {
            FontCtxEntry* entry = entries + entryIndex;
            std::uint32_t* lockWord = EntryLockWord(entry, modeLow);
            const std::uint32_t previous = AcquireWordLock(*lockWord);
            FontObj* node = FindSubFont(static_cast<FontObj*>(*EntryObjectSlot(entry, modeLow)), subFontIndex);
            std::uint32_t updated = previous;
            if ((previous & OPEN_BIT) == 0) {
                rc = SCE_FONT_ERROR_INVALID_FONT_HANDLE;
            } else if ((previous & COUNT_MASK) == COUNT_MASK) {
                rc = SCE_FONT_ERROR_FONT_OPEN_MAX;
            } else if (!node) {
                rc = SCE_FONT_ERROR_FATAL;
            } else {
                ++node->refcount;
                ++updated;
            }
            std::atomic_ref<std::uint32_t>(*lockWord).store(updated & ~LOCK_BIT, std::memory_order_release);
        }
    }
    if (rc != SCE_FONT_OK) {
        font->magic = 0;
        font->open_info = {};
        font->library = library;
        const std::uint16_t previousFlags = font->flags;
        font->flags = 0;
        if ((previousFlags & HANDLE_OWNED) != 0) library->iface->dealloc(library->alloc_ctx, target);
        RemoveState(target);
        return fail(rc);
    }
    if (owned) LinkFontToLibrary(library, target);
    ReleaseFontLock(source, sourceLock);
    FontState& targetState = ResetState(target);
    if (FontState* sourceState = TryGetState(fontHandle)) {
        targetState.scaleW = sourceState->scaleW;
        targetState.scaleH = sourceState->scaleH;
        if (!sourceState->faceData.empty()) LoadStateFace(targetState, sourceState->faceData, subFontIndex);
    }
    if (pFontHandle) *pFontHandle = target;
    return SCE_FONT_OK;
}

int APS5_VABI sceFontOpenFontSet(FontLibrary library, std::uint32_t fontSetType, std::uint32_t openMode, const FontOpenDetail* openDetail, FontHandle* pFontHandle) {
    (void)openDetail;
    auto* lib = static_cast<FontLibNative*>(library);
    if (!lib || lib->magic != LIBRARY_MAGIC) {
        if (pFontHandle) *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_LIBRARY;
    }
    if (!pFontHandle) return SCE_FONT_ERROR_INVALID_PARAMETER;
    std::uint32_t libraryLock = 0;
    if (!AcquireLibraryLock(lib, libraryLock)) {
        *pFontHandle = nullptr;
        return SCE_FONT_ERROR_INVALID_LIBRARY;
    }
    int rc;
    if (openMode != 1 && openMode != 2 && openMode != 3) {
        rc = SCE_FONT_ERROR_INVALID_PARAMETER;
    } else if (!lib->fontset_registry || !lib->sys_driver) {
        rc = SCE_FONT_ERROR_INVALID_LIBRARY;
    } else if (!lib->iface || !lib->iface->alloc || !lib->iface->dealloc) {
        rc = SCE_FONT_ERROR_INVALID_MEMORY;
    } else if (!lib->sysfonts_ctx) {
        rc = SCE_FONT_ERROR_NO_SUPPORT_FUNCTION;
    } else if (std::find(SYSTEM_FONT_SET_TYPES.begin(), SYSTEM_FONT_SET_TYPES.end(), fontSetType) == SYSTEM_FONT_SET_TYPES.end()) {
        rc = SCE_FONT_ERROR_NO_SUPPORT_FONTSET;
    } else {
        rc = SCE_FONT_ERROR_FONT_OPEN_FAILED;
    }
    ReleaseLibraryLock(lib, libraryLock);
    *pFontHandle = nullptr;
    return rc;
}

int APS5_VABI sceFontCloseFont(FontHandle fontHandle) {
    auto* font = GetNativeFont(fontHandle);
    std::uint32_t fontLock = 0;
    if (!font || font->magic != HANDLE_MAGIC || !AcquireFontLock(font, fontLock)) return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    auto* library = static_cast<FontLibNative*>(font->library);
    std::uint32_t libraryLock = 0;
    if (!library || library->magic != LIBRARY_MAGIC || !AcquireLibraryLock(library, libraryLock)) {
        ReleaseFontLock(font, fontLock);
        return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    }
    ReleaseFontObjectsForHandle(font);
    const std::uint16_t flags = font->flags;
    if ((flags & HANDLE_OWNED) != 0) UnlinkFontFromLibrary(library, fontHandle);
    RemoveState(fontHandle);
    font->magic = 0;
    font->open_info = {};
    font->renderer = nullptr;
    font->flags = 0;
    font->lock_word = 0;
    if ((flags & HANDLE_OWNED) != 0) library->iface->dealloc(library->alloc_ctx, fontHandle);
    ReleaseLibraryLock(library, libraryLock);
    return SCE_FONT_OK;
}

int APS5_VABI sceFontGetLibrary(FontHandle fontHandle, FontLibrary* pLibrary) {
    if (!pLibrary) return SCE_FONT_ERROR_INVALID_PARAMETER;
    const auto* font = GetNativeFont(fontHandle);
    if (!font || font->magic != HANDLE_MAGIC) {
        *pLibrary = nullptr;
        return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    }
    *pLibrary = font->library;
    return SCE_FONT_OK;
}

int APS5_VABI sceFontBindRenderer(FontHandle fontHandle, FontRenderer renderer) {
    auto* font = GetNativeFont(fontHandle);
    std::uint32_t fontLock = 0;
    if (!font || !AcquireFontLock(font, fontLock)) return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    std::uint32_t cachedLock = 0;
    if (!AcquireCachedStyleLock(font, cachedLock)) {
        ReleaseFontLock(font, fontLock);
        return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    }
    int rc = SCE_FONT_ERROR_ALREADY_BOUND_RENDERER;
    if (!font->renderer) {
        rc = SCE_FONT_ERROR_INVALID_RENDERER;
        if (renderer && static_cast<const RendererNative*>(renderer)->magic == RENDERER_MAGIC) {
            CopyStyleToCache(font);
            font->renderer = renderer;
            rc = SCE_FONT_OK;
        }
    }
    ReleaseCachedStyleLock(font, cachedLock);
    ReleaseFontLock(font, fontLock);
    return rc;
}

int APS5_VABI sceFontRebindRenderer(FontHandle fontHandle) {
    auto* font = GetNativeFont(fontHandle);
    if (!font || font->magic != HANDLE_MAGIC) return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    std::uint32_t fontLock = 0;
    if (!AcquireFontLock(font, fontLock)) return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    std::uint32_t cachedLock = 0;
    if (!AcquireCachedStyleLock(font, cachedLock)) {
        ReleaseFontLock(font, fontLock);
        return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    }
    int rc;
    if (!font->renderer) {
        rc = SCE_FONT_ERROR_NOT_BOUND_RENDERER;
    } else if (static_cast<const RendererNative*>(font->renderer)->magic != RENDERER_MAGIC) {
        rc = SCE_FONT_ERROR_FATAL;
    } else {
        CopyStyleToCache(font);
        rc = SCE_FONT_OK;
    }
    ReleaseCachedStyleLock(font, cachedLock);
    ReleaseFontLock(font, fontLock);
    return rc;
}

int APS5_VABI sceFontUnbindRenderer(FontHandle fontHandle) {
    auto* font = GetNativeFont(fontHandle);
    std::uint32_t cachedLock = 0;
    if (!font || !AcquireCachedStyleLock(font, cachedLock)) return SCE_FONT_ERROR_INVALID_FONT_HANDLE;
    int rc = SCE_FONT_ERROR_NOT_BOUND_RENDERER;
    if (font->renderer) {
        font->renderer = nullptr;
        rc = SCE_FONT_OK;
    }
    ReleaseCachedStyleLock(font, cachedLock);
    return rc;
}

}

#pragma GCC visibility pop
