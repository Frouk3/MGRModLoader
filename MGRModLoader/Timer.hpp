#pragma once
#ifndef TIMER_HPP
#define TIMER_HPP

#include <time.h>
#include <Windows.h>

#if TAKE_TIMER_SNAPSHOTS
class CTimer
{
private:
	clock_t m_Start;
	clock_t m_End;
public:
	void start()
	{
		m_Start = clock();
	}

	CTimer()
	{
		start();
	}

	clock_t stop()
	{
		m_End = clock();
		return m_End;
	}

	void reset()
	{
		m_End = 0;
		m_Start = clock();
	}

	clock_t getDifferenceTicks()
	{
		return m_End - m_Start;
	}

	double getDifferenceSeconds()
	{
		return double((double)getDifferenceTicks() / CLOCKS_PER_SEC);
	}
};

#define LOG_TIMER(timer, format) LOG(format, timer.getDifferenceSeconds())

#else
class CTimer
{
public:

	void start() {}
	CTimer() {}
	clock_t stop() { return 0L; }
	void reset() {}
	clock_t getDifferenceTicks() { return 0L; }
	double getDifferenceSeconds() { return 0.0; }
};

#define LOG_TIMER(timer, format)

#endif

#endif