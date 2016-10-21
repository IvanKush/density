
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include "density_common.h"
#include <mutex>

#define DENSITY_TEST_RANDOM_WAIT()

#define DENSITY_STATS(expr)

namespace density
{

    /* Defines the kind of algorithm used for a concurrent data structure.
        Note: by lock-free is intended that the implementation does not use mutexes for synchronization, and it is based on a lockfree
        algorithm. Anyway sometimes a lockfree method may need to allocate memory, which may be a locking operation.In density pages
        are allocated from a lockfree page freelist. Anyway, when the freelist is out of memory, a new memory region is requested to
        the operating system, which almost surely will lock a mutex of some kind.
        Refer to the documentation of the single methods to check the guarantees in case of concurrent access. */
    enum class SynchronizationKind
    {
        MutexBased, /**< The implementation is based on a mutex, so it is locking. This allows a thread to go in an efficient waiting state
                        (for an example a thread may wait for a command to be pushed on a queue). */
        LocklessMultiple, /**< The implementation is based on a lock-free algorithm. It is safe for multiple threads to concurrently do the
                            same operation (for example multiple threads can push an element to a queue). */
        LocklessSingle, /**< The implementation is based on a lock-free algorithm. If multiple threads do the same operation concurrently,
                            with no external synchronization, the program is incurring in a data race, therefore the behavior is undefined. */
    };

    namespace detail
    {
        constexpr size_t concurrent_alignment = 64;

        template <typename INTERNAL_WORD, typename RUNTIME_TYPE, INTERNAL_WORD PAGE_SIZE,
            SynchronizationKind PUSH_SYNC, SynchronizationKind CONSUME_SYNC>
                class ou_conc_queue_header;
    }
}

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable:4324) // structure was padded due to alignment specifier
#endif
#define DENSITY_INCLUDING_CONC_QUEUE_DETAIL
    #include "detail\ou_conc_queue_header_lflf.h"
