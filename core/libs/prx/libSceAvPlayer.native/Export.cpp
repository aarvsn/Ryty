// libSceAvPlayer: player lifecycle without media decoding. A source is accepted, the event callback sees the usual
// READY / PLAY transitions, and the video getters hand out frames of a fixed-length blank clip on a wall-clock
// media clock before the stream reports as finished (STOP). The frame budget matters: a title whose intro sequence
// is driven by the frame getters stops asking only once it has been given frames and then an end-of-stream, so
// answering every poll with "no frame" strands it in its own wait loop. No frame is ever decoded, so the picture
// is black.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "SceTypes.hpp"
#include "HitLog.hpp"
#include "prx/libc/include/General.hpp"

namespace {
constexpr std::int32_t kEventStop = 0x01;
constexpr std::int32_t kEventReady = 0x02;
constexpr std::int32_t kEventPlay = 0x03;
constexpr std::int32_t kEventPause = 0x04;
constexpr std::int32_t kEventWarning = 0x20;
constexpr std::int32_t kErrInvalidParams = static_cast<std::int32_t>(0x806a0001u);
constexpr std::int32_t kErrOperationFailed = static_cast<std::int32_t>(0x806a0002u);
constexpr std::uint32_t kWarningJumpComplete = 0x806a00a3u;
constexpr std::uint64_t kMagic = 0x5041564c50594c41ull;

// The blank clip this player "plays". Both the stream info and every delivered frame are described by these
// values, so the title's own texture sizing and the surface it is handed cannot disagree.
constexpr std::uint32_t kFrameWidth = 1920;
constexpr std::uint32_t kFrameHeight = 1080;
// NV12 rows are pitched to 256 bytes; SceAvPlayerVideoEx::width publishes that coded extent, not the visible
// one, with the difference reported through cropRightOffset.
constexpr std::uint32_t kFramePitch = (kFrameWidth + 255u) & ~255u;
constexpr std::size_t kFrameBytes = static_cast<std::size_t>(kFramePitch) * kFrameHeight * 3u / 2u;
// Limited-range black, not zero: Y=U=V=0 converts to mid green in both BT.601 and BT.709, and a title that
// samples the coded extent would read the pitch padding as a green stripe down the right edge.
constexpr std::uint8_t kBlackLuma = 0x10;
constexpr std::uint8_t kBlackChroma = 0x80;
constexpr std::uint64_t kFrameIntervalMs = 33;
constexpr std::uint64_t kFrameBudget = 120;
constexpr std::uint64_t kStreamDurationMs = kFrameBudget * kFrameIntervalMs;
constexpr double kFrameRate = 1000.0 / static_cast<double>(kFrameIntervalMs);

using EventCallback = void (APS5_VABI *)(void*, std::int32_t, std::int32_t, void*);
using TextureAllocator = void* (APS5_VABI *)(void*, std::uint32_t, std::uint32_t);
using TextureFree = void (APS5_VABI *)(void*, void*);

// SceAvPlayerVideoEx. The offsets are asserted below because the guest reads them by position, not by name.
struct VideoDetailsEx {
    std::uint32_t width;
    std::uint32_t height;
    float aspectRatio;
    std::uint8_t language[4];
    std::uint8_t reserved0[4];
    std::uint32_t cropLeftOffset;
    std::uint32_t cropRightOffset;
    std::uint32_t cropTopOffset;
    std::uint32_t cropBottomOffset;
    std::uint32_t pitch;
    std::uint8_t lumaBitDepth;
    std::uint8_t chromaBitDepth;
    std::uint8_t fullRange;
    std::uint8_t reserved1[5];
    double frameRate;
    std::uint32_t colourPrimaries;
    std::uint32_t transferCharacteristics;
    std::uint8_t reserved2[16];
};
static_assert(sizeof(VideoDetailsEx) == 80, "SceAvPlayerVideoEx ABI");
static_assert(offsetof(VideoDetailsEx, cropLeftOffset) == 20, "SceAvPlayerVideoEx ABI");
static_assert(offsetof(VideoDetailsEx, pitch) == 36, "SceAvPlayerVideoEx ABI");
static_assert(offsetof(VideoDetailsEx, frameRate) == 48, "SceAvPlayerVideoEx ABI");

// SceAvPlayerFrameInfo / SceAvPlayerFrameInfoEx: the out-param of the video getters. The Ex variant carries
// the wide details union; both put their timestamp at 16 and their details at 24.
struct FrameInfo {
    void* data;
    std::uint8_t reserved[4];
    std::uint64_t timestamp;
    std::uint32_t width;
    std::uint32_t height;
    float aspectRatio;
    std::uint8_t language[4];
};
struct FrameInfoEx {
    void* data;
    std::uint8_t reserved[4];
    std::uint64_t timestamp;
    VideoDetailsEx video;
};
static_assert(sizeof(FrameInfo) == 40 && offsetof(FrameInfo, timestamp) == 16, "SceAvPlayerFrameInfo ABI");
static_assert(sizeof(FrameInfoEx) == 104 && offsetof(FrameInfoEx, video) == 24, "SceAvPlayerFrameInfoEx ABI");

// Prefix of sceAvPlayerInitData / sceAvPlayerInitDataEx that this player reads. The Ex layout starts with its own size.
struct InitPrefix {
    void* memObject;
    void* alloc;
    void* dealloc;
    void* allocTexture;
    void* deallocTexture;
    void* fileObject;
    void* fileOpen;
    void* fileClose;
    void* fileRead;
    void* fileSize;
    void* eventObject;
    void* eventCallback;
    std::int32_t debugLevel;
    std::uint32_t basePriority;
    std::int32_t numFramebuffers;
    std::uint8_t autoStart;
};

struct Player {
    std::uint64_t magic = kMagic;
    void* eventObject = nullptr;
    EventCallback callback = nullptr;
    void* memObject = nullptr;
    TextureAllocator allocTexture = nullptr;
    TextureFree deallocTexture = nullptr;
    bool autoStart = false;
    bool haveSource = false;
    bool playing = false;
    bool paused = false;
    bool stopFired = false;
    bool looping = false;
    bool noStorage = false;
    std::int32_t numFramebuffers = 0;
    // Media clock. A hardware decoder advances the stream on its own clock and a poll picks up whichever frame
    // is due; counting polls instead stretched this 4-second blank clip over 80 seconds of a 1.5 fps run,
    // because the title polls exactly once per host frame. bankedMs holds the media time reached at clockAnchor,
    // and a cleared anchor means the clock is stopped (paused or not yet started) or re-anchors on next read.
    std::chrono::steady_clock::time_point clockAnchor{};
    std::uint64_t bankedMs = 0;
    std::uint64_t framesDelivered = 0;
    std::string path;
    std::vector<std::uint8_t*> frames;
    std::mutex lock;
};

Player* Check(void* handle) {
    Player* p = static_cast<Player*>(handle);
    return p != nullptr && p->magic == kMagic ? p : nullptr;
}

// The callback runs guest code and may re-enter the player, so it is always invoked without the player lock held.
void Fire(Player* p, std::int32_t event, void* data = nullptr) {
    if (p->callback == nullptr) return;
    // Budgeted per event number rather than per call site: one shared APS5_HIT line would cap the whole
    // census at three events of any kind, so the READY/PLAY pair that opens a movie eats the budget and the
    // STOP that ends it is never reported.
    static std::atomic<unsigned> fired[64];
    const auto slot = static_cast<unsigned>(event) < 64 ? static_cast<unsigned>(event) : 0u;
    if (fired[slot].fetch_add(1) < 3)
        std::fprintf(stderr, "[AVP] event 0x%02x\n", event);
    p->callback(p->eventObject, event, 0, data);
}

// Caller holds the lock.
std::uint64_t MediaTime(Player& p) {
    const auto now = std::chrono::steady_clock::now();
    if (p.clockAnchor == std::chrono::steady_clock::time_point{}) {
        if (!p.playing || p.paused) return p.bankedMs;
        p.clockAnchor = now;
    }
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now - p.clockAnchor).count());
    return p.bankedMs + elapsed;
}

