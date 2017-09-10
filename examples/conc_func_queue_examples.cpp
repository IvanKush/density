
#include <string>
#include <iostream>
#include <iterator>
#include <complex>
#include <string>
#include <chrono>
#include <vector>
#include <assert.h>
#include <density/conc_function_queue.h>
#include "../density_tests/test_framework/progress.h"

// if assert expands to nothing, some local variable becomes unused
#if defined(_MSC_VER) && defined(NDEBUG)
	#pragma warning(push)
	#pragma warning(disable:4189) // local variable is initialized but not referenced
#endif

namespace density_tests
{
    template <density::function_type_erasure ERASURE>
        struct ConcFunctionQueueSamples
    {
        static void func_queue_put_samples(std::ostream & i_ostream)
        {
	        PrintScopeDuration(i_ostream, "function queue put samples");

	        using namespace density;

            {
                //! [conc_function_queue push example 1]
    conc_function_queue<void(), void_allocator, ERASURE> queue;
    queue.push( []{ std::cout << "Hello"; } );
    queue.push( []{ std::cout << " world"; } );
    queue.push( []{ std::cout << "!!!"; } );
    queue.push( []{ std::cout << std::endl; } );
    while( queue.try_consume() );
        //! [conc_function_queue push example 1]
    }
    {
        //! [conc_function_queue push example 2]
    double last_val = 1.;

    auto func = [&last_val] { 
        return last_val /= 2.; 
    };

    conc_function_queue<double(), void_allocator, ERASURE> queue;
    for(int i = 0; i < 10; i++)
        queue.push(func);

    while (auto const return_value = queue.try_consume())
        std::cout << *return_value << std::endl;
                //! [conc_function_queue push example 2]
            }
            {
                //! [conc_function_queue emplace example 1]
    /* This local struct is unmovable and uncopyable, so emplace is the only
        option to add it to the queue. Note that operator () returns an int,
        but we add it to a void() function queue. This is ok, as we are just 
        discarding the return value. */
    struct Func
    {
        int const m_value;

        Func(int i_value) : m_value(i_value) {}

        Func(const Func &) = delete;
        Func & operator = (const Func &) = delete;

        int operator () () const
        { 
            std::cout << m_value << std::endl;
            return m_value;
        }
    };

    conc_function_queue<void(), void_allocator, ERASURE> queue;
    queue.template emplace<Func>(7);

    bool const invoked = queue.try_consume();
    assert(invoked);
                //! [conc_function_queue emplace example 1]
                (void)invoked;
            }
            {
                //! [conc_function_queue start_push example 1]
    struct Func
    {
        const char * m_string_1;
        const char * m_string_2;
        
        void operator () ()
        {
            std::cout << m_string_1 << std::endl;
            std::cout << m_string_2 << std::endl;
        }
    };

    conc_function_queue<void(), void_allocator, ERASURE> queue;
    auto transaction = queue.start_push(Func{});

    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
    transaction.element().m_string_2 = transaction.raw_allocate_copy("\t(I'm so happy)!!");

    transaction.commit();

    bool const invoked = queue.try_consume();
    assert(invoked);
        //! [conc_function_queue start_push example 1]
        (void)invoked;
    }
    {
        //! [conc_function_queue start_emplace example 1]
    struct Func
    {
        const char * m_string_1;
        const char * m_string_2;
        
        void operator () ()
        {
            std::cout << m_string_1 << std::endl;
            std::cout << m_string_2 << std::endl;
        }
    };

    conc_function_queue<void(), void_allocator, ERASURE> queue;
    auto transaction = queue.template start_emplace<Func>();

    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
    transaction.element().m_string_2 = transaction.raw_allocate_copy("\t(I'm so happy)!!");

    transaction.commit();

    bool const invoked = queue.try_consume();
    assert(invoked);
                //! [conc_function_queue start_emplace example 1]
                (void)invoked;
            }
        }