#undef DENSITY_INCLUDING_CONC_QUEUE_DETAIL
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace density
{
    namespace experimental
    {

        template < typename ELEMENT = void, typename PAGE_ALLOCATOR = void_allocator, typename RUNTIME_TYPE = runtime_type<ELEMENT>,
            SynchronizationKind PUSH_SYNC = SynchronizationKind::LocklessMultiple,
            SynchronizationKind CONSUME_SYNC = SynchronizationKind::LocklessMultiple >
                class concurrent_heterogeneous_queue;

        /**
            The queues always keep at least an allocated page. Therefore the constructor allocates a page. The reason is to allow
                producer to assume that the page in which the push is tried (the last one) doesn't get deallocated while the push
                is in progress.
                The consume algorithm uses hazard pointers to safely delete the pages. Pages are deleted immediately by consumers
                when no longer needed. Using the default (and recommended) allocator, deleted pages are added to a thread-local
                free-list. When the number of pages in this free-list exceeds a fixed number, a page is added in a global lock-free
                free-list. See void_allocator for details.
                See "Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects" of Maged M. Michael for details. */
        template < typename ELEMENT, typename PAGE_ALLOCATOR, typename RUNTIME_TYPE >
            class concurrent_heterogeneous_queue< ELEMENT, PAGE_ALLOCATOR, RUNTIME_TYPE,
                SynchronizationKind::LocklessMultiple, SynchronizationKind::LocklessMultiple> : private PAGE_ALLOCATOR
        {
            using queue_header = detail::ou_conc_queue_header<uint64_t, RUNTIME_TYPE, 4096, SynchronizationKind::LocklessMultiple, SynchronizationKind::LocklessMultiple>;

        public:

            static_assert(std::is_same<ELEMENT, void>::value, "Currently only fully-heterogeneous elements are supported");
                /* Reason: casting from a derived class is often a trivial operation (the value of the pointer does not change),
                    but sometimes it may require offsetting the pointer, or it may also require a memory indirection. Most containers
                    in this library store the result of this cast implicitly in the overhead data of the elements, but currently this container doesn't. */

            using allocator_type = PAGE_ALLOCATOR;
            using runtime_type = RUNTIME_TYPE;
            using value_type = ELEMENT;
            using reference = typename std::add_lvalue_reference< ELEMENT >::type;
            using const_reference = typename std::add_lvalue_reference< const ELEMENT>::type;
            using size_type = size_t;

            concurrent_heterogeneous_queue()
            {
                auto first_queue = new(get_allocator_ref().allocate_page()) queue_header();
                m_first.store(first_queue);
                m_last.store(reinterpret_cast<uintptr_t>(first_queue));
                m_can_delete_page.clear();
                m_first_consumer = nullptr;
            }

            template <typename ELEMENT_COMPLETE_TYPE>
                void push(ELEMENT_COMPLETE_TYPE && i_source)
            {
                static_assert(std::is_convertible< typename std::decay<ELEMENT_COMPLETE_TYPE>::type*, ELEMENT*>::value,
                    "ELEMENT_COMPLETE_TYPE must be covariant to (i.e. must derive from) ELEMENT, or ELEMENT must be void");
                push_impl(std::forward<ELEMENT_COMPLETE_TYPE>(i_source),
                    typename std::is_rvalue_reference<ELEMENT_COMPLETE_TYPE&&>::type());
            }

            template <typename CONSTRUCTOR>
                void push(const RUNTIME_TYPE & i_source_type, CONSTRUCTOR && i_constructor)
            {
                push(i_source_type, std::forward<CONSTRUCTOR>(i_constructor), i_source_type.size());
            }

            template <typename CONSTRUCTOR>
                void push(const RUNTIME_TYPE & i_source_type, CONSTRUCTOR && i_constructor, size_t i_size)
            {
                DENSITY_ASSERT(i_source_type.size() == i_size); // inconsistent element size?

                for (;;)
                {
                    /* Take the last page, and try to push in it. We can assume that the page will dot be deleted in the meanwhile,
                        because consumer threads can delete a page only if it is not the last one. */
                    auto last = reinterpret_cast<queue_header*>(m_last.load() & ~static_cast<uintptr_t>(1));
                    if (last->push(i_source_type, std::forward<CONSTRUCTOR>(i_constructor), i_size))
                    {
                        /* Here the push is successful, so we exit the loop */
                        break;
                    }

                    /* Now we try to get exclusive access on m_last. If we fail to do that, just continue the loop. */
                    DENSITY_TEST_RANDOM_WAIT();
                    auto prev_last = m_last.fetch_or(1);
                    if ((prev_last & 1) == 0)
                    {
                        /* Ok, we have set the exclusive flag from 0 to 1. First allocate the page. */
                        DENSITY_TEST_RANDOM_WAIT();
                        auto const queue = new(get_allocator_ref().allocate_page()) queue_header();

                        /** Now we can link the prev-last with the new page. This is a non-atomic write. */
                        DENSITY_TEST_RANDOM_WAIT();
                        DENSITY_ASSERT_INTERNAL(reinterpret_cast<queue_header*>(prev_last)->m_next == nullptr);
                        reinterpret_cast<queue_header*>(prev_last)->m_next = queue;

                        /** Finally we update m_last, and continue the loop */
                        DENSITY_TEST_RANDOM_WAIT();
                        m_last.store(reinterpret_cast<uintptr_t>(queue));
                        DENSITY_STATS(++g_stats.allocated_pages);
                    }
                }
            }

            template <typename OPERATION>
                bool manual_consume(OPERATION && i_operation)
            {
                for (;;)
                {
                    auto res = m_head->consume(std::move(i_operation));
                    if (res == ConsumeResult::Success)
                    {
                        return true;
                    }
                }
            }

            class consumer
            {
            public:

                consumer() = default;

                ~consumer()
                {
                    if (m_target_queue != nullptr)
                    {
                        detach_from_queue();
                    }
                }

                consumer(const consumer &) = delete;
                consumer & operator = (const consumer &) = delete;

                consumer(consumer &&) = default;
                consumer & operator = (consumer &&) = default;

            private:

                consumer(concurrent_heterogeneous_queue & i_target_queue)
                {
                    attach_to_queue(i_target_queue);
                }

                void attach_to_queue(concurrent_heterogeneous_queue & i_target_queue)
                {
                    {
                        std::lock_guard lock(i_target_queue.m_hazard_pointers_mutex); // note: the lock may throw
                        m_next = i_target_queue.m_first_consumer;
                        i_target_queue.m_first_consumer = this;
                    }
                    m_target_queue = &i_target_queue;
                }

                void detach_from_queue()
                {
                    DENSITY_ASSERT_INTERNAL(m_target_queue != nullptr);

                    /* linear search for this in the linked list of consumers associated to this queue. */
                    consumer * prev = nullptr;
                    for (consumer curr = m_target_queue->m_first_consumer; curr != nullptr; curr = curr->m_next)
                    {
                        if (curr == this)
                            break;
                        prev = curr;
                    }

                    if (prev != nullptr)
                    {
                        // not the first consumer
                        DENSITY_ASSERT_INTERNAL(target != s_first_hazard_pointer);
                        DENSITY_ASSERT_INTERNAL(prev->m_next == target);
                        prev->m_next = target->m_next;
                    }
                    else
                    {
                        // it's the first consumer
                        DENSITY_ASSERT_INTERNAL(target == s_first_hazard_pointer);
                        s_first_hazard_pointer = target->m_next;
                    }

                    target->m_next = nullptr;
                }

                friend class concurrent_heterogeneous_queue;

            private:
                std::atomic<queue_header*> m_hazard_page = nullptr; /** pointer to the page currently used by this
                                                                   consumer. This pointer prevents a page to be destroyed by another consumer. */
                consumer * m_next = nullptr;
                concurrent_heterogeneous_queue * m_target_queue = nullptr;
            };

            consumer make_consumer()
            {
                return consumer(*this);
            }

            /** Returns a copy of the allocator instance owned by the queue.
                \n\b Throws: anything that the copy-constructor of the allocator throws
                \n\b Complexity: constant */
            allocator_type get_allocator() const
            {
                return *static_cast<const allocator_type*>(this);
            }

            /** Returns a reference to the allocator instance owned by the queue.
                \n\b Throws: nothing
                \n\b Complexity: constant */
            allocator_type & get_allocator_ref() noexcept
            {
                return *static_cast<allocator_type*>(this);
            }

            /** Returns a const reference to the allocator instance owned by the queue.
                \n\b Throws: nothing
                \n\b Complexity: constant */
            const allocator_type & get_allocator_ref() const noexcept
            {
                return *static_cast<const allocator_type*>(this);
            }

        private:

            // overload used if i_source is an r-value
            template <typename ELEMENT_COMPLETE_TYPE>
                void push_impl(ELEMENT_COMPLETE_TYPE && i_source, std::true_type)
            {
                using ElementCompleteType = typename std::decay<ELEMENT_COMPLETE_TYPE>::type;
                push(runtime_type::template make<ElementCompleteType>(),
                    [&i_source](const runtime_type &, void * i_dest) {
                    auto const result = new(i_dest) ElementCompleteType(std::move(i_source));
                    return static_cast<ELEMENT*>(result);
                });
            }

            // overload used if i_source is an l-value
            template <typename ELEMENT_COMPLETE_TYPE>
                void push_impl(ELEMENT_COMPLETE_TYPE && i_source, std::false_type)
            {
                using ElementCompleteType = typename std::decay<ELEMENT_COMPLETE_TYPE>::type;
                push(runtime_type::template make<ElementCompleteType>(),
                    [&i_source](const runtime_type &, void * i_dest) {
                    auto const result = new(i_dest) ElementCompleteType(i_source);
                    return static_cast<ELEMENT*>(result);
                });
            }

        private:
            std::atomic<queue_header*> m_first; /**< Pointer to the first page of the queue. Consumer will try to consume from this page first. */
            std::atomic<uintptr_t> m_last; /**< Pointer to the first page of the queue, plus the exclusive-access flag (the least significant bit).
                                                A thread gets exclusive-access on this pointer if it succeeds in setting this bit to 1.
                                                The address of the page is obtained by masking away the first bit. Producer threads try to push
                                                new elements in this page first. */

            std::atomic_flag m_can_delete_page; /** Atomic flag used to synchronize consumer threads that attempt to delete the first page. */

            std::mutex m_hazard_pointers_mutex; /** This mutex protects the linked list of consumer, whose head is m_first_consumer. */
            consumer * m_first_consumer; /* Head of the linked list of consumers. Each consumer has an hazard pointer, that is the page
                                            that the consumer is using. A consumer can delete a page only when it is not the only page in the queue
                                            (that is, no producer is possibly using it), and no other consumer has this page in its hazard pointer. */
        };

    } // namespace experimental

} // namespace density