// Frame due at the current media time, saturating at the end of the clip.
std::uint64_t CurrentFrame(Player& p) {
    const auto frame = MediaTime(p) / kFrameIntervalMs;
    return frame > kFrameBudget ? kFrameBudget : frame;
}

// Bank the clock where it stands and stop it.
void HaltClock(Player& p) {
    p.bankedMs = MediaTime(p);
    p.clockAnchor = {};
}

// True once the blank clip has run out -- or was never going to run, because the title gave us no allocator to
// put its frames in. Both are the same thing to a polling title: no more frames will ever arrive.
bool StreamExhausted(Player& p) { return p.noStorage || CurrentFrame(p) >= kFrameBudget; }

// There is no decoder, so this player "plays" a fixed-length blank clip instead of the source. On real hardware
// the end of the stream is reported by the poll that finds no more frames, and the STOP event fires on that same
// poll -- which is the only end-of-movie signal a title gets if it pumps the data getters instead of
// sceAvPlayerIsActive. A title with an intro-logo or splash-video sequence commonly does exactly that, so STOP
// has to come from the poll, and only once the clip has actually run out: a title that is waiting for frames
// ends its sequence on the end-of-stream that follows them, not on one that arrives before the first.
// Returns true when this call is the one that ends the stream, so the caller fires the event outside the lock.
bool EndStream(Player* p) {
    std::lock_guard g(p->lock);
    if (!p->playing || p->paused || p->stopFired) return false;
    if (!StreamExhausted(*p)) return false;
    // Once per player, and outside the APS5_HIT budget: this is the line that says whether a movie was
    // allowed to finish or is stranded in a pause that never resumed.
    std::fprintf(stderr, "[AVP] end of stream: %llu frames delivered, clock at %llu ms\n",
        static_cast<unsigned long long>(CurrentFrame(*p)),
        static_cast<unsigned long long>(MediaTime(*p)));
    std::fflush(stderr);
    HaltClock(*p);
    p->playing = false;
    p->stopFired = true;
    p->haveSource = false;
    return true;
}

