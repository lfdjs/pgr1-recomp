#pragma once
namespace pgr::gldirect {
// SDL's video thread publishes physical pixels atomically to the game thread.
void SetDesktopPixels(unsigned width,unsigned height);
void SetOutputPixels(unsigned width,unsigned height);
}
