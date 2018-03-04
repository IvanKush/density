
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "allocator_stress_test.h"
#include "density_test_common.h"
#include "easy_random.h"
#include "threading_extensions.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <density/default_allocator.h>
#include <mutex>
#include <thread>
#include <vector>

namespace density_tests
{
    namespace
    {
        /** Shynchronization object that wraps a counter, similar to std::experimental::latch. 
            Threads can increment the counter, or wait until the counter reaches a value. */
        class wait_counter
        {
          public:
            void increment()
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_counter++;
                m_condition.notify_all();
            }

            void wait_to(uint64_t i_count_to_reach)
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(
                  lock, [this, i_count_to_reach] { return m_counter >= i_count_to_reach; });
            }

          private:
            uint64_t                m_counter = 0;
            std::mutex              m_mutex;
            std::condition_variable m_condition;
        };

        class page_warehouse
        {
          public:
            using clock = std::chrono::steady_clock;

            page_warehouse(size_t i_max_memory, clock::duration i_timeout)
                : m_max_pages(i_max_memory / density::default_allocator::page_size),
                  m_timeout(i_timeout)
            {
            }

            void allocation_loop()
            {
                auto const start_time = clock::now();
                bool       zeroed     = false;
                do
                {
                    zeroed = !zeroed;

                    // allocate a page
                    auto const new_page = zeroed
                                            ? density::default_allocator().try_allocate_page_zeroed(
                                                density::progress_blocking)
                                            : density::default_allocator().try_allocate_page(
                                                density::progress_blocking);
                    if (new_page == nullptr)
                        break;

                    // fill it with its own address
                    auto const words = static_cast<uintptr_t *>(new_page);
                    auto const word_count =
                      density::default_allocator::page_size / sizeof(uintptr_t);
                    for (size_t index = 0; index < word_count; index++)
                    {
                        if (zeroed)
                        {
                            DENSITY_TEST_ASSERT(words[index] == 0);
                        }
                        words[index] = reinterpret_cast<uintptr_t>(new_page);
                    }

                    // push in m_allocated_pages.back.push_back(new_page);
                    try
                    {
                        m_allocated_pages.push_back(new_page);
                    }
                    catch (...)
                    {
                        density::default_allocator().deallocate_page(new_page);
                        throw;
                    }

                } while (clock::now() - start_time < m_timeout &&
                         m_allocated_pages.size() < m_max_pages);
            }

            void deallocation_loop() noexcept
            {
                bool zeroed = false;

                for (auto page : m_allocated_pages)
                {
                    zeroed = !zeroed;

                    auto const words = static_cast<uintptr_t *>(page);
                    auto const word_count =
                      density::default_allocator::page_size / sizeof(uintptr_t);

                    static_assert(
                      density::default_allocator::page_size % sizeof(uintptr_t) == 0,
                      "Othewrise we should zero the remainder too");
                    for (size_t index = 0; index < word_count; index++)
                    {
                        DENSITY_TEST_ASSERT(words[index] == reinterpret_cast<uintptr_t>(page));
                        if (zeroed)
                        {
                            words[index] = 0;
                        }
                    }
                    if (zeroed)
                        density::default_allocator().deallocate_page_zeroed(page);
                    else
                        density::default_allocator().deallocate_page(page);
                }
                m_allocated_pages.clear();
            }

            ~page_warehouse() { deallocation_loop(); }

          private:
            std::vector<void *>   m_allocated_pages;
            size_t const          m_max_pages;
            clock::duration const m_timeout;
        };

    } // namespace

    struct allocator_stress_test::Impl
    {
        std::atomic<bool> m_should_exit{false};

        struct ThreadData
        {
            std::thread m_thread;
        };
        config const            m_config;
        std::vector<ThreadData> m_thread_datas;
        wait_counter            m_started_threads;

        Impl(config i_config) : m_config(i_config)
        {
            uint64_t processor_count =
              (std::min)(get_num_of_processors(), m_config.m_num_processors);
            m_thread_datas.resize(static_cast<size_t>(processor_count));
            for (uint64_t index = 0; index < processor_count; index++)
            {
                auto & thread_data   = m_thread_datas[index];
                thread_data.m_thread = std::thread(&Impl::run_busier, this, index);
            }

            m_started_threads.wait_to(processor_count);
        }

        ~Impl()
        {
            m_should_exit.store(true);
            for (auto & thread : m_thread_datas)
            {
                thread.m_thread.join();
            }
        }

        static std::chrono::microseconds random_duration(
          EasyRandom &              i_rand,
          std::chrono::microseconds i_min,
          std::chrono::microseconds i_max) noexcept
        {
            return std::chrono::microseconds{
              i_rand.get_int<std::chrono::microseconds::rep>(i_min.count(), i_max.count())};
        }

        void run_busier(uint64_t i_cpu_index)
        {
            set_thread_affinity(uint64_t(1) << i_cpu_index);
            set_thread_priority(thread_priority::critical);
            EasyRandom rand;

            page_warehouse warehouse(m_config.m_max_memory_per_cpu, m_config.m_allocation_timeout);

            m_started_threads.increment();

            bool should_allocate = false;
            while (!m_should_exit.load())
            {
                auto const wait_duration =
                  random_duration(rand, m_config.m_min_wait, m_config.m_max_wait);

                std::this_thread::sleep_for(wait_duration);

                should_allocate = !should_allocate;
                if (should_allocate)
                    warehouse.allocation_loop();
                else
                    warehouse.deallocation_loop();
            }
        }
    };

    allocator_stress_test::allocator_stress_test(config i_config) : m_impl(new Impl(i_config)) {}

    allocator_stress_test::~allocator_stress_test() = default;

} // namespace density_tests
