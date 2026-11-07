#include "frame_ipc.h"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pgr::stream {

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "The Linux shared-frame protocol requires lock-free 64-bit atomics");

File::~File() { Close(); }

void File::Close() noexcept {
    if (mapping_) munmap(mapping_, kFileLength);
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    mapping_ = nullptr;
    header_ = nullptr;
    pixels_ = nullptr;
    seen_sequence_ = 0;
    publisher_ = false;
}

bool File::Open(const std::filesystem::path& path, bool create) {
    Close();
    if (path.empty()) return false;
    fd_ = open(path.c_str(), create ? O_RDWR | O_CREAT : O_RDONLY, 0600);
    if (fd_ < 0) return false;
    struct stat st{};
    if (fstat(fd_, &st) != 0 || (create && ftruncate(fd_, kFileLength) != 0) ||
        (!create && st.st_size < static_cast<off_t>(kFileLength))) {
        Close(); return false;
    }
    // MAP_SHARED: publisher writes; viewer only reads. Seqlock uses atomic 64-bit loads.
    mapping_ = mmap(nullptr, kFileLength, create ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd_, 0);
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        Close(); return false;
    }
    header_ = reinterpret_cast<Header*>(mapping_);
    pixels_ = reinterpret_cast<std::uint8_t*>(mapping_) + sizeof(Header);
    publisher_ = create;
    if (create) {
        new (header_) Header{};
    } else if (header_->magic != kMagic || header_->version != kVersion) {
        Close(); return false;
    }
    return true;
}

bool File::Publish(std::uint32_t width, std::uint32_t height, std::uint64_t frame,
                   const std::uint8_t* bgra, std::size_t row_stride) {
    if (!publisher_ || !header_ || !bgra || !width || !height || width > kMaxWidth || height > kMaxHeight ||
        row_stride < std::size_t(width) * 4) return false;
    const std::uint32_t bytes = width * height * 4;
    // A single publisher; no concurrent Publish calls.
    const auto old = header_->sequence.load(std::memory_order_relaxed);
    header_->sequence.store(old + 1, std::memory_order_release);
    // A reader using a seqlock must not accept anything while sequence is odd.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    header_->width = width;
    header_->height = height;
    header_->byte_count = bytes;
    header_->frame = frame;
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(pixels_ + std::size_t(y) * width * 4, bgra + std::size_t(y) * row_stride,
                    std::size_t(width) * 4);
    }
    std::atomic_thread_fence(std::memory_order_release);
    header_->sequence.store(old + 2, std::memory_order_release);
    return true;
}

bool File::Receive(Frame& frame) {
    if (publisher_ || !header_) return false;
    const auto first = header_->sequence.load(std::memory_order_acquire);
    if (!first || (first & 1) || first == seen_sequence_) return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    const std::uint32_t w = header_->width, h = header_->height, bytes = header_->byte_count;
    const std::uint64_t n = header_->frame;
    if (!w || !h || w > kMaxWidth || h > kMaxHeight || bytes != std::uint64_t(w) * h * 4) return false;
    frame.bgra.resize(bytes);
    std::memcpy(frame.bgra.data(), pixels_, bytes);
    std::atomic_thread_fence(std::memory_order_acquire);
    const auto last = header_->sequence.load(std::memory_order_acquire);
    if (first != last || (last & 1)) return false;
    seen_sequence_ = last;
    frame.width = w;
    frame.height = h;
    frame.index = n;
    return true;
}

}  // namespace pgr::stream
