
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "../density/dense_function_queue.h"
#include "../density/paged_function_queue.h"
#include "../testity/test_tree.h"
#include <functional>
#include <deque>
#include <queue>
#include <vector>

namespace density
{
    namespace tests
    {
		using namespace testity;

		void register_function_queue_benchmarks(TestTree & i_test_node)
		{
			PerformanceTestGroup group("push & consume");

			// paged_function_queue
			group.add_test( __FILE__, __LINE__, [](size_t i_cardinality) {
				paged_function_queue< void() > queue;
				for (size_t index = 0; index < i_cardinality; index++)
					queue.push([]() { volatile int dummy = 1; (void)dummy; });
				for (size_t index = 0; index < i_cardinality; index++)
					queue.consume_front();
			}, __LINE__ );

			// dense_function_queue
			group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
				dense_function_queue< void() > queue;
				for (size_t index = 0; index < i_cardinality; index++)
					queue.push([]() { volatile int dummy = 1; (void)dummy; });
				for (size_t index = 0; index < i_cardinality; index++)
					queue.consume_front();
			}, __LINE__);

			// std::queue< std::function >
			group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
				std::queue< std::function<void()> > queue;
				for (size_t index = 0; index < i_cardinality; index++)
					queue.push([]() { volatile int dummy = 1; (void)dummy; });
				for (size_t index = 0; index < i_cardinality; index++)
					queue.front()(), queue.pop();
			}, __LINE__);

			// std::vector< std::function >
			group.add_test(__FILE__, __LINE__, [](size_t i_cardinality) {
				std::vector< std::function<void()> > queue;
				for (size_t index = 0; index < i_cardinality; index++)
					queue.push_back([]() { volatile int dummy = 1; (void)dummy; });
				for (size_t index = 0; index < i_cardinality; index++)
					queue[index]();
			}, __LINE__);

			i_test_node["/density/function_queue_test"].add_performance_test(group);
		}

    } // namespace benchmarks

} // namespace density

