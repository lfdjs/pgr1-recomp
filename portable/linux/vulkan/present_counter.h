#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace pgr::vulkan {
// Feed only successful swapchain presentations, using a monotonic clock.
// Offscreen submissions, simulation ticks and failed acquisitions never enter.
class PresentCounter {
public:
    void Presented(double seconds) {
        if (!std::isfinite(seconds) || (started_ && seconds <= last_)) return;
        ++total_;
        if (!started_) {
            started_=true; last_=window_start_=seconds;
            return;
        }
        frame_ms_=(seconds-last_)*1000.;
        last_=seconds;
        ++intervals_;
        const double elapsed=seconds-window_start_;
        if (elapsed>=.5) {
            fps_=intervals_/elapsed;
            average_ms_=1000.*elapsed/intervals_;
            std::snprintf(text_.data(),text_.size(),"%.1f FPS  %.1f MS",fps_,average_ms_);
            intervals_=0; window_start_=seconds;
        }
    }
    const char* Text() const { return text_.data(); }
    double Fps() const { return fps_; }
    double FrameMs() const { return frame_ms_; }
    double AverageMs() const { return average_ms_; }
    std::uint64_t Total() const { return total_; }
private:
    bool started_=false;
    double last_=0,window_start_=0,frame_ms_=0,fps_=0,average_ms_=0;
    std::uint64_t total_=0,intervals_=0;
    std::array<char,64> text_{'0','.', '0',' ','F','P','S',' ',' ','0','.', '0',' ','M','S',0};
};
}
