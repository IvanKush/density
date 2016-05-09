
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "..\paged_queue.h"
#include "testing_utils.h"
#include <iostream>
#include <random>

namespace density
{
    namespace detail
    {
        void paged_queue_test(std::mt19937 & i_random)
        {
            (void)i_random;

            PagedQueue<int> queue(128);
            std::vector<int> ints;
            
            for (int i = 0; i < 1000; i++)
            {
                queue.push(i);
            }

            /*for (int i = 0; i < 1000; i++)
            {
                queue.consume([] (const PagedQueue<int>::runtime_type &, int val) {
                    std::cout << val << std::endl;
                });
            }

            DENSITY_TEST_ASSERT(queue.empty());

            for (int i = 0; i < 1000; i++)
            {
                size_t size = 0;
                auto e = queue.end();
                for (auto it = queue.begin(); it != e; it++)
                {
                    size++;
                }

                auto const queue_dist = std::distance(queue.begin(), queue.end());
                DENSITY_TEST_ASSERT(queue_dist == static_cast<decltype(queue_dist)>(ints.size()));
                DENSITY_TEST_ASSERT(queue.empty() == ints.empty());

                if ( !ints.empty() && std::uniform_int_distribution<int>(0, 6)(i_random) <= 2)
                {
                    queue.pop();
                    ints.erase(ints.begin());
                }
                queue.push(i);
                ints.push_back(i);
            }

            for (auto i : queue)
            {
                std::cout << i << std::endl;
            }*/
        }
    }

    void paged_queue_test()
    {
        std::mt19937 random;
        detail::paged_queue_test(random);
    }
}
