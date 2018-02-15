
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <density/raw_atomic.h>
#include <type_traits>

namespace density
{
    namespace detail
    {
        template<typename COMMON_TYPE> struct LfQueueControl // used by lf_heter_queue<T,...>
        {
            atomic_uintptr_t m_next; // raw atomic
            COMMON_TYPE * m_element;
        };

        template<> struct LfQueueControl<void> // used by lf_heter_queue<void,...>
        {
            atomic_uintptr_t m_next; // raw atomic
        };

        enum NbQueue_Flags : uintptr_t
        {
            NbQueue_Busy = 1, /**< set on LfQueueControl::m_next when a thread is producing or consuming an element */
            NbQueue_Dead = 2,  /**< set on LfQueueControl::m_next when an element is not consumable.
                               If NbQueue_Dead is set, then NbQueue_Busy is meaningless.
                               This flag is not revertible: once it is set, it can't be removed. */
            NbQueue_External = 4,  /**< set on LfQueueControl::m_next in case of external allocation */
            NbQueue_InvalidNextPage = 8,  /**< initial value for the pointer to the next page */
            NbQueue_AllFlags = NbQueue_Busy | NbQueue_Dead | NbQueue_External | NbQueue_InvalidNextPage
        };

        /** \internal Internally we do not distinguish between progress_lock_free and progress_obstruction_free, and furthermore
            in the implementation functions we need to know if we are inside a try function (and we can't throw) or
            inside a non-try function (possibly throwing, and always with blocking progress guarantee) */
        enum LfQueue_ProgressGuarantee
        {
            LfQueue_Throwing, /**< maps to progress_blocking, allows exceptions */
            LfQueue_Blocking, /**< maps to progress_blocking, noexcept */
            LfQueue_LockFree, /**< maps to progress_lock_free and progress_obstruction_free, noexcept */
            LfQueue_WaitFree /**< maps to progress_lock_free and progress_wait_free, noexcept */
        };

        constexpr LfQueue_ProgressGuarantee ToLfGuarantee(progress_guarantee i_progress_guarantee, bool i_can_throw)
        {
            return i_can_throw ? LfQueue_Throwing : (
                i_progress_guarantee == progress_blocking ? LfQueue_Blocking : (
                (i_progress_guarantee == progress_lock_free || i_progress_guarantee == progress_obstruction_free) ? LfQueue_LockFree : LfQueue_WaitFree
            ));
        }

        constexpr progress_guarantee ToDenGuarantee(LfQueue_ProgressGuarantee i_progress_guarantee)
        {
            return (i_progress_guarantee == LfQueue_Throwing || i_progress_guarantee == LfQueue_Blocking) ? progress_blocking : (
                i_progress_guarantee == LfQueue_LockFree ? progress_lock_free : progress_wait_free
            );
        }

        template <typename COMMON_TYPE, typename RUNTIME_TYPE, typename ALLOCATOR_TYPE>
            class LFQueue_Base : public ALLOCATOR_TYPE
        {
        protected:

            using ControlBlock = LfQueueControl<COMMON_TYPE>;

            /** \internal This struct contains the result of a low-level allocation. */
            struct Block
            {
                LfQueueControl<COMMON_TYPE> * m_control_block;
                uintptr_t m_next_ptr;
                void * m_user_storage;

                Block() noexcept : m_user_storage(nullptr) {}

                Block(LfQueueControl<COMMON_TYPE> * i_control_block, uintptr_t i_next_ptr, void * i_user_storage) noexcept
                    : m_control_block(i_control_block), m_next_ptr(i_next_ptr), m_user_storage(i_user_storage) {}
            };

            /** Minimum alignment used for the storage of the elements.
            The storage of elements is always aligned according to the most-derived type. */
            constexpr static size_t min_alignment = alignof(void*); /* there are no particular requirements on
                                                                    the choice of this value: it just should be a very common alignment. */

            LFQueue_Base() noexcept(std::is_nothrow_default_constructible<ALLOCATOR_TYPE>::value) = default;

            LFQueue_Base(ALLOCATOR_TYPE && i_allocator) noexcept
                : ALLOCATOR_TYPE(std::move(i_allocator))
            {
            }

            LFQueue_Base(const ALLOCATOR_TYPE & i_allocator)
                : ALLOCATOR_TYPE(i_allocator)
            {
            }

            /** Returns whether the input addresses belong to the same page or they are both nullptr */
            static bool same_page(const void * i_first, const void * i_second) noexcept
            {
                auto const page_mask = ALLOCATOR_TYPE::page_alignment - 1;
                return ((reinterpret_cast<uintptr_t>(i_first) ^ reinterpret_cast<uintptr_t>(i_second)) & ~page_mask) == 0;
            }

            /** Head and tail pointers are alway multiple of this constant. To avoid the need of
            upper-aligning the addresses of the control-block and the runtime type, we raise it to the
            maximum alignment between ControlBlock and RUNTIME_TYPE (which are unlikely to be overaligned).
            The ControlBlock is always at offset 0 in the layout of a value or raw block. */
            constexpr static uintptr_t s_alloc_granularity = size_max(size_max(concurrent_alignment,
                alignof(ControlBlock), alignof(RUNTIME_TYPE), alignof(ExternalBlock)),
                min_alignment, size_log2(NbQueue_AllFlags + 1));

            /** Offset of the runtime_type in the layout of a value */
            constexpr static uintptr_t s_type_offset = uint_upper_align(sizeof(ControlBlock), alignof(RUNTIME_TYPE));

            /** Minimum offset of the element in the layout of a value (The actual offset is dependent on
                the alignment of the element). */
            constexpr static uintptr_t s_element_min_offset = uint_upper_align(s_type_offset + sizeof(RUNTIME_TYPE), min_alignment);

            /** Minimum offset of a row block. (The actual offset is dependent on the alignment of the block). */
            constexpr static uintptr_t s_rawblock_min_offset = uint_upper_align(sizeof(ControlBlock), size_max(min_alignment, alignof(ExternalBlock)));

            /** Offset from the beginning of the page of the end-control-block. */
            constexpr static uintptr_t s_end_control_offset = uint_lower_align(ALLOCATOR_TYPE::page_size - sizeof(ControlBlock), s_alloc_granularity);

            /** Maximum size for an element or raw block to be allocated in a page. */
            constexpr static size_t s_max_size_inpage = s_end_control_offset - s_element_min_offset;

            /** Value used to initialize the head and the tail.
                This value is designed to always cause a page overflow in the fast path.
                This mechanism allows the default constructor to be small, fast, and noexcept. */
            constexpr static uintptr_t s_invalid_control_block = s_end_control_offset;

            // some static checks
            static_assert(ALLOCATOR_TYPE::page_size > sizeof(ControlBlock) &&
                s_end_control_offset > 0 && s_end_control_offset > s_element_min_offset, "pages are too small");
            static_assert(is_power_of_2(s_alloc_granularity), "isn't concurrent_alignment a power of 2?");
        };

        /** \internal Class template that implements the low-level interface for put transactions */
        template < typename COMMON_TYPE, typename RUNTIME_TYPE, typename ALLOCATOR_TYPE,
            concurrency_cardinality PROD_CARDINALITY, consistency_model CONSISTENCY_MODEL >
                class LFQueue_Tail;

        /** \internal Class template that implements the low-level interface for consume operations */
        template < typename COMMON_TYPE, typename RUNTIME_TYPE, typename ALLOCATOR_TYPE,
            concurrency_cardinality CONSUMER_CARDINALITY, typename QUEUE_TAIL >
                class LFQueue_Head;

    } // namespace detail

} // namespace density
