#pragma once
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

template<typename TFloat>
struct uv_transform
{
	TFloat minx, maxx, miny, maxy;
	uv_transform() = default;

	template<typename TSize>
	uv_transform(TSize x, TSize y, const work_range<TSize>& domain)
	{
		minx = luc::Map<TFloat>(x, domain.minx, domain.maxx, 0, 1) - TFloat(.5);
		miny = luc::Map<TFloat>(y, domain.miny, domain.maxy, 0, 1) - TFloat(.5);
		maxx = luc::Map<TFloat>(x + TFloat(1.), domain.minx, domain.maxx, 0, 1) - TFloat(.5);
		maxy = luc::Map<TFloat>(y + TFloat(1.), domain.miny, domain.maxy, 0, 1) - TFloat(.5);
	}

	luc::VectorTN<TFloat, 2> transform(TFloat u, TFloat v) const
	{
		const auto su = luc::Map<TFloat>(u, -.5f, .5f, minx, maxx);
		const auto sv = luc::Map<TFloat>(v, -.5f, .5f, miny, maxy);
		return luc::VectorTN<TFloat, 2>(su, sv);
	}
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
	std::function<void(work_range<TSize>)> subdivide = [&](const work_range<TSize>& range)
	{
		const TSize max_size = 64;
		const auto w = range.maxx - range.minx;
		const auto h = range.maxy - range.miny;
		if (std::max(w, h) > max_size)
		{
			auto split = split_range(range);
			subdivide(split.first);
			subdivide(split.second);
		}
		else
		{
			domain.ranges.push_back(range);
		}
	};
	subdivide(domain.range);
	return domain;
}

template<typename TSize>
work_domain<TSize> generate_parallel_for_domain(TSize width, TSize height)
{
	return generate_parallel_for_domain(0, width, 0, height);
}

template<typename TSize>
std::queue<work_range<TSize>> range_queue_from_domain(std::vector<work_range<TSize>>& domain_ranges)
{
	std::queue<work_range<TSize>> range_queue;
	for (const auto& range : domain_ranges)
	{
		range_queue.push(range);
	}
	return range_queue;
}

//template<typename TSize>
//void iterate_over_tile(const work_block<TSize>& block, std::function<void(TSize, TSize)> func)
//{
//	for (auto y = block.tile.miny; y < block.tile.maxy; y++)
//	{
//		for (auto x = block.tile.minx; x < block.tile.maxx; x++)
//		{
//			func(x, y);
//		}
//	}
//}
//
//template<typename TSize, typename TColor>
//void iterate_over_tile(const work_block<TSize>& block, std::function<TColor(TSize, TSize)> func)
//{
//	for (auto y = block.tile.miny; y < block.tile.maxy; y++)
//	{
//		for (auto x = block.tile.minx; x < block.tile.maxx; x++)
//		{
//			const auto color = func(x, y);
//		}
//	}
//}
//
//template<typename TSize, typename TFloat>
//void iterate_over_tile(const work_block<TSize>& block, std::function<void(TSize, TSize, uv_transform<TFloat>&)> func)
//{
//	for (auto y = block.tile.miny; y < block.tile.maxy; y++)
//	{
//		for (auto x = block.tile.minx; x < block.tile.maxx; x++)
//		{
//			uv_transform uvt(x, y, block.domain);
//			func(x, y, uvt);
//		}
//	}
//}

template<typename TSize, typename TFloat=float>
void iterate_over_tile(const work_block<TSize>& block, auto&& func, auto&& store)
{
	for (auto y = block.tile.miny; y < block.tile.maxy; y++)
	{
		for (auto x = block.tile.minx; x < block.tile.maxx; x++)
		{
			uv_transform<TFloat> uvt(x, y, block.domain);
			const auto color = func(x, y, uvt);
			store(x, y, color);
		}
	}
}

template<typename TSize = int>
void parallel_for(work_domain<TSize>& domain, auto&& func, auto&& store)
{
	auto range_queue = range_queue_from_domain(domain.ranges);
	std::mutex work_stealing_mutex;
	const auto number_of_threads = std::thread::hardware_concurrency();
	auto take_range = [&]()
	{
		std::scoped_lock stealing_work(work_stealing_mutex);
		if (range_queue.size() == 0)
			return std::optional<work_range<TSize>>();
		const auto range = range_queue.front();
		range_queue.pop();
		const auto w = range.maxx - range.minx;
		const auto h = range.maxy - range.miny;
		if (range_queue.size() < number_of_threads && std::min(w,h) > 4)
		{
			const auto& split = split_range(range);
			range_queue.push(split.second);
			return std::make_optional(split.first);
		}
		return std::make_optional(range);
	};
	auto worker = [&]()
	{
		while (true)
		{
			auto range = take_range();
			if (range.has_value())
			{
				work_block<TSize> block(*range, domain.range);
				iterate_over_tile(block, func(block), store);
			}
			else
			{
				break;
			}
		}
	};
	std::vector<std::thread> threads;
	for (size_t i = 0; i < number_of_threads; i++)
	{
		threads.emplace_back(worker);
	}
	for (auto& thread : threads)
	{
		thread.join();
	}
}

template<typename TSize = int>
void parallel_for(TSize width, TSize height, std::function<void(const work_block<TSize>&)> func)
{
	auto domain = generate_parallel_for_domain(width, height);
	parallel_for<TSize>(domain, func);
}

//template<typename TSize = int>
//void parallel_for(work_domain<TSize>& domain, std::function<void(const work_item<TSize>&)> func)
//{
//	auto tile_func = [&](const work_block<TSize>& block)
//	{
//		iterate_over_tile(block, func);
//	};
//	parallel_for<TSize>(domain, tile_func);
//}
//
//template<typename TSize = int>
//void parallel_for(TSize width, TSize height, std::function<void(const work_item<TSize>&)> func)
//{
//	auto domain = generate_parallel_for_domain(width, height);
//	parallel_for<TSize>(domain, func);
//}
