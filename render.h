#pragma once
#include "parallel_for.h"
#include "framebuffer.h"

template<typename TColor>
void render(framebuffer<TColor> framebuffer, auto&& tile_func, abort_token& aborter)
{
    const auto domain = generate_parallel_for_domain(framebuffer.width, framebuffer.height);
    parallel_for(domain, tile_func, aborter);
}