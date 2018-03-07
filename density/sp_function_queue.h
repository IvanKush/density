
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <density/detail/function_runtime_type.h>
#include <density/sp_heter_queue.h>

namespace density
{
    template <
      typename CALLABLE,
      typename ALLOCATOR_TYPE                      = default_allocator,
      function_type_erasure   ERASURE              = function_standard_erasure,
      concurrency_cardinality PROD_CARDINALITY     = concurrency_multiple,
      concurrency_cardinality CONSUMER_CARDINALITY = concurrency_multiple,
      typename BUSY_WAIT_FUNC                      = default_busy_wait>
    class sp_function_queue;

    /** Heterogeneous FIFO pseudo-container specialized to hold callable objects. sp_function_queue is an adaptor for sp_heter_queue.

        sp_function_queue is a concurrent version of function_queue that uses a mix of lock free algorithms and spin locking.
        This class is very similar to lf_function_queue, with the difference that if multiple producers are supported, they
        use a spin-locking mutex to synchronize the write to the tail pointer.

        @tparam CALLABLE Signature required to the callable objects. Must be in the form RET_VAL (PARAMS...)
        @tparam ALLOCATOR_TYPE Allocator type to be used. This type must meet the requirements of both \ref UntypedAllocator_concept
                "UntypedAllocator" and \ref PagedAllocator_concept "PagedAllocator". The default is density::default_allocator.
        @tparam ERASURE Type erasure to use the callable objects. Must be a member of density::function_type_erasure.
        @tparam PROD_CARDINALITY specifies whether multiple threads can do put transactions concurrently. Must be a member of density::concurrency_cardinality.
        @tparam CONSUMER_CARDINALITY specifies whether multiple threads can do consume operations concurrently. Must be a member of density::concurrency_cardinality.
        @tparam BUSY_WAIT_FUNC callable object to be invoked (with an empty parameter list) in the body of the spin lock.
            The default is density::default_busy_wait, that calls std::this_thread::yield.

        If ERASURE == function_manual_clear, sp_function_queue is not able to destroy the callable objects without invoking them.
            This produces a performance benefit, but:
            - The function sp_function_queue::clear can't be used (calling it causes undefined behavior)
            - When the destructor of sp_function_queue is called, the queue must be already empty

        \n <b>Thread safeness</b>: A thread doing put operations and another thread doing consumes don't need to be synchronized.
                If PROD_CARDINALITY is concurrency_multiple, multiple threads are allowed to put without any synchronization.
                If CONSUMER_CARDINALITY is concurrency_multiple, multiple threads are allowed to consume without any synchronization.
        \n <b>Exception safeness</b>: Any function of sp_heter_queue is noexcept or provides the strong exception guarantee.

        This class template provides all the put functions provided by function_queue, and furthermore it adds the try_ variants, that:
        - Don't throw in case of failure allocating memory. Anyway they pass through any exception thrown by the constructor of the
            element and the constructor of the runtime type.
        - Allow to specify a progress guarantee to be respected by the overall operation. For example, if the lock-free guarantee is
            requested, but it requires a memory operation that the allocator is not able to complete in lock-freedom, the put fails.
            In the current implementation, wait-free put operation may fail even in isolation, because page pinning is lock-free but not wait-free.
    */
#ifndef DOXYGEN_DOC_GENERATION
    template <
      typename RET_VAL,
      typename... PARAMS,
      typename ALLOCATOR_TYPE,
      function_type_erasure   ERASURE,
      concurrency_cardinality PROD_CARDINALITY,
      concurrency_cardinality CONSUMER_CARDINALITY,
      typename BUSY_WAIT_FUNC>
    class sp_function_queue<
      RET_VAL(PARAMS...),
      ALLOCATOR_TYPE,
      ERASURE,
      PROD_CARDINALITY,
      CONSUMER_CARDINALITY,
      BUSY_WAIT_FUNC>
#else
    template <
      typename CALLABLE,
      typename ALLOCATOR_TYPE                      = default_allocator,
      function_type_erasure   ERASURE              = function_standard_erasure,
      concurrency_cardinality PROD_CARDINALITY     = concurrency_multiple,
      concurrency_cardinality CONSUMER_CARDINALITY = concurrency_multiple,
      typename BUSY_WAIT_FUNC                      = default_busy_wait>
    class sp_function_queue
#endif
    {
      private:
        using UnderlyingQueue = sp_heter_queue<
          void,
          detail::FunctionRuntimeType<ERASURE, RET_VAL(PARAMS...)>,
          ALLOCATOR_TYPE,
          PROD_CARDINALITY,
          CONSUMER_CARDINALITY,
          BUSY_WAIT_FUNC>;
        UnderlyingQueue m_queue;

      public:
        /** Whether multiple threads can do put operations on the same queue without any further synchronization. */
        static constexpr bool concurrent_puts = PROD_CARDINALITY == concurrency_multiple;

        /** Whether multiple threads can do consume operations on the same queue without any further synchronization. */
        static constexpr bool concurrent_consumes = CONSUMER_CARDINALITY == concurrency_multiple;

        /** Whether puts and consumes can be done concurrently without any further synchronization. In any case unsynchronized concurrency is
            constrained by concurrent_puts and concurrent_consumes. */
        static constexpr bool concurrent_put_consumes = true;

        /** Whether this queue is sequential consistent. */
        static constexpr bool is_seq_cst = true;

        /** Default constructor.

        \snippet sp_func_queue_examples.cpp sp_function_queue default construct example 1 */
        sp_function_queue() noexcept = default;

        /** Move constructor.

        \snippet sp_func_queue_examples.cpp sp_function_queue move construct example 1 */
        sp_function_queue(sp_function_queue && i_source) noexcept = default;

        /** Move assignment.

        \snippet sp_func_queue_examples.cpp sp_function_queue move assign example 1 */
        sp_function_queue & operator=(sp_function_queue && i_source) noexcept = default;

        /** Swaps two function queues.

        \snippet sp_func_queue_examples.cpp sp_function_queue swap example 1 */
        friend void swap(sp_function_queue & i_first, sp_function_queue & i_second) noexcept
        {
            using std::swap;
            swap(i_first.m_queue, i_second.m_queue);
        }

        /** Destructor */
        ~sp_function_queue()
        {
            auto erasure = ERASURE;
            if (erasure == function_manual_clear)
            {
                DENSITY_ASSERT(empty());
            }
        }

        /** Alias to sp_heter_queue::put_transaction. */
        template <typename ELEMENT_COMPLETE_TYPE>
        using put_transaction =
          typename UnderlyingQueue::template put_transaction<ELEMENT_COMPLETE_TYPE>;

        /** Alias to sp_heter_queue::reentrant_put_transaction. */
        template <typename ELEMENT_COMPLETE_TYPE>
        using reentrant_put_transaction =
          typename UnderlyingQueue::template reentrant_put_transaction<ELEMENT_COMPLETE_TYPE>;

        /** Alias to lf_heter_queue::consume_operation. */
        using consume_operation = typename UnderlyingQueue::consume_operation;

        /** Alias to lf_heter_queue::reentrant_consume_operation. */
        using reentrant_consume_operation = typename UnderlyingQueue::reentrant_consume_operation;

        /** Adds at the end of the queue a callable object.

        See sp_heter_queue::push for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue push example 1
        \snippet sp_func_queue_examples.cpp sp_function_queue push example 2
        \snippet sp_func_queue_examples.cpp sp_function_queue push example 3 */
        template <typename ELEMENT_COMPLETE_TYPE> void push(ELEMENT_COMPLETE_TYPE && i_source)
        {
            m_queue.push(std::forward<ELEMENT_COMPLETE_TYPE>(i_source));
        }

        /** Adds at the end of the queue a callable object of type <code>ELEMENT_COMPLETE_TYPE</code>, inplace-constructing it from
                a perfect forwarded parameter pack.
            \n <i>Note</i>: the template argument <code>ELEMENT_COMPLETE_TYPE</code> can't be deduced from the parameters so it must explicitly specified.

            See sp_heter_queue::emplace for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue emplace example 1 */
        template <typename ELEMENT_COMPLETE_TYPE, typename... CONSTRUCTION_PARAMS>
        void emplace(CONSTRUCTION_PARAMS &&... i_construction_params)
        {
            m_queue.template emplace<ELEMENT_COMPLETE_TYPE>(
              std::forward<CONSTRUCTION_PARAMS>(i_construction_params)...);
        }

        /** Begins a transaction that appends an element of type <code>ELEMENT_TYPE</code>, copy-constructing
                or move-constructing it from the source.

            See sp_heter_queue::start_push for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue start_push example 1 */
        template <typename ELEMENT_TYPE>
        put_transaction<typename std::decay<ELEMENT_TYPE>::type>
          start_push(ELEMENT_TYPE && i_source)
        {
            return m_queue.start_push(std::forward<ELEMENT_TYPE>(i_source));
        }


        /** Begins a transaction that appends an element of a type <code>ELEMENT_TYPE</code>,
            inplace-constructing it from a perfect forwarded parameter pack.

            See sp_heter_queue::start_emplace for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue start_emplace example 1 */
        template <typename ELEMENT_TYPE, typename... CONSTRUCTION_PARAMS>
        put_transaction<ELEMENT_TYPE> start_emplace(CONSTRUCTION_PARAMS &&... i_construction_params)
        {
            return m_queue.template start_emplace<ELEMENT_TYPE>(
              std::forward<ELEMENT_TYPE>(i_construction_params)...);
        }

        /** Adds at the end of the queue a callable object.

        See sp_heter_queue::reentrant_push for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue reentrant_push example 1 */
        template <typename ELEMENT_COMPLETE_TYPE>
        void reentrant_push(ELEMENT_COMPLETE_TYPE && i_source)
        {
            m_queue.reentrant_push(std::forward<ELEMENT_COMPLETE_TYPE>(i_source));
        }

        /** Adds at the end of the queue a callable object of type <code>ELEMENT_COMPLETE_TYPE</code>, inplace-constructing it from
                a perfect forwarded parameter pack.
            \n <i>Note</i>: the template argument <code>ELEMENT_COMPLETE_TYPE</code> can't be deduced from the parameters so it must explicitly specified.

            See sp_heter_queue::reentrant_emplace for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue reentrant_emplace example 1 */
        template <typename ELEMENT_COMPLETE_TYPE, typename... CONSTRUCTION_PARAMS>
        void reentrant_emplace(CONSTRUCTION_PARAMS &&... i_construction_params)
        {
            m_queue.template reentrant_emplace<ELEMENT_COMPLETE_TYPE>(
              std::forward<CONSTRUCTION_PARAMS>(i_construction_params)...);
        }

        /** Begins a transaction that appends an element of type <code>ELEMENT_TYPE</code>, copy-constructing
                or move-constructing it from the source.

            See sp_heter_queue::start_reentrant_push for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue start_reentrant_push example 1 */
        template <typename ELEMENT_TYPE>
        reentrant_put_transaction<typename std::decay<ELEMENT_TYPE>::type>
          start_reentrant_push(ELEMENT_TYPE && i_source)
        {
            return m_queue.start_reentrant_push(std::forward<ELEMENT_TYPE>(i_source));
        }


        /** Begins a transaction that appends an element of a type <code>ELEMENT_TYPE</code>,
            inplace-constructing it from a perfect forwarded parameter pack.

            See sp_heter_queue::start_reentrant_emplace for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue start_reentrant_emplace example 1 */
        template <typename ELEMENT_TYPE, typename... CONSTRUCTION_PARAMS>
        reentrant_put_transaction<ELEMENT_TYPE>
          start_reentrant_emplace(CONSTRUCTION_PARAMS &&... i_construction_params)
        {
            return m_queue.template start_reentrant_emplace<ELEMENT_TYPE>(
              std::forward<CONSTRUCTION_PARAMS>(i_construction_params)...);
        }

        /** Tries to add at the end of the queue a callable object respecting a progress guarantee.

        See sp_heter_queue::try_push for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue try_push example 1 */
        template <typename ELEMENT_COMPLETE_TYPE>
        bool try_push(
          progress_guarantee i_progress_guarantee,
          ELEMENT_COMPLETE_TYPE &&
            i_source) noexcept(noexcept(m_queue
                                          .try_push(
                                            i_progress_guarantee,
                                            std::forward<ELEMENT_COMPLETE_TYPE>(i_source))))
        {
            return m_queue.try_push(
              i_progress_guarantee, std::forward<ELEMENT_COMPLETE_TYPE>(i_source));
        }

        /** Tries to add at the end of the queue a callable object of type <code>ELEMENT_COMPLETE_TYPE</code> respecting a progress guarantee,
            inplace-constructing it from a perfect forwarded parameter pack.
            \n <i>Note</i>: the template argument <code>ELEMENT_COMPLETE_TYPE</code> can't be deduced from the parameters so it must explicitly specified.

            See sp_heter_queue::try_emplace for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue try_emplace example 1 */
        template <typename ELEMENT_COMPLETE_TYPE, typename... CONSTRUCTION_PARAMS>
        bool
          try_emplace(progress_guarantee i_progress_guarantee, CONSTRUCTION_PARAMS &&... i_construction_params) noexcept(
            noexcept(m_queue.template try_emplace<ELEMENT_COMPLETE_TYPE>(
              i_progress_guarantee, std::forward<CONSTRUCTION_PARAMS>(i_construction_params)...)))
        {
            return m_queue.template try_emplace<ELEMENT_COMPLETE_TYPE>(
              i_progress_guarantee, std::forward<CONSTRUCTION_PARAMS>(i_construction_params)...);
        }

        /** Tries to begin a transaction that appends an element of type <code>ELEMENT_TYPE</code>, copy-constructing
                or move-constructing it from the source.

            See sp_heter_queue::try_start_push for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue try_start_push example 1 */
        template <typename ELEMENT_TYPE>
        put_transaction<typename std::decay<ELEMENT_TYPE>::type> try_start_push(
          progress_guarantee i_progress_guarantee,
          ELEMENT_TYPE &&    i_source) noexcept(noexcept(m_queue
                                                        .try_start_push(
                                                          i_progress_guarantee,
                                                          std::forward<ELEMENT_TYPE>(i_source))))
        {
            return m_queue.try_start_push(
              i_progress_guarantee, std::forward<ELEMENT_TYPE>(i_source));
        }


        /** Tries to begin a transaction appends an element of a type <code>ELEMENT_TYPE</code>,
            inplace-constructing it from a perfect forwarded parameter pack.

            See sp_heter_queue::try_start_emplace for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue try_start_emplace example 1 */
        template <typename ELEMENT_TYPE, typename... CONSTRUCTION_PARAMS>
        put_transaction<ELEMENT_TYPE> try_start_emplace(
          progress_guarantee i_progress_guarantee,
          CONSTRUCTION_PARAMS &&... i_construction_params) noexcept(noexcept(m_queue
                                                                               .template try_start_emplace<
                                                                                 ELEMENT_TYPE>(
                                                                                 i_progress_guarantee,
                                                                                 std::forward<
                                                                                   ELEMENT_TYPE>(
                                                                                   i_construction_params)...)))
        {
            return m_queue.template try_start_emplace<ELEMENT_TYPE>(
              i_progress_guarantee, std::forward<ELEMENT_TYPE>(i_construction_params)...);
        }

        /** Tries to add at the end of the queue a callable object.

        See sp_heter_queue::try_reentrant_push for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue try_reentrant_push example 1 */
        template <typename ELEMENT_COMPLETE_TYPE>
        bool try_reentrant_push(
          progress_guarantee i_progress_guarantee,
          ELEMENT_COMPLETE_TYPE &&
            i_source) noexcept(noexcept(m_queue
                                          .try_reentrant_push(
                                            i_progress_guarantee,
                                            std::forward<ELEMENT_COMPLETE_TYPE>(i_source))))
        {
            return m_queue.try_reentrant_push(
              i_progress_guarantee, std::forward<ELEMENT_COMPLETE_TYPE>(i_source));
        }

        /** Tries to add at the end of the queue a callable object of type <code>ELEMENT_COMPLETE_TYPE</code>, inplace-constructing it from
                a perfect forwarded parameter pack.
            \n <i>Note</i>: the template argument <code>ELEMENT_COMPLETE_TYPE</code> can't be deduced from the parameters so it must explicitly specified.

            See sp_heter_queue::try_reentrant_emplace for a detailed description.

        \snippet sp_func_queue_examples.cpp sp_function_queue try_reentrant_emplace example 1 */
        template <typename ELEMENT_COMPLETE_TYPE, typename... CONSTRUCTION_PARAMS>
        bool try_reentrant_emplace(
          progress_guarantee i_progress_guarantee,
          CONSTRUCTION_PARAMS &&... i_construction_params) noexcept(noexcept(m_queue
                                                                               .template try_reentrant_emplace<
                                                                                 ELEMENT_COMPLETE_TYPE>(
                                                                                 i_progress_guarantee,
                                                                                 std::forward<
                                                                                   CONSTRUCTION_PARAMS>(
                                                                                   i_construction_params)...)))
        {
            return m_queue.template try_reentrant_emplace<ELEMENT_COMPLETE_TYPE>(
              i_progress_guarantee, std::forward<CONSTRUCTION_PARAMS>(i_construction_params)...);
        }

        /** Tries to begin a transaction appends an element of type <code>ELEMENT_TYPE</code>, copy-constructing
                or move-constructing it from the source.

            See sp_heter_queue::try_start_reentrant_push for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue try_start_reentrant_push example 1 */
        template <typename ELEMENT_TYPE>
        reentrant_put_transaction<typename std::decay<ELEMENT_TYPE>::type> try_start_reentrant_push(
          progress_guarantee i_progress_guarantee,
          ELEMENT_TYPE &&    i_source) noexcept(noexcept(m_queue
                                                        .try_start_reentrant_push(
                                                          i_progress_guarantee,
                                                          std::forward<ELEMENT_TYPE>(i_source))))
        {
            return m_queue.try_start_reentrant_push(
              i_progress_guarantee, std::forward<ELEMENT_TYPE>(i_source));
        }


        /** Tries to begin a transaction appends an element of a type <code>ELEMENT_TYPE</code>,
            inplace-constructing it from a perfect forwarded parameter pack.

            See sp_heter_queue::try_start_reentrant_emplace for a detailed description.

            <b>Examples</b>
            \snippet sp_func_queue_examples.cpp sp_function_queue try_start_reentrant_emplace example 1 */
        template <typename ELEMENT_TYPE, typename... CONSTRUCTION_PARAMS>
        reentrant_put_transaction<ELEMENT_TYPE> try_start_reentrant_emplace(
          progress_guarantee i_progress_guarantee,
          CONSTRUCTION_PARAMS &&... i_construction_params) noexcept(noexcept(m_queue
                                                                               .template try_start_reentrant_emplace<
                                                                                 ELEMENT_TYPE>(
                                                                                 i_progress_guarantee,
                                                                                 std::forward<
                                                                                   ELEMENT_TYPE>(
                                                                                   i_construction_params)...)))
        {
            return m_queue.template try_start_reentrant_emplace<ELEMENT_TYPE>(
              i_progress_guarantee, std::forward<ELEMENT_TYPE>(i_construction_params)...);
        }

        /** If the queue is not empty, invokes the first function object of the queue and then deletes it
            from the queue. Otherwise no operation is performed.

            @param i_params... parameters to be forwarded to the function object
            @return If RET_VAL is void, the return value is a boolean indicating whether a callable object was consumed.
                Otherwise the return value is an density::optional that contains the value returned by the callable object, or
                an empty density::optional in case the queue was empty.

            This function is not reentrant: if the callable object accesses in any way this queue, the behavior
            is undefined. Use sp_function_queue::try_reentrant_consume if you are not sure about what the callable object may do.

            \b Throws: unspecified
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no observable effects).

            \snippet sp_func_queue_examples.cpp sp_function_queue try_consume example 1 */
        typename std::conditional<std::is_void<RET_VAL>::value, bool, density::optional<RET_VAL>>::
          type
          try_consume(PARAMS... i_params)
        {
            return try_consume_impl(std::is_void<RET_VAL>(), std::forward<PARAMS>(i_params)...);
        }

        /** If the queue is not empty, invokes the first function object of the queue and then deletes it
            from the queue. Otherwise no operation is performed.

            The consume operation is performed using the provided consume_operation object. If the element to consume
            is in the same page of the last element visited by the provided consume operation, the implementation
            does not need to pin page. For this reason this overload of try_consume is much faster than the other.

            @param i_consume object to use for the consume operation
            @param i_params... parameters to be forwarded to the function object
            @return If RET_VAL is void, the return value is a boolean indicating whether a callable object was consumed.
                Otherwise the return value is an optional that contains the value returned by the callable object, or
                an empty optional in case the queue was empty.

            This function is not reentrant: if the callable object accesses in any way this queue, the behavior
            is undefined. Use sp_function_queue::try_reentrant_consume if you are not sure about what the callable object may do.

            \b Throws: unspecified
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no observable effects).

            \snippet sp_func_queue_examples.cpp sp_function_queue try_consume example 2 */
        typename std::conditional<std::is_void<RET_VAL>::value, bool, optional<RET_VAL>>::type
          try_consume(consume_operation & i_consume, PARAMS... i_params)
        {
            return try_consume_impl_cached(
              std::is_void<RET_VAL>(), i_consume, std::forward<PARAMS>(i_params)...);
        }

        /** If the queue is not empty, invokes the first function object of the queue and then deletes it
            from the queue. Otherwise no operation is performed.

            @param i_params... parameters to be forwarded to the function object
            @return If RET_VAL is void, the return value is a boolean indicating whether a callable object was consumed.
                Otherwise the return value is an density::optional that contains the value returned by the callable object, or
                an empty density::optional in case the queue was empty.

            This function is reentrant: the callable object can access in any way this queue.

            \b Throws: unspecified
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no observable effects).

            \snippet sp_func_queue_examples.cpp sp_function_queue try_reentrant_consume example 1 */
        typename std::conditional<std::is_void<RET_VAL>::value, bool, density::optional<RET_VAL>>::
          type
          try_reentrant_consume(PARAMS... i_params)
        {
            return try_reentrant_consume_impl(
              std::is_void<RET_VAL>(), std::forward<PARAMS>(i_params)...);
        }

        /** If the queue is not empty, invokes the first function object of the queue and then deletes it
            from the queue. Otherwise no operation is performed.

            The consume operation is performed using the provided consume_operation object. If the element to consume
            is in the same page of the last element visited by the provided consume operation, the implementation
            does not need to pin page. For this reason this overload of try_reentrant_consume is much faster than the other.

            @param i_consume object to use for the consume operation
            @param i_params... parameters to be forwarded to the function object
            @return If RET_VAL is void, the return value is a boolean indicating whether a callable object was consumed.
                Otherwise the return value is an optional that contains the value returned by the callable object, or
                an empty optional in case the queue was empty.

            This function is reentrant: the callable object can access in any way this queue.

            \b Throws: unspecified
            \n <b>Exception guarantee</b>: strong (in case of exception the function has no observable effects).

            \snippet sp_func_queue_examples.cpp sp_function_queue try_reentrant_consume example 2 */
        typename std::conditional<std::is_void<RET_VAL>::value, bool, optional<RET_VAL>>::type
          try_reentrant_consume(reentrant_consume_operation & i_consume, PARAMS... i_params)
        {
            return try_reentrant_consume_impl_cached(
              std::is_void<RET_VAL>(), i_consume, std::forward<PARAMS>(i_params)...);
        }

        /** Deletes all the callable objects in the queue. This function is disabled at conpile-time if ERASURE is function_manual_clear.

            \n<b> Effects on iterators </b>: all the iterators are invalidated
            \n\b Throws: nothing
            \n\b Complexity: linear.

        \snippet sp_func_queue_examples.cpp sp_function_queue clear example 1 */
        template <
          function_type_erasure ERASURE_                                     = ERASURE,
          typename std::enable_if<ERASURE_ != function_manual_clear>::type * = nullptr>
        void clear() noexcept
        {
            auto erasure = ERASURE;
            if (erasure == function_manual_clear)
            {
                DENSITY_ASSERT(false);
            }
            else
            {
                m_queue.clear();
            }
        }

        /** Returns whether this container is empty */
        bool empty() noexcept { return m_queue.empty(); }

      private:
        /** \internal - try_consume_impl - non-void return type, temporary consume_operation */
        optional<RET_VAL> try_consume_impl(std::false_type, PARAMS... i_params)
        {
            if (auto cons = m_queue.try_start_consume())
            {
                auto && result = cons.complete_type().align_invoke_destroy(
                  cons.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                cons.commit_nodestroy();
                return optional<RET_VAL>(std::move(result));
            }
            else
            {
                return optional<RET_VAL>();
            }
        }

        /** \internal - try_consume_impl - void return type, temporary consume_operation */
        bool try_consume_impl(std::true_type, PARAMS... i_params)
        {
            if (auto cons = m_queue.try_start_consume())
            {
                cons.complete_type().align_invoke_destroy(
                  cons.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                cons.commit_nodestroy();
                return true;
            }
            else
            {
                return false;
            }
        }

        /** \internal - try_consume_impl_cached - non-void return type, cached consume_operation */
        optional<RET_VAL> try_consume_impl_cached(
          std::false_type, consume_operation & i_consume, PARAMS... i_params)
        {
            if (m_queue.try_start_consume(i_consume))
            {
                auto && result = i_consume.complete_type().align_invoke_destroy(
                  i_consume.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                i_consume.commit_nodestroy();
                return optional<RET_VAL>(std::move(result));
            }
            else
            {
                return optional<RET_VAL>();
            }
        }

        /** \internal - try_consume_impl_cached - void return type, cached consume_operation */
        bool
          try_consume_impl_cached(std::true_type, consume_operation & i_consume, PARAMS... i_params)
        {
            if (m_queue.try_start_consume(i_consume))
            {
                i_consume.complete_type().align_invoke_destroy(
                  i_consume.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                i_consume.commit_nodestroy();
                return true;
            }
            else
            {
                return false;
            }
        }

        /** \internal - try_reentrant_consume_impl - non-void return type, temporary consume_operation */
        optional<RET_VAL> try_reentrant_consume_impl(std::false_type, PARAMS... i_params)
        {
            if (auto cons = m_queue.try_start_reentrant_consume())
            {
                auto && result = cons.complete_type().align_invoke_destroy(
                  cons.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                cons.commit_nodestroy();
                return optional<RET_VAL>(std::move(result));
            }
            else
            {
                return optional<RET_VAL>();
            }
        }

        /** \internal - try_reentrant_consume_impl - void return type, temporary consume_operation */
        bool try_reentrant_consume_impl(std::true_type, PARAMS... i_params)
        {
            if (auto cons = m_queue.try_start_reentrant_consume())
            {
                cons.complete_type().align_invoke_destroy(
                  cons.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                cons.commit_nodestroy();
                return true;
            }
            else
            {
                return false;
            }
        }

        /** \internal - try_reentrant_consume_impl_cached - non-void return type, cached consume_operation */
        optional<RET_VAL> try_reentrant_consume_impl_cached(
          std::false_type, reentrant_consume_operation & i_consume, PARAMS... i_params)
        {
            if (m_queue.try_start_reentrant_consume(i_consume))
            {
                auto && result = i_consume.complete_type().align_invoke_destroy(
                  i_consume.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                i_consume.commit_nodestroy();
                return optional<RET_VAL>(std::move(result));
            }
            else
            {
                return optional<RET_VAL>();
            }
        }

        /** \internal - try_reentrant_consume_impl_cached - void return type, cached consume_operation */
        bool try_reentrant_consume_impl_cached(
          std::true_type, reentrant_consume_operation & i_consume, PARAMS... i_params)
        {
            if (m_queue.try_start_reentrant_consume(i_consume))
            {
                i_consume.complete_type().align_invoke_destroy(
                  i_consume.unaligned_element_ptr(), std::forward<PARAMS>(i_params)...);
                i_consume.commit_nodestroy();
                return true;
            }
            else
            {
                return false;
            }
        }
    };

} // namespace density
