#ifndef HG_CHONPS_TIMER_H
#define HG_CHONPS_TIMER_H

#include <chrono>

namespace Chonps
{
	class Timer
	{
	public:
		void Begin()
		{
			m_Start = std::chrono::high_resolution_clock::now();
		}

		void Reset()
		{
			m_Start = std::chrono::high_resolution_clock::now();
		}

		float Elapsed()
		{
			return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now() - m_Start).count() * 0.001f * 0.001f * 0.001f;
		}

		float ElapsedMilli()
		{
			return Elapsed() * 1000.0f;
		}

	private:
		std::chrono::time_point<std::chrono::high_resolution_clock> m_Start;
	};
}

#endif