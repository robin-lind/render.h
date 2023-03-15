#pragma once

#include "lucmath.h"

template<typename T>
struct animated
{
	T t0, t1;
	animated() = default;
	animated(T _t0, T _t1) : t0(_t0), t1(_t1) { }
	T lerp(float t)
	{
		return luc::Lerp<T>(t, t0, t1);
	}
};