
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "../test_framework/density_test_common.h"
//

#include "test_framework/progress.h"
#include <assert.h>
#include <chrono>
#include <complex>
#include <density/sp_function_queue.h>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

// if assert expands to nothing, some local variable becomes unused
#if defined(_MSC_VER) && defined(NDEBUG)
#pragma warning(push)
#pragma warning(disable : 4189) // local variable is initialized but not referenced
#endif

namespace density_tests
{
    template <
      density::function_type_erasure   ERASURE,
      density::concurrency_cardinality PROD_CARDINALITY,
      density::concurrency_cardinality CONSUMER_CARDINALITY>
    struct SpFunctionQueueSamples
    {
        static void func_queue_put_samples(std::ostream & i_ostream)
        {
            PrintScopeDuration dur(i_ostream, "spin-locking function queue put samples");

            using namespace density;

            {
                //! [sp_function_queue push example 1]
                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                queue.push([] { std::cout << "Hello"; });
                queue.push([] { std::cout << " world"; });
                queue.push([] { std::cout << "!!!"; });
                queue.push([] { std::cout << std::endl; });
                while (queue.try_consume())
                    ;
                //! [sp_function_queue push example 1]
            }
            {
                //! [sp_function_queue push example 3]
#if !defined(_MSC_VER) || !defined(_M_X64) /* the size of a type must always be a multiple of
        the alignment, but in the Microsoft's compiler, on 64-bit targets, pointers to data
        member are 4 bytes big, but are aligned to 8 bytes.

        Test code:
            using T = int Struct::*;
            std::cout << sizeof(T) << std::endl;
            std::cout << alignof(T) << std::endl;
        */

                struct Struct
                {
                    int func_1() { return 1; }
                    int func_2() { return 2; }
                    int var_1 = 3;
                    int var_2 = 4;
                };

                sp_function_queue<int(Struct *)> queue;
                queue.push(&Struct::func_1);
                queue.push(&Struct::func_2);
                queue.push(&Struct::var_1);
                queue.push(&Struct::var_2);

                Struct struct_instance;

                int sum = 0;
                while (auto const return_value = queue.try_consume(&struct_instance))
                    sum += *return_value;
                assert(sum == 10);

#endif
//! [sp_function_queue push example 3]
#if !defined(_MSC_VER) || !defined(_M_X64)
                (void)sum;
#endif
            }
            {
                //! [sp_function_queue push example 2]
                double last_val = 1.;

                auto func = [&last_val] { return last_val /= 2.; };

                sp_function_queue<
                  double(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                for (int i = 0; i < 10; i++)
                    queue.push(func);

                while (auto const return_value = queue.try_consume())
                    std::cout << *return_value << std::endl;
                //! [sp_function_queue push example 2]
            }
            {
                //! [sp_function_queue emplace example 1]
                /* This local struct is unmovable and uncopyable, so emplace is the only
                    option to add it to the queue. Note that operator () returns an int,
                    but we add it to a void() function queue. This is ok, as we are just
                    discarding the return value. */
                struct Func
                {
                    int const m_value;

                    Func(int i_value) : m_value(i_value) {}

                    Func(const Func &) = delete;
                    Func & operator=(const Func &) = delete;

                    int operator()() const
                    {
                        std::cout << m_value << std::endl;
                        return m_value;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                queue.template emplace<Func>(7);

                bool const invoked = queue.try_consume();
                assert(invoked);
                //! [sp_function_queue emplace example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue start_push example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                     queue;
                auto transaction = queue.start_push(Func{});

                // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                transaction.element().m_string_2 =
                  transaction.raw_allocate_copy("\t(I'm so happy)!!");

                transaction.commit();

                bool const invoked = queue.try_consume();
                assert(invoked);
                //! [sp_function_queue start_push example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue start_emplace example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                     queue;
                auto transaction = queue.template start_emplace<Func>();

                // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                transaction.element().m_string_2 =
                  transaction.raw_allocate_copy("\t(I'm so happy)!!");

                transaction.commit();

                bool const invoked = queue.try_consume();
                assert(invoked);
                //! [sp_function_queue start_emplace example 1]
                (void)invoked;
            }
        }

        static void func_queue_try_put_samples(std::ostream & i_ostream)
        {
            PrintScopeDuration dur(i_ostream, "spin-locking function queue put samples");

            using namespace density;

            {
                //! [sp_function_queue try_push example 1]
                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                bool const ok =
                  queue.try_push(progress_lock_free, [] { std::cout << "Hello world!"; });

                while (queue.try_consume())
                    ;
                //! [sp_function_queue try_push example 1]
                (void)ok;
            }
            {
                //! [sp_function_queue try_emplace example 1]
                /* This local struct is unmovable and uncopyable, so emplace is the only
                    option to add it to the queue. Note that operator () returns an int,
                    but we add it to a void() function queue. This is ok, as we are just
                    discarding the return value. */
                struct Func
                {
                    int const m_value;

                    Func(int i_value) : m_value(i_value) {}

                    Func(const Func &) = delete;
                    Func & operator=(const Func &) = delete;

                    int operator()() const
                    {
                        std::cout << m_value << std::endl;
                        return m_value;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                     queue;
                bool invoked = false;
                if (queue.template try_emplace<Func>(progress_lock_free, 7))
                {
                    invoked = queue.try_consume();
                    assert(invoked);
                }
                //! [sp_function_queue try_emplace example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue try_start_push example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                bool invoked = false;
                if (auto transaction = queue.try_start_push(progress_lock_free, Func{}))
                {
                    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                    transaction.element().m_string_2 =
                      transaction.raw_allocate_copy("\t(I'm so happy)!!");

                    transaction.commit();

                    invoked = queue.try_consume();
                    assert(invoked);
                }
                //! [sp_function_queue try_start_push example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue try_start_emplace example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                     queue;
                bool invoked = false;
                if (auto transaction = queue.template try_start_emplace<Func>(progress_lock_free))
                {
                    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                    transaction.element().m_string_2 =
                      transaction.raw_allocate_copy("\t(I'm so happy)!!");

                    transaction.commit();

                    invoked = queue.try_consume();
                    assert(invoked);
                }
                //! [sp_function_queue try_start_emplace example 1]
                (void)invoked;
            }
        }

        static void func_queue_reentrant_put_samples(std::ostream & i_ostream)
        {
            PrintScopeDuration dur(i_ostream, "spin-locking function queue reentrant put samples");

            using namespace density;

            {
                //! [sp_function_queue reentrant_push example 1]
                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                queue.reentrant_push([] { std::cout << "Hello"; });
                queue.reentrant_push([] { std::cout << " world"; });
                queue.reentrant_push([] { std::cout << "!!!"; });
                queue.reentrant_push([] { std::cout << std::endl; });
                while (queue.try_reentrant_consume())
                    ;
                //! [sp_function_queue reentrant_push example 1]
            }
            {
                //! [sp_function_queue reentrant_push example 2]
                double last_val = 1.;

                auto func = [&last_val] { return last_val /= 2.; };

                sp_function_queue<
                  double(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                for (int i = 0; i < 10; i++)
                    queue.reentrant_push(func);

                while (auto const return_value = queue.try_reentrant_consume())
                    std::cout << *return_value << std::endl;
                //! [sp_function_queue reentrant_push example 2]
            }
            {
                //! [sp_function_queue reentrant_emplace example 1]
                /* This local struct is unmovable and uncopyable, so emplace is the only
                    option to add it to the queue. Note that operator () returns an int,
                    but we add it to a void() function queue. This is ok, as we are just
                    discarding the return value. */
                struct Func
                {
                    int const m_value;

                    Func(int i_value) : m_value(i_value) {}

                    Func(const Func &) = delete;
                    Func & operator=(const Func &) = delete;

                    int operator()() const
                    {
                        std::cout << m_value << std::endl;
                        return m_value;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                queue.template reentrant_emplace<Func>(7);

                bool const invoked = queue.try_reentrant_consume();
                assert(invoked);
                //! [sp_function_queue reentrant_emplace example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue start_reentrant_push example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                auto transaction = queue.start_reentrant_push(Func{});

                // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                transaction.element().m_string_2 =
                  transaction.raw_allocate_copy("\t(I'm so happy)!!");

                transaction.commit();

                // now transaction is empty

                bool const invoked = queue.try_reentrant_consume();
                assert(invoked);
                //! [sp_function_queue start_reentrant_push example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue start_reentrant_emplace example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                auto transaction = queue.template start_reentrant_emplace<Func>();

                // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                transaction.element().m_string_2 =
                  transaction.raw_allocate_copy("\t(I'm so happy)!!");

                transaction.commit();

                bool const invoked = queue.try_reentrant_consume();
                assert(invoked);
                //! [sp_function_queue start_reentrant_emplace example 1]
                (void)invoked;
            }
        }

        static void func_queue_try_reentrant_put_samples(std::ostream & i_ostream)
        {
            PrintScopeDuration dur(i_ostream, "spin-locking function queue reentrant put samples");

            using namespace density;

            {
                //! [sp_function_queue try_reentrant_push example 1]
                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                if (queue.try_reentrant_push(
                      progress_lock_free, [] { std::cout << "Hello world"; }))
                {
                    while (queue.try_reentrant_consume())
                        ;
                }
                //! [sp_function_queue try_reentrant_push example 1]
            }
            {
                //! [sp_function_queue try_reentrant_emplace example 1]
                /* This local struct is unmovable and uncopyable, so emplace is the only
                    option to add it to the queue. Note that operator () returns an int,
                    but we add it to a void() function queue. This is ok, as we are just
                    discarding the return value. */
                struct Func
                {
                    int const m_value;

                    Func(int i_value) : m_value(i_value) {}

                    Func(const Func &) = delete;
                    Func & operator=(const Func &) = delete;

                    int operator()() const
                    {
                        std::cout << m_value << std::endl;
                        return m_value;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                bool invoked = false;
                if (queue.template try_reentrant_emplace<Func>(progress_lock_free, 7))
                {
                    invoked = queue.try_reentrant_consume();
                    assert(invoked);
                }
                //! [sp_function_queue try_reentrant_emplace example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue try_start_reentrant_push example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                bool invoked = false;
                if (auto transaction = queue.try_start_reentrant_push(progress_lock_free, Func{}))
                {
                    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                    transaction.element().m_string_2 =
                      transaction.raw_allocate_copy("\t(I'm so happy)!!");

                    transaction.commit();

                    // now transaction is empty

                    invoked = queue.try_reentrant_consume();
                    assert(invoked);
                }
                //! [sp_function_queue try_start_reentrant_push example 1]
                (void)invoked;
            }
            {
                //! [sp_function_queue try_start_reentrant_emplace example 1]
                struct Func
                {
                    const char * m_string_1;
                    const char * m_string_2;

                    void operator()()
                    {
                        std::cout << m_string_1 << std::endl;
                        std::cout << m_string_2 << std::endl;
                    }
                };

                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                bool invoked = false;
                if (
                  auto transaction =
                    queue.template try_start_reentrant_emplace<Func>(progress_lock_free))
                {
                    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
                    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
                    transaction.element().m_string_2 =
                      transaction.raw_allocate_copy("\t(I'm so happy)!!");

                    transaction.commit();

                    invoked = queue.try_reentrant_consume();
                    assert(invoked);
                }
                //! [sp_function_queue try_start_reentrant_emplace example 1]
                (void)invoked;
            }
        }

        static void func_queue_reentrant_consume_samples(std::ostream &)
        {
            using namespace density;

            {
                //! [sp_function_queue try_consume example 1]
                sp_function_queue<
                  int(std::vector<std::string> & vect),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                queue.push([](std::vector<std::string> & vect) {
                    vect.push_back("Hello");
                    return 2;
                });

                queue.push([](std::vector<std::string> & vect) {
                    vect.push_back(" world!");
                    return 3;
                });

                std::vector<std::string> strings;

                int sum = 0;
                while (auto const return_value = queue.try_consume(strings))
                {
                    sum += *return_value;
                }

                assert(sum == 5);

                for (auto const & str : strings)
                    std::cout << str;
                std::cout << std::endl;
                //! [sp_function_queue try_consume example 1]
            }

            {
                //! [sp_function_queue try_consume example 2]
                using Queue = sp_function_queue<
                  int(std::vector<std::string> & vect),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>;
                Queue queue;

                queue.push([](std::vector<std::string> & vect) {
                    vect.push_back("Hello");
                    return 2;
                });

                queue.push([](std::vector<std::string> & vect) {
                    vect.push_back(" world!");
                    return 3;
                });

                std::vector<std::string> strings;

                // providing a cached consume_operation gives better performances
                typename Queue::consume_operation consume;

                int sum = 0;
                while (auto const return_value = queue.try_consume(consume, strings))
                {
                    sum += *return_value;
                }

                assert(sum == 5);

                for (auto const & str : strings)
                    std::cout << str;
                std::cout << std::endl;
                //! [sp_function_queue try_consume example 2]
            }

            {
                //! [sp_function_queue try_reentrant_consume example 1]
                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                auto func1 = [&queue] {
                    std::cout << (queue.empty() ? "The queue is empty" : "The queue is not empty")
                              << std::endl;
                };

                auto func2 = [&queue, func1] { queue.push(func1); };

                queue.push(func1);
                queue.push(func2);

                /* The callable objects we are going to invoke will access the queue, so we
                    must use a reentrant consume. Note: during the invoke of the last function
                    the queue is empty to any observer. */
                while (queue.try_reentrant_consume())
                    ;

                // Output:
                // The queue is not empty
                // The queue is empty
                //! [sp_function_queue try_reentrant_consume example 1]
            }
            {
                //! [sp_function_queue try_reentrant_consume example 2]
                sp_function_queue<
                  void(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;

                auto func1 = [&queue] {
                    std::cout << (queue.empty() ? "The queue is empty" : "The queue is not empty")
                              << std::endl;
                };

                auto func2 = [&queue, func1] { queue.push(func1); };

                queue.push(func1);
                queue.push(func2);

                // providing a cached consume_operation gives much better performances
                typename decltype(queue)::reentrant_consume_operation consume;

                /* The callable objects we are going to invoke will access the queue, so we
                    must use a reentrant consume. Note: during the invoke of the last function
                    the queue is empty to any observer. */
                while (queue.try_reentrant_consume(consume))
                    ;

                // Output:
                // The queue is not empty
                // The queue is empty
                //! [sp_function_queue try_reentrant_consume example 2]
            }
        }

        static void func_queue_reentrant_misc_samples(std::ostream &)
        {
            using namespace density;

            {
                //! [sp_function_queue default construct example 1]
                sp_function_queue<
                  int(float, double),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                assert(queue.empty());
                //! [sp_function_queue default construct example 1]
            }

            {
                //! [sp_function_queue move construct example 1]
                sp_function_queue<
                  int(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                queue.push([] { return 6; });

                auto queue_1(std::move(queue));
                assert(queue.empty());

                auto result = queue_1.try_consume();
                assert(result && *result == 6);
                //! [sp_function_queue move construct example 1]
            }

            {
                //! [sp_function_queue move assign example 1]
                sp_function_queue<
                  int(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue, queue_1;
                queue.push([] { return 6; });

                queue_1 = std::move(queue);

                auto result = queue_1.try_consume();
                assert(result && *result == 6);
                //! [sp_function_queue move assign example 1]
            }

            {
                //! [sp_function_queue swap example 1]
                sp_function_queue<
                  int(),
                  default_allocator,
                  ERASURE,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue, queue_1;
                queue.push([] { return 6; });

                std::swap(queue, queue_1);
                assert(queue.empty());

                auto result = queue_1.try_consume();
                assert(result && *result == 6);
                //! [sp_function_queue swap example 1]
            }

            {
                //! [sp_function_queue clear example 1]
                sp_function_queue<
                  int(),
                  default_allocator,
                  function_standard_erasure,
                  PROD_CARDINALITY,
                  CONSUMER_CARDINALITY>
                  queue;
                queue.push([] { return 6; });
                queue.clear();
                assert(queue.empty());
                //! [sp_function_queue clear example 1]
            }
        }

        static void func_queue_samples(std::ostream & i_ostream)
        {
            func_queue_reentrant_misc_samples(i_ostream);

            func_queue_put_samples(i_ostream);
            func_queue_reentrant_put_samples(i_ostream);
            func_queue_try_put_samples(i_ostream);
            func_queue_try_reentrant_put_samples(i_ostream);

            func_queue_reentrant_consume_samples(i_ostream);
        }
    };


    void sp_func_queue_samples(std::ostream & i_ostream)
    {
        constexpr auto standard_erasure = density::function_standard_erasure;
        constexpr auto manual_clear     = density::function_manual_clear;

        constexpr auto mult   = density::concurrency_multiple;
        constexpr auto single = density::concurrency_single;

        SpFunctionQueueSamples<standard_erasure, mult, mult>::func_queue_samples(i_ostream);
        SpFunctionQueueSamples<manual_clear, mult, mult>::func_queue_samples(i_ostream);

        SpFunctionQueueSamples<standard_erasure, single, mult>::func_queue_samples(i_ostream);
        SpFunctionQueueSamples<manual_clear, single, mult>::func_queue_samples(i_ostream);

        SpFunctionQueueSamples<standard_erasure, mult, single>::func_queue_samples(i_ostream);
        SpFunctionQueueSamples<manual_clear, mult, single>::func_queue_samples(i_ostream);

        SpFunctionQueueSamples<standard_erasure, single, single>::func_queue_samples(i_ostream);
        SpFunctionQueueSamples<manual_clear, single, single>::func_queue_samples(i_ostream);
    }


} // namespace density_tests

#if defined(_MSC_VER) && defined(NDEBUG)
#pragma warning(pop)
#endif