        static void func_queue_reentrant_put_samples(std::ostream & i_ostream)
        {
	        PrintScopeDuration(i_ostream, "function queue reentrant put samples");

	        using namespace density;

            {
                //! [conc_function_queue reentrant_push example 1]
    conc_function_queue<void(), void_allocator, ERASURE> queue;
    queue.reentrant_push( []{ std::cout << "Hello"; } );
    queue.reentrant_push( []{ std::cout << " world"; } );
    queue.reentrant_push( []{ std::cout << "!!!"; } );
    queue.reentrant_push( []{ std::cout << std::endl; } );
    while( queue.try_reentrant_consume() );
                //! [conc_function_queue reentrant_push example 1]
            }
            {
                //! [conc_function_queue reentrant_push example 2]
    double last_val = 1.;

    auto func = [&last_val] { 
        return last_val /= 2.; 
    };

    conc_function_queue<double(), void_allocator, ERASURE> queue;
    for(int i = 0; i < 10; i++)
        queue.reentrant_push(func);

    while (auto const return_value = queue.try_reentrant_consume())
        std::cout << *return_value << std::endl;
                //! [conc_function_queue reentrant_push example 2]
            }
            {
                //! [conc_function_queue reentrant_emplace example 1]
    /* This local struct is unmovable and uncopyable, so emplace is the only
        option to add it to the queue. Note that operator () returns an int,
        but we add it to a void() function queue. This is ok, as we are just 
        discarding the return value. */
    struct Func
    {
        int const m_value;

        Func(int i_value) : m_value(i_value) {}

        Func(const Func &) = delete;
        Func & operator = (const Func &) = delete;

        int operator () () const
        { 
            std::cout << m_value << std::endl;
            return m_value;
        }
    };

    conc_function_queue<void(), void_allocator, ERASURE> queue;
    queue.template reentrant_emplace<Func>(7);

    bool const invoked = queue.try_reentrant_consume();
    assert(invoked);
                //! [conc_function_queue reentrant_emplace example 1]
                (void)invoked;
            }
            {
                //! [conc_function_queue start_reentrant_push example 1]
    struct Func
    {
        const char * m_string_1;
        const char * m_string_2;
        
        void operator () ()
        {
            std::cout << m_string_1 << std::endl;
            std::cout << m_string_2 << std::endl;
        }
    };

    conc_function_queue<void(), void_allocator, ERASURE> queue;

    auto transaction = queue.start_reentrant_push(Func{});
    
    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
    transaction.element().m_string_2 = transaction.raw_allocate_copy("\t(I'm so happy)!!");
    
    transaction.commit();

    // now transaction is empty

    bool const invoked = queue.try_reentrant_consume();
    assert(invoked);
        //! [conc_function_queue start_reentrant_push example 1]
        (void)invoked;
    }
    {
        //! [conc_function_queue start_reentrant_emplace example 1]
    struct Func
    {
        const char * m_string_1;
        const char * m_string_2;
        
        void operator () ()
        {
            std::cout << m_string_1 << std::endl;
            std::cout << m_string_2 << std::endl;
        }
    };

    conc_function_queue<void(), void_allocator, ERASURE> queue;

    auto transaction = queue.template start_reentrant_emplace<Func>();

    // in case of exception here, since the transaction is not committed, it is discarded with no observable effects
    transaction.element().m_string_1 = transaction.raw_allocate_copy("Hello world");
    transaction.element().m_string_2 = transaction.raw_allocate_copy("\t(I'm so happy)!!");

    transaction.commit();

    bool const invoked = queue.try_reentrant_consume();
    assert(invoked);
                //! [conc_function_queue start_reentrant_emplace example 1]
                (void)invoked;
            }
        }

        static void func_queue_reentrant_consume_samples(std::ostream &)
        {
            using namespace density;

            {
                //! [conc_function_queue try_consume example 1]
    conc_function_queue<int (std::vector<std::string> & vect), void_allocator, ERASURE> queue;

    queue.push( [](std::vector<std::string> & vect) { 
        vect.push_back("Hello");
        return 2;
    });

    queue.push( [](std::vector<std::string> & vect) { 
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
                //! [conc_function_queue try_consume example 1]
            }

            {
                //! [conc_function_queue try_reentrant_consume example 1]
    conc_function_queue<void(), void_allocator, ERASURE> queue;

    auto func1 = [&queue] { 
        std::cout << (queue.empty() ? "The queue is empty" : "The queue is not empty") << std::endl;
    };

    auto func2 = [&queue, func1] { 
        queue.push(func1);
    };

    queue.push(func1);
    queue.push(func2);

    /* The callable objects we are going to invoke will access the queue, so we
        must use a reentrant consume. Note: during the invoke of the last function
        the queue is empty to any observer. */
    while (queue.try_reentrant_consume());
    
    // Output:
    // The queue is not empty
    // The queue is empty
                //! [conc_function_queue try_reentrant_consume example 1]
            }
        }

        static void func_queue_reentrant_misc_samples(std::ostream &)
        {
            using namespace density;

            {
                //! [conc_function_queue default construct example 1]
    conc_function_queue<int (float, double), void_allocator, ERASURE> queue;
    assert(queue.empty());
                //! [conc_function_queue default construct example 1]
            }

            {
                //! [conc_function_queue move construct example 1]
    conc_function_queue<int (), void_allocator, ERASURE> queue;
    queue.push([] { return 6; });

    auto queue_1(std::move(queue));
    assert(queue.empty());
    
    auto result = queue_1.try_consume();
    assert(result && *result == 6);
                //! [conc_function_queue move construct example 1]
            }

            {
                //! [conc_function_queue move assign example 1]
    conc_function_queue<int (), void_allocator, ERASURE> queue, queue_1;
    queue.push([] { return 6; });

    queue_1 = std::move(queue);
    
    auto result = queue_1.try_consume();
    assert(result && *result == 6);
                //! [conc_function_queue move assign example 1]
            }


            {
                //! [conc_function_queue swap example 1]
    conc_function_queue<int (), void_allocator, ERASURE> queue, queue_1;
    queue.push([] { return 6; });

    std::swap(queue, queue_1);
    assert(queue.empty());
    
    auto result = queue_1.try_consume();
    assert(result && *result == 6);
                //! [conc_function_queue swap example 1]
            }

            {
                //! [conc_function_queue clear example 1]
    bool erasure = ERASURE;
    if (erasure != function_manual_clear)
    {
        conc_function_queue<int (), void_allocator, ERASURE> queue;
        queue.push([] { return 6; });
        queue.clear();
        assert(queue.empty());
    }
                //! [conc_function_queue clear example 1]
            }
        }

        static void func_queue_samples(std::ostream & i_ostream)
        {
            func_queue_reentrant_misc_samples(i_ostream);
            func_queue_put_samples(i_ostream);
            func_queue_reentrant_put_samples(i_ostream);
            func_queue_reentrant_consume_samples(i_ostream);
        }
    };


    void conc_func_queue_samples(std::ostream & i_ostream)
    {
        ConcFunctionQueueSamples<density::function_standard_erasure>::func_queue_samples(i_ostream);
        ConcFunctionQueueSamples<density::function_manual_clear>::func_queue_samples(i_ostream);
    }


} // namespace density_tests

#if defined(_MSC_VER) && defined(NDEBUG)		
	#pragma warning(pop)
#endif
