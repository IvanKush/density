
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include "../test_framework/density_test_common.h"
#include "../test_framework/test_objects.h"
#include "../test_framework/test_allocators.h"
#include "../test_framework/progress.h"
#include "complex_polymorphism.h"
#include <density/sp_heter_queue.h>
#include <type_traits>
#include <iterator>

namespace density_tests
{
    template < density::concurrency_cardinality PROD_CARDINALITY,
        density::concurrency_cardinality CONSUMER_CARDINALITY>
            struct SpQueueBasicTests
    {
        template < typename COMMON_TYPE = void, typename RUNTIME_TYPE = density::runtime_type<COMMON_TYPE>, typename ALLOCATOR_TYPE = density::void_allocator>
            using SpHeterQueue = density::sp_heter_queue<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE,
                    PROD_CARDINALITY, CONSUMER_CARDINALITY>;

        static void spinlocking_heterogeneous_queue_lifetime_tests()
        {
            using density::void_allocator;
            using density::runtime_type;

            {
                void_allocator allocator;
                SpHeterQueue<> queue(allocator); // copy construct allocator
                queue.push(1);
                queue.push(2);

                auto other_queue(std::move(queue)); // move construct queue - the source becomes empty
                DENSITY_TEST_ASSERT(queue.empty() && !other_queue.empty());

                // test swaps
                swap(queue, other_queue);
                DENSITY_TEST_ASSERT(!queue.empty() && other_queue.empty());
                swap(queue, other_queue);
                DENSITY_TEST_ASSERT(queue.empty() && !other_queue.empty());
                auto cons = other_queue.try_start_consume();
                DENSITY_TEST_ASSERT(cons && cons.complete_type().template is<int>() && cons.template element<int>() == 1);
                cons.commit();
                cons = other_queue.try_start_consume();
                DENSITY_TEST_ASSERT(cons && cons.complete_type().template is<int>() && cons.template element<int>() == 2);
                cons.commit();
                DENSITY_TEST_ASSERT(other_queue.empty());

                // test allocator getters
                move_only_void_allocator movable_alloc(5);
                SpHeterQueue<void, runtime_type<>, move_only_void_allocator> move_only_queue(std::move(movable_alloc));

                auto allocator_copy = other_queue.get_allocator();
                (void)allocator_copy;

                move_only_queue.push(1);
                move_only_queue.push(2);

                move_only_queue.get_allocator_ref().dummy_func();

                auto const & const_move_only_queue(move_only_queue);
                const_move_only_queue.get_allocator_ref().const_dummy_func();
            }
        }

        /** Basic tests SpHeterQueue<void, ...> with a non-polymorphic base */
        template <typename QUEUE>
            static void spinlocking_heterogeneous_queue_basic_void_tests()
        {
            static_assert(std::is_void<typename QUEUE::common_type>::value, "");

            {
                QUEUE queue;
                DENSITY_TEST_ASSERT(queue.empty());
            }

            {
                QUEUE queue;
                queue.clear();

                queue.push(1);
                DENSITY_TEST_ASSERT(!queue.empty());

                queue.clear();
                DENSITY_TEST_ASSERT(queue.empty());
                queue.clear();
            }
        }

        template<typename ELEMENT, typename QUEUE>
            static void dynamic_push_3(QUEUE & i_queue)
        {
            auto const type = QUEUE::runtime_type::template make<ELEMENT>();

            i_queue.dyn_push(type);

            ELEMENT copy_source;
            i_queue.dyn_push_copy(type, &copy_source);

            ELEMENT move_source;
            i_queue.dyn_push_move(type, &move_source);
        }

