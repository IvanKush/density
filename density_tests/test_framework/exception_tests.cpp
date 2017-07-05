
#include "exception_tests.h"

namespace density_tests
{
	namespace
	{
		struct StaticData
		{
			int64_t m_current_counter;
			int64_t m_except_at;
			StaticData() : m_current_counter(0), m_except_at(-1) {}
		};

		thread_local StaticData  * st_static_data;
	}

	void exception_checkpoint()
	{
		auto const static_data = st_static_data;
		if (static_data != nullptr)
		{
			if (static_data->m_current_counter == static_data->m_except_at)
			{
				throw TestException();
			}
			static_data->m_current_counter++;
		}
	}

	int64_t run_exception_test(std::function<void()> i_test)
	{
		DENSITY_TEST_ASSERT(st_static_data == nullptr); // "run_exception_test does no support recursion"

		StaticData static_data;
		st_static_data = &static_data;
		int64_t curr_iteration = 0;
		try
		{
			bool exception_occurred;
			do {
				exception_occurred = false;

				try
				{
					static_data.m_current_counter = 0;
					static_data.m_except_at = curr_iteration;
					i_test();
				}
				catch (TestException)
				{
					exception_occurred = true;
				}
				curr_iteration++;

			} while (exception_occurred);
		}
		catch (...)
		{
			st_static_data = nullptr; // unknown exception, reset st_static_data and rethrow
			throw;
		}
		st_static_data = nullptr;

		return curr_iteration - 1;
	}

} // namespace density_tests