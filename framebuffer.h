#pragma once
#include <vector>

template<typename TColor>
struct framebuffer
{
	std::vector<TColor> pixels;
	framebuffer() = default;
};