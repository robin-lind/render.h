#pragma once
#include "lucmath.h"
#include <vector>
#include <thread>
#include <functional>
#include <numeric>
#include <tuple>
#include <mutex>
#include <queue>
#include <optional>

template<typename TSize>
struct work_range
{
	TSize minx, maxx, miny, maxy;
	work_range() = default;
	work_range(TSize min_x, TSize max_x, TSize min_y, TSize max_y) : minx(min_x), maxx(max_x), miny(min_y), maxy(max_y) {}
};

template<typename TSize>
struct work_domain
{
	work_range<TSize> range;
	std::vector<work_range<TSize>> ranges;
	work_domain() = default;
	work_domain(TSize min_x, TSize max_x, TSize min_y, TSize max_y) : range(min_x, max_x, min_y, max_y) {}
};

template<typename TSize>
struct work_block
{
	work_range<TSize> tile;
	work_range<TSize> domain;
	work_block() = default;
	work_block(work_range<TSize> _tile, work_range<TSize> _domain) : tile(_tile), domain(_domain) {}
};

template<typename TSize>
std::pair<work_range<TSize>, work_range<TSize>> split_range(const work_range<TSize>& range)
{
	const auto w = range.maxx - range.minx;
	const auto h = range.maxy - range.miny;
	bool vertical = false;
	auto min = range.miny, max = range.maxy;
	if (w > h)
	{
		vertical = true;
		min = range.minx;
		max = range.maxx;
	}
	auto mid = std::midpoint(min, max);
	if (vertical)
	{
		work_range<TSize> a(range.minx, mid, range.miny, range.maxy);
		work_range<TSize> b(mid, max, range.miny, range.maxy);
		return std::make_pair(a, b);
	}
	else
	{
		work_range<TSize> a(range.minx, range.maxx, range.miny, mid);
		work_range<TSize> b(range.minx, range.maxx, mid, range.maxy);
		return std::make_pair(a, b);
	}
}

template<typename TSize>
work_domain<TSize> generate_parallel_for_domain(TSize min_x, TSize max_x, TSize min_y, TSize max_y)
{
	work_domain<TSize> domain(min_x, max_x, min_y, max_y);
	std::queue<work_range<TSize>> queue;
	queue.push(domain.range);
	while (!queue.empty())
	{
		const auto range = queue.front();
		queue.pop();
		const TSize max_size = 32;
		const auto w = range.maxx - range.minx;
		const auto h = range.maxy - range.miny;
		if (std::max(w, h) > max_size)
		{
			auto split = split_range(range);
			queue.push(split.first);
			queue.push(split.second);
		}
		else
		{
			domain.ranges.push_back(range);
		}
	}
	return domain;
}

template<typename TSize>
work_domain<TSize> generate_parallel_for_domain(TSize width, TSize height)
{
	return generate_parallel_for_domain(0, width, 0, height);
}

template<typename TSize>
std::queue<work_range<TSize>> range_queue_from_domain(const std::vector<work_range<TSize>>& domain_ranges)
{
	std::queue<work_range<TSize>> range_queue;
	for (const auto& range : domain_ranges)
	{
		range_queue.push(range);
	}
	return range_queue;
}

template<typename TSize, typename TFloat=float>
void iterate_over_tile(const work_block<TSize>& block, auto&& item_func)
{
	for (auto y = block.tile.miny; y < block.tile.maxy; y++)
	{
		for (auto x = block.tile.minx; x < block.tile.maxx; x++)
		{
			const auto minx = luc::Map<TFloat>(x, block.domain.minx, block.domain.maxx, 0, 1) - TFloat(.5);
			const auto miny = luc::Map<TFloat>(y, block.domain.miny, block.domain.maxy, 0, 1) - TFloat(.5);
			const auto maxx = luc::Map<TFloat>(x + TFloat(1.), block.domain.minx, block.domain.maxx, 0, 1) - TFloat(.5);
			const auto maxy = luc::Map<TFloat>(y + TFloat(1.), block.domain.miny, block.domain.maxy, 0, 1) - TFloat(.5);
			auto transform = [minx, miny, maxx, maxy](TFloat u, TFloat v)
			{
				const auto su = luc::Map<TFloat>(u, -.5f, .5f, minx, maxx);
				const auto sv = luc::Map<TFloat>(v, -.5f, .5f, miny, maxy);
				return luc::VectorTN<TFloat, 2>(su, sv);
			};
			item_func(x, y, transform);
		}
	}
}

struct abort_token
{
	bool aborted = false;
	abort_token() = default;
	void abort() { aborted = true; }
};

template<typename TSize = int, bool parallel = true>
void parallel_for(const work_domain<TSize>& domain, auto&& tile_func, abort_token& aborter)
{
	auto range_queue = range_queue_from_domain(domain.ranges);
	std::mutex work_stealing_mutex;
	const auto hardware_concurrency = std::thread::hardware_concurrency();
	//const auto number_of_threads = hardware_concurrency;
	const auto number_of_threads = hardware_concurrency * 4 / 3;
	//const auto number_of_threads = hardware_concurrency * 32 / 22;
	//const auto number_of_threads = hardware_concurrency * 3 / 2;
	const auto thread_count = parallel ? number_of_threads : 1;
	auto worker = [&]()
	{
		work_block<TSize> block(domain.range, domain.range);
		while (!aborter.aborted)
		{
			{
				std::scoped_lock stealing_work(work_stealing_mutex);
				if (range_queue.size() == 0)
					break;
				block.tile = range_queue.front();
				range_queue.pop();
				const auto w = block.tile.maxx - block.tile.minx;
				const auto h = block.tile.maxy - block.tile.miny;
				if (range_queue.size() < thread_count && std::min(w, h) > 4)
				{
					const auto& split = split_range(block.tile);
					range_queue.push(split.second);
					block.tile = split.first;
				}
			}
			tile_func(block);
		}
	};
	std::vector<std::thread> threads;
	for (size_t i = 0; i < thread_count; i++)
	{
		threads.emplace_back(worker);
	}
	for (auto& thread : threads)
	{
		thread.join();
	}
}