        /** Test SpHeterQueue with a non-polymorphic base */
        static void spinlocking_heterogeneous_queue_basic_nonpolymorphic_base_tests()
        {
            using namespace density::type_features;
            using density::runtime_type;
            using density::void_allocator;

            using RunTimeType = runtime_type<NonPolymorphicBase, feature_list<
                default_construct, move_construct, copy_construct, destroy, size, alignment>>;
            SpHeterQueue<NonPolymorphicBase, RunTimeType> queue;

            queue.push(NonPolymorphicBase());
            queue.template emplace<SingleDerivedNonPoly>();

            dynamic_push_3<NonPolymorphicBase>(queue);
            dynamic_push_3<SingleDerivedNonPoly>(queue);

            for (;;)
            {
                auto consume = queue.try_start_consume();
                if (!consume)
                    break;

                if (consume.complete_type().template is<NonPolymorphicBase>())
                {
                    consume.template element<NonPolymorphicBase>().check();
                }
                else
                {
                    DENSITY_TEST_ASSERT(consume.complete_type().template is<SingleDerivedNonPoly>());
                    consume.template element<SingleDerivedNonPoly>().check();
                }
                consume.commit();
            }

            DENSITY_TEST_ASSERT(queue.empty());
        }

        /** Test SpHeterQueue with a polymorphic base */
        static void spinlocking_heterogeneous_queue_basic_polymorphic_base_tests()
        {
            using namespace density::type_features;
            using density::runtime_type;
            using density::void_allocator;

            using RunTimeType = runtime_type<PolymorphicBase, feature_list<
                default_construct, move_construct, copy_construct, destroy, size, alignment>>;
            SpHeterQueue<PolymorphicBase, RunTimeType> queue;

            queue.push(PolymorphicBase());
            queue.template emplace<SingleDerived>();
            queue.template emplace<Derived1>();
            queue.template emplace<Derived2>();
            queue.template emplace<MultipleDerived>();

            dynamic_push_3<PolymorphicBase>(queue);
            dynamic_push_3<SingleDerived>(queue);
            dynamic_push_3<Derived1>(queue);
            dynamic_push_3<Derived2>(queue);
            dynamic_push_3<MultipleDerived>(queue);

            polymorphic_consume<PolymorphicBase>(queue.try_start_consume());
            polymorphic_consume<SingleDerived>(queue.try_start_reentrant_consume());
            polymorphic_consume<Derived1>(queue.try_start_consume());
            polymorphic_consume<Derived2>(queue.try_start_reentrant_consume());
            polymorphic_consume<MultipleDerived>(queue.try_start_consume());

            for(int i = 0; i < 3; i++)
                polymorphic_consume<PolymorphicBase>(queue.try_start_reentrant_consume());
            for(int i = 0; i < 3; i++)
                polymorphic_consume<SingleDerived>(queue.try_start_consume());
            for(int i = 0; i < 3; i++)
                polymorphic_consume<Derived1>(queue.try_start_reentrant_consume());
            for(int i = 0; i < 3; i++)
                polymorphic_consume<Derived2>(queue.try_start_consume());
            for(int i = 0; i < 3; i++)
                polymorphic_consume<MultipleDerived>(queue.try_start_reentrant_consume());

            DENSITY_TEST_ASSERT(queue.empty());
        }

        static void tests(std::ostream & /*i_ostream*/)
        {
            using density::runtime_type;

            spinlocking_heterogeneous_queue_lifetime_tests();

            spinlocking_heterogeneous_queue_basic_nonpolymorphic_base_tests();

            spinlocking_heterogeneous_queue_basic_polymorphic_base_tests();

            spinlocking_heterogeneous_queue_basic_void_tests<SpHeterQueue<>>();

            spinlocking_heterogeneous_queue_basic_void_tests<SpHeterQueue<void, runtime_type<>, UnmovableFastTestAllocator<>>>();

            spinlocking_heterogeneous_queue_basic_void_tests<SpHeterQueue<void, TestRuntimeTime<>, DeepTestAllocator<>>>();
        }

    }; // SpQueueBasicTests



    /** Basic tests for sp_heter_queue<...> */
    void spinlocking_heterogeneous_queue_basic_tests(std::ostream & i_ostream)
    {
        PrintScopeDuration dur(i_ostream, "spin-locking heterogeneous queue basic tests");

        constexpr auto mult = density::concurrency_multiple;
        constexpr auto single = density::concurrency_single;

        SpQueueBasicTests<        mult,        mult        >::tests(i_ostream);
        SpQueueBasicTests<        single,        mult        >::tests(i_ostream);
        SpQueueBasicTests<        mult,        single        >::tests(i_ostream);
        SpQueueBasicTests<        single,        single        >::tests(i_ostream);
    }
}