// The frame buffers come from the title's own texture allocator, exactly as on hardware: SceAvPlayerInitData
// hands it out alongside the memory pool precisely so frame storage lives in graphics memory, which is what lets
// the upload the title later issues from the delivered pointer be validated as guest memory. A player that
// supplies no such allocator has nowhere to put a frame, and that is reported once -- as an exhausted stream --
// rather than retried on every poll. Caller holds the lock.
bool PrepareFrames(Player& p) {
    if (!p.frames.empty() || p.noStorage) return !p.frames.empty();
    if (p.allocTexture == nullptr || p.memObject == nullptr) {
        p.noStorage = true;
        APS5_HIT("AVP", "no guest frame allocator: InitData supplied no texture allocator");
        return false;
    }
    unsigned requested = p.numFramebuffers > 0 ? static_cast<unsigned>(p.numFramebuffers) : 2u;
    if (requested < 2u) requested = 2u;
    if (requested > 8u) requested = 8u;
    const auto planeBytes = static_cast<std::size_t>(kFramePitch) * kFrameHeight;
    for (unsigned i = 0; i < requested; ++i) {
        auto* frame = static_cast<std::uint8_t*>(
            p.allocTexture(p.memObject, 0x100, static_cast<std::uint32_t>(kFrameBytes)));
        if (frame == nullptr) break;
        std::memset(frame, kBlackLuma, planeBytes);
        std::memset(frame + planeBytes, kBlackChroma, kFrameBytes - planeBytes);
        p.frames.push_back(frame);
    }
    if (p.frames.empty()) {
        p.noStorage = true;
        return false;
    }
    std::fprintf(stderr, "[AVP] blank clip: buffers=%zu bytes=%llu pitch=%u allocator=%p first=%p\n",
        p.frames.size(), static_cast<unsigned long long>(kFrameBytes), kFramePitch,
        reinterpret_cast<void*>(p.allocTexture), static_cast<void*>(p.frames.front()));
    std::fflush(stderr);
    return true;
}

