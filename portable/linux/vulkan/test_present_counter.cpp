#include "present_counter.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
using pgr::vulkan::PresentCounter;
int main() {
    PresentCounter counter;
    assert(counter.Total()==0);
    for(unsigned i=0;i<=60;i++)counter.Presented(10.+i/60.);
    assert(counter.Total()==61);
    assert(std::abs(counter.Fps()-60.)<1e-8);
    assert(std::abs(counter.AverageMs()-1000./60.)<1e-8);
    assert(std::strstr(counter.Text(),"60.0 FPS"));
    const auto before=counter.Total();
    counter.Presented(11.); counter.Presented(2.);
    counter.Presented(std::numeric_limits<double>::quiet_NaN());
    assert(counter.Total()==before);
    counter.Presented(12.);
    assert(counter.Fps()<2.);
    assert(std::abs(counter.FrameMs()-1000.)<1e-8);
}
