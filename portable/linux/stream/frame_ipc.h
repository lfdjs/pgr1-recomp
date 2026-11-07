#pragma once

// Linux-only IPC transport used by the Xbox reference harness and SDL3 viewer.
// One fixed-sized mmap file (prefer /dev/shm), never per-frame disk image files.
// Pixels are BGRA8, tightly packed, with the top row first.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace pgr::stream {

inline constexpr std::uint32_t kMagic = 0x31475250;  // "PRG1" tag, protocol marker only
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMaxWidth = 1920;
inline constexpr std::uint32_t kMaxHeight = 1080;
inline constexpr std::size_t kPixelCapacity = std::size_t{kMaxWidth} * kMaxHeight * 4;

struct alignas(64) Header {
    std::uint32_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::atomic<std::uint64_t> sequence{0}; // odd means writer active
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t byte_count = 0;
    std::uint32_t reserved = 0;
    std::uint64_t frame = 0;
};

struct Frame {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t index = 0;
    std::vector<std::uint8_t> bgra;
};

class File {
public:
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File();

    // create=true for game process (publisher), false for viewer (reader).
    bool Open(const std::filesystem::path& path, bool create);
    bool IsOpen() const noexcept { return mapping_ != nullptr; }
    void Close() noexcept;
    bool Publish(std::uint32_t width, std::uint32_t height, std::uint64_t frame,
                 const std::uint8_t* bgra, std::size_t row_stride);
    // Returns true only when a NEW, consistent frame arrived.
    bool Receive(Frame& frame);

private:
    static constexpr std::size_t kFileLength = sizeof(Header) + kPixelCapacity;
    int fd_ = -1;
    void* mapping_ = nullptr;
    Header* header_ = nullptr;
    std::uint8_t* pixels_ = nullptr;
    std::uint64_t seen_sequence_ = 0;
    bool publisher_ = false;
};

}  // namespace pgr::stream