// Publishes whichever frame the media clock has reached. Returns false when the clip is over, which is the poll
// that reports the end of the stream. Every frame of this clip is the same blank image, so skipping ahead on a
// slow host costs a title nothing it can observe -- unlike a real movie, where the clock would have to be
// slowed to the host's pace instead. Caller holds the lock.
bool DeliverFrame(Player& p, void*& data, std::uint64_t& timestamp) {
    if (!p.playing || p.paused || !PrepareFrames(p)) return false;
    const auto frame = CurrentFrame(p);
    if (frame >= kFrameBudget) return false;
    data = p.frames[frame % p.frames.size()];
    timestamp = frame * kFrameIntervalMs;
    if (frame + 1 > p.framesDelivered) {
        const auto previous = p.framesDelivered;
        p.framesDelivered = frame + 1;
        // Every thirtieth frame *crossed*, not every thirtieth index: on a slow host the clock jumps
        // straight over exact multiples, and an equality test there reports nothing at all.
        if (previous / 30 != p.framesDelivered / 30 || p.framesDelivered == kFrameBudget)
            std::fprintf(stderr, "[AVP] blank frame #%llu of %llu t=%llu ms\n",
                static_cast<unsigned long long>(p.framesDelivered),
                static_cast<unsigned long long>(kFrameBudget),
                static_cast<unsigned long long>(timestamp));
    }
    return true;
}

// The wide video description of the blank clip. `width` publishes the coded extent (the pitch) and the
// difference from the visible picture goes out through cropRightOffset, so a title that sizes its luma texture
// from `pitch` and a title that crops `width` both end up with the visible frame.
void WriteVideoDetails(VideoDetailsEx& out) {
    out = {};
    out.width = kFramePitch;
    out.height = kFrameHeight;
    out.aspectRatio = static_cast<float>(kFrameWidth) / static_cast<float>(kFrameHeight);
    out.cropRightOffset = kFramePitch - kFrameWidth;
    out.pitch = kFramePitch;
    out.lumaBitDepth = 8;
    out.chromaBitDepth = 8;
    out.frameRate = kFrameRate;
}

// Shared body of the two video getters. The frame is written through the guest's out-param in whichever of the
// two layouts the caller announced, and a poll that finds the clip spent is turned into the end-of-stream report
// on the way out -- outside the lock, because the STOP callback runs guest code.
Bool PollVideo(Player* p, void* frame_info, bool extended) {
 if (frame_info == nullptr) return 0;
 void* data = nullptr;
 std::uint64_t timestamp = 0;
 bool delivered = false;
 bool wasPaused = false;
 {
  std::lock_guard g(p->lock);
  wasPaused = p->paused;
  delivered = DeliverFrame(*p, data, timestamp);
  if (delivered) {
   if (extended) {
    FrameInfoEx out = {};
    out.data = data;
    out.timestamp = timestamp;
    WriteVideoDetails(out.video);
    std::memcpy(frame_info, &out, sizeof(out));
   } else {
    FrameInfo out = {};
    out.data = data;
    out.timestamp = timestamp;
    out.width = kFrameWidth;
    out.height = kFrameHeight;
    out.aspectRatio = static_cast<float>(kFrameWidth) / static_cast<float>(kFrameHeight);
    std::memcpy(frame_info, &out, sizeof(out));
   }
   return 1;
  }
 }
 // Unbudgeted (unlike APS5_HIT above): every poll that finds the player paused, so the exact interleaving
 // of the game's own Pause call against its own polling loop is visible instead of guessed at.
 if (wasPaused) {
  std::fprintf(stderr, "[AVPLIFE] poll found paused, no frame delivered\n");
  std::fflush(stderr);
 }
 if (EndStream(p)) Fire(p, kEventStop);
 return 0;
}

// HANDOFF13/14: HANDOFF12's still-open question was "why does the guest pause and never resume". These
// call-site logs (unbudgeted, unlike APS5_HIT -- we need every call, not just the first three) print the
// thread that called, the player pointer (multiple concurrent players would explain a stray pause on the
// wrong handle), and the media clock's state at the moment of the call.
void LogLifecycleCall(const char* site, Player* p) {
    std::ostringstream tid;
    tid << std::this_thread::get_id();
    std::lock_guard g(p->lock);
    std::fprintf(stderr, "[AVPLIFE] %s p=%p tid=%s playing=%d paused=%d stopFired=%d haveSource=%d framesDelivered=%llu bankedMs=%llu clockRunning=%d\n",
        site, static_cast<void*>(p), tid.str().c_str(), p->playing ? 1 : 0,
        p->paused ? 1 : 0, p->stopFired ? 1 : 0, p->haveSource ? 1 : 0,
        static_cast<unsigned long long>(p->framesDelivered), static_cast<unsigned long long>(p->bankedMs),
        p->clockAnchor != std::chrono::steady_clock::time_point{} ? 1 : 0);
    std::fflush(stderr);
}

Player* CreateFrom(const InitPrefix* d) {
    Player* p = new Player();
    if (d != nullptr) {
        p->eventObject = d->eventObject;
        p->callback = reinterpret_cast<EventCallback>(d->eventCallback);
        p->autoStart = d->autoStart != 0;
        p->memObject = d->memObject;
        p->allocTexture = reinterpret_cast<TextureAllocator>(d->allocTexture);
        p->deallocTexture = reinterpret_cast<TextureFree>(d->deallocTexture);
        p->numFramebuffers = d->numFramebuffers;
    }
    return p;
}

int AddSourceCommon(Player* p, const char* path) {
    if (p == nullptr || path == nullptr || *path == '\0') return kErrInvalidParams;
    LogLifecycleCall("AddSource(before)", p);
    bool play = false;
    {
        std::lock_guard g(p->lock);
        p->path = path;
        p->haveSource = true;
        p->playing = p->autoStart;
        p->paused = false;
        p->stopFired = false;
        p->bankedMs = 0;
        p->clockAnchor = {};
        p->framesDelivered = 0;
        play = p->autoStart;
    }
    APS5_HIT("AVP", "AddSource '%s' (no decode: %llu blank frames then end of stream)", path,
        static_cast<unsigned long long>(kFrameBudget));
    Fire(p, kEventReady);
    if (play) Fire(p, kEventPlay);
    return 0;
}
}  // namespace

extern "C" {

void* APS5_VABI sceAvPlayerInit(const void* init) {
 APS5_HIT("AVP", "sceAvPlayerInit");
 return CreateFrom(static_cast<const InitPrefix*>(init));
}
int APS5_VABI sceAvPlayerInitEx(const void* init_ex, void** handle) {
 if (handle == nullptr) return kErrInvalidParams;
 // sceAvPlayerInitDataEx begins with a 64-bit size, followed by the same allocator / file / event blocks.
 const std::uint8_t* base = static_cast<const std::uint8_t*>(init_ex);
 *handle = CreateFrom(base != nullptr ? reinterpret_cast<const InitPrefix*>(base + 8) : nullptr);
 return 0;
}
int APS5_VABI sceAvPlayerPostInit(void* h, const void* post_init) {
 (void)post_init;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
int APS5_VABI sceAvPlayerSetLogCallback(void* callback, void* user_data) {
 (void)callback;
 (void)user_data;
 return 0;
}
int APS5_VABI sceAvPlayerAddSource(void* h, const char* filename) {
 return AddSourceCommon(Check(h), filename);
}
int APS5_VABI sceAvPlayerAddSourceEx(void* h, std::uint32_t uri_type, const void* source_details) {
 (void)uri_type;
 // SceAvPlayerSourceDetails begins with { const char* name; uint32_t length; }.
 const char* name = source_details != nullptr ? *static_cast<const char* const*>(source_details) : nullptr;
 return AddSourceCommon(Check(h), name);
}
int APS5_VABI sceAvPlayerStart(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 LogLifecycleCall("Start(before)", p);
 bool play = false;
 {
  std::lock_guard g(p->lock);
  if (p->haveSource && !p->playing && !p->stopFired) { p->playing = true; p->paused = false; play = true; }
 }
 if (play) Fire(p, kEventPlay);
 return 0;
}
int APS5_VABI sceAvPlayerStartEx(void* h, const void* start_info_ex) {
 (void)start_info_ex;
 return sceAvPlayerStart(h);
}
int APS5_VABI sceAvPlayerPause(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 LogLifecycleCall("Pause(before)", p);
 bool notify = false;
 {
  std::lock_guard g(p->lock);
  if (p->playing && !p->paused) { p->paused = true; HaltClock(*p); notify = true; }
 }
 if (notify) Fire(p, kEventPause);
 return 0;
}
int APS5_VABI sceAvPlayerResume(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 LogLifecycleCall("Resume(before)", p);
 bool notify = false;
 {
  std::lock_guard g(p->lock);
  if (p->playing && p->paused) { p->paused = false; notify = true; }
 }
 if (notify) Fire(p, kEventPlay);
 return 0;
}
int APS5_VABI sceAvPlayerStop(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 LogLifecycleCall("Stop(before)", p);
 bool notify = false;
 {
  std::lock_guard g(p->lock);
  if (p->playing && !p->stopFired) { p->playing = false; HaltClock(*p); p->stopFired = true; notify = true; }
  p->haveSource = false;
 }
 if (notify) Fire(p, kEventStop);
 return 0;
}
int APS5_VABI sceAvPlayerClose(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 std::vector<std::uint8_t*> frames;
 {
  std::lock_guard g(p->lock);
  p->magic = 0;
  frames.swap(p->frames);
 }
 // The frame storage came from the title's allocator, so it goes back to it: a title that opens a player per
 // movie would otherwise lose every buffer of the clip on each close.
 if (p->deallocTexture != nullptr)
  for (auto* frame : frames) p->deallocTexture(p->memObject, frame);
 delete p;
 return 0;
}
// bool: true while the player is presenting. See EndStream: the clip is finite, so once it has run out the poll
// that finds no frame left reports the end (STOP event) and IsActive turns false. Until then it stays true, which
// is what keeps a title that pumps the getters rather than IsActive from being told the movie ended early.
Bool APS5_VABI sceAvPlayerIsActive(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return 0;
 if (EndStream(p)) Fire(p, kEventStop);
 std::lock_guard g(p->lock);
 return p->playing ? 1 : 0;
}
// The clip's own media clock: it runs on wall time while the player presents, so it agrees with the timestamps
// on the frames handed out and with the duration the stream info reports.
std::uint64_t APS5_VABI sceAvPlayerCurrentTime(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return 0;
 std::lock_guard g(p->lock);
 const auto time_ms = MediaTime(*p);
 return time_ms > kStreamDurationMs ? kStreamDurationMs : time_ms;
}
// A seek within a clip whose frames are all identical is a clock move, and that is all the title can be
// waiting for: a pull loop that discards any frame no newer than the seek target would otherwise throw away
// every frame from here on.
int APS5_VABI sceAvPlayerJumpToTime(void* h, std::uint64_t time_ms) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 LogLifecycleCall("JumpToTime(before)", p);
 std::fprintf(stderr, "[AVPLIFE] JumpToTime time_ms=%llu\n", static_cast<unsigned long long>(time_ms));
 std::fflush(stderr);
 {
  std::lock_guard g(p->lock);
  p->bankedMs = time_ms > kStreamDurationMs ? kStreamDurationMs : time_ms;
  p->clockAnchor = {};
 }
 std::uint32_t warning = kWarningJumpComplete;
 Fire(p, kEventWarning, &warning);
 return 0;
}
int APS5_VABI sceAvPlayerSetLooping(void* h, Bool loop) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 p->looping = loop != 0;
 return 0;
}
int APS5_VABI sceAvPlayerSetAvSyncMode(void* h, std::uint32_t sync_mode) {
 (void)sync_mode;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
int APS5_VABI sceAvPlayerSetTrickSpeed(void* h, std::int32_t trick_speed) {
 (void)trick_speed;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
int APS5_VABI sceAvPlayerSetAvailableBandwidth(void* h, std::uint32_t start_bandwidth, std::uint32_t minimum_bandwidth, std::uint32_t maximum_bandwidth) {
 (void)start_bandwidth;
 (void)minimum_bandwidth;
 (void)maximum_bandwidth;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
// One (placeholder) video stream once a source is present; the stream table below describes it as an empty 1920x1080 clip.
int APS5_VABI sceAvPlayerStreamCount(void* h) {
 Player* p = Check(h);
 if (p == nullptr) return kErrInvalidParams;
 std::lock_guard g(p->lock);
 return p->haveSource ? 1 : 0;
}
int APS5_VABI sceAvPlayerEnableStream(void* h, std::uint32_t stream_id) {
 (void)stream_id;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
int APS5_VABI sceAvPlayerDisableStream(void* h, std::uint32_t stream_id) {
 (void)stream_id;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
int APS5_VABI sceAvPlayerChangeStream(void* h, std::uint32_t old_stream_id, std::uint32_t new_stream_id) {
 (void)old_stream_id;
 (void)new_stream_id;
 return Check(h) != nullptr ? 0 : kErrInvalidParams;
}
// SceAvPlayerStreamInfo (32 bytes): u32 type (1 video, 2 audio), u8 reserved[4], details[16], u64 duration (ms).
// Video details: u32 width, u32 height, float aspect, char language[4].
int APS5_VABI sceAvPlayerGetStreamInfo(void* h, std::uint32_t stream_id, void* info) {
 Player* p = Check(h);
 if (p == nullptr || info == nullptr) return kErrInvalidParams;
 std::lock_guard g(p->lock);
 if (!p->haveSource || stream_id != 0) return kErrOperationFailed;
 std::uint8_t* out = static_cast<std::uint8_t*>(info);
 std::memset(out, 0, 32);
 const std::uint32_t type = 1;
 const std::uint32_t width = kFrameWidth;
 const std::uint32_t height = kFrameHeight;
 const float aspect = static_cast<float>(kFrameWidth) / static_cast<float>(kFrameHeight);
 const std::uint64_t duration = kStreamDurationMs;
 std::memcpy(out + 0, &type, 4);
 std::memcpy(out + 8, &width, 4);
 std::memcpy(out + 12, &height, 4);
 std::memcpy(out + 16, &aspect, 4);
 std::memcpy(out + 24, &duration, 8);
 return 0;
}
// SceAvPlayerStreamInfoEx (104 bytes): u64 size, u32 type, reserved[4], details[80], u64 duration. The details
// union holds the same wide video description the frame getters publish, so a title that sizes its movie
// surfaces from the stream info instead of from a frame gets one answer, not two.
int APS5_VABI sceAvPlayerGetStreamInfoEx(void* h, std::uint32_t stream_id, void* info) {
 Player* p = Check(h);
 if (p == nullptr || info == nullptr) return kErrInvalidParams;
 std::lock_guard g(p->lock);
 if (!p->haveSource || stream_id != 0) return kErrOperationFailed;
 std::uint8_t* out = static_cast<std::uint8_t*>(info);
 std::uint64_t size = 0;
 std::memcpy(&size, out, 8);
 std::memset(out, 0, 104);
 std::memcpy(out, &size, 8);
 const std::uint32_t type = 1;
 const std::uint64_t duration = kStreamDurationMs;
 std::memcpy(out + 8, &type, 4);
 VideoDetailsEx details;
 WriteVideoDetails(details);
 std::memcpy(out + 16, &details, sizeof(details));
 std::memcpy(out + 96, &duration, 8);
 return 0;
}
// The data getters are the frame source as well as the end-of-stream report: they hand out whichever frame of
// the blank clip the media clock has reached, and the poll that finds the clip spent is the one that reports the
// stream as finished. Each site logs its own first three calls, so a run shows which of these the title pumps.
Bool APS5_VABI sceAvPlayerGetVideoData(void* h, void* video_info) {
 Player* p = Check(h);
 if (p == nullptr) return 0;
 APS5_HIT("AVP", "GetVideoData");
 return PollVideo(p, video_info, false);
}
Bool APS5_VABI sceAvPlayerGetVideoDataEx(void* h, void* video_info) {
 Player* p = Check(h);
 if (p == nullptr) return 0;
 APS5_HIT("AVP", "GetVideoDataEx");
 return PollVideo(p, video_info, true);
}
Bool APS5_VABI sceAvPlayerGetAudioData(void* h, void* audio_info) {
 (void)audio_info;
 Player* p = Check(h);
 if (p == nullptr) return 0;
 APS5_HIT("AVP", "GetAudioData");
 if (EndStream(p)) Fire(p, kEventStop);
 return 0;
}
}
