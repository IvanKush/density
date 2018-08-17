
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once

namespace density
{
    namespace detail
    {
        /** \internal Partial specialization of LFQueue_Tail for single-threaded producers. Since consumers don't need to access
            the tail to detect the end of the queue (it is basically null-terminated), this specialization can hold
            the tail in a non-atomic member variable. */
        template <
          typename RUNTIME_TYPE,
          typename ALLOCATOR_TYPE,
          consistency_model CONSISTENCY_MODEL>
        class LFQueue_Tail<RUNTIME_TYPE, ALLOCATOR_TYPE, concurrency_single, CONSISTENCY_MODEL>
            : public LFQueue_Base<
                RUNTIME_TYPE,
                ALLOCATOR_TYPE,
                LFQueue_Tail<RUNTIME_TYPE, ALLOCATOR_TYPE, concurrency_single, CONSISTENCY_MODEL>>
        {
          public:
            using Base = LFQueue_Base<
              RUNTIME_TYPE,
              ALLOCATOR_TYPE,
              LFQueue_Tail<RUNTIME_TYPE, ALLOCATOR_TYPE, concurrency_single, CONSISTENCY_MODEL>>;

            using Base::min_alignment;
            using Base::s_alloc_granularity;
            using Base::s_element_min_offset;
            using Base::s_end_control_offset;
            using Base::s_invalid_control_block;
            using Base::s_max_size_inpage;
            using Base::s_rawblock_min_offset;
            using typename Base::Allocation;
            using typename Base::ControlBlock;

            /* This specialization of LFQueue_Tail does not need zeroed pages */
            constexpr static bool s_deallocate_zeroed_pages = false;

            /** Whether page switch happens only at the control block returned by get_end_control_block.
                Used only for assertions. */
            constexpr static bool s_needs_end_control = false;

            constexpr LFQueue_Tail() noexcept
                : m_tail(s_invalid_control_block), m_initial_page(nullptr)
            {
            }

            constexpr LFQueue_Tail(ALLOCATOR_TYPE && i_allocator) noexcept
                : Base(std::move(i_allocator)), m_tail(s_invalid_control_block),
                  m_initial_page(nullptr)
            {
            }

            constexpr LFQueue_Tail(const ALLOCATOR_TYPE & i_allocator) noexcept
                : Base(i_allocator), m_tail(s_invalid_control_block), m_initial_page(nullptr)
            {
            }

            // move constructor
            LFQueue_Tail(LFQueue_Tail && i_source) noexcept : LFQueue_Tail()
            {
                swap(*this, i_source);
            }

            // move assignment
            LFQueue_Tail & operator=(LFQueue_Tail && i_source) noexcept
            {
                swap(*this, i_source);
                return *this;
            }

            // this function is not required to be threadsafe
            friend void swap(LFQueue_Tail & i_first, LFQueue_Tail & i_second) noexcept
            {
                // swap the base
                swap(static_cast<Base &>(i_first), static_cast<Base &>(i_second));

                // swap m_tail
                std::swap(i_first.m_tail, i_second.m_tail);

                // swap m_initial_page
                auto const tmp1 = i_second.m_initial_page.load();
                i_second.m_initial_page.store(i_first.m_initial_page.load());
                i_first.m_initial_page.store(tmp1);
            }

            ~LFQueue_Tail()
            {
                if (m_tail != s_invalid_control_block)
                {
                    ALLOCATOR_TYPE::deallocate_page(reinterpret_cast<void *>(m_tail));
                }
            }

            /** Allocates a block of memory.
                The block may be allocated in the pages or in a legacy memory block, depending on the size and the alignment.
                @param i_control_bits flags to add to the control block. Only LfQueue_Busy, LfQueue_Dead and LfQueue_External are allowed
                @param i_include_type true if this is an element value, false if it's a raw allocation
                @param i_size it must a multiple of the alignment
                @param i_alignment is must be > 0 and a power of two 

                The upper layers shouldn't use LfQueue_External:: external blocks are handled internally by indirect recursion. */
            template <LfQueue_ProgressGuarantee PROGRESS_GUARANTEE>
            Allocation try_inplace_allocate_impl(
              uintptr_t i_control_bits,
              bool      i_include_type,
              size_t    i_size,
              size_t    i_alignment) noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee =
                  PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                DENSITY_ASSERT_INTERNAL(
                  (i_control_bits & ~(LfQueue_Dead | LfQueue_External | LfQueue_Busy)) == 0);
                DENSITY_ASSERT_INTERNAL(is_power_of_2(i_alignment) && (i_size % i_alignment) == 0);

                if (i_alignment < min_alignment)
                {
                    i_alignment = min_alignment;
                    i_size      = uint_upper_align(i_size, min_alignment);
                }

                for (;;)
                {
                    DENSITY_ASSERT_INTERNAL(
                      m_tail != 0 && uint_is_aligned(m_tail, s_alloc_granularity));

                    // allocate space for the control block (and possibly the runtime type)
                    auto new_tail =
                      m_tail + (i_include_type ? s_element_min_offset : s_rawblock_min_offset);

                    // allocate the user space
                    new_tail                = uint_upper_align(new_tail, i_alignment);
                    auto const user_storage = reinterpret_cast<void *>(new_tail);
                    new_tail = uint_upper_align(new_tail + i_size, s_alloc_granularity);

                    // check for page overflow
                    auto const page_start =
                      uint_lower_align(m_tail, ALLOCATOR_TYPE::page_alignment);
                    DENSITY_ASSUME(new_tail > page_start);
                    auto const new_tail_offset = new_tail - page_start;
                    if (DENSITY_LIKELY(new_tail_offset <= s_end_control_offset))
                    {
                        /* null-terminate the next control-block before updating the new one, to prevent 
                            consumers from reaching an unitialized area. No memory ordering is required here */
                        raw_atomic_store(
                          &reinterpret_cast<ControlBlock *>(new_tail)->m_next,
                          uintptr_t(0),
                          mem_relaxed);
                        /* to investigate: this may probably be a non-atomic operation, because consumers can only
                            read this after acquiring the next write (to new_block->m_next). Anyway clang tsan
                            has reported it has a data race. Furthermore atomicity in this case should have no 
                            consequences on the generated code. 
                        
                            todo: the std::atomic_thread_fence in the page_allocator should be enough
                        */

                        /* setup the new control block. Here we use release ordering so that consumers
                            acquiring this control block can see the previous write. */
                        auto const new_block = reinterpret_cast<ControlBlock *>(m_tail);
                        auto const next_ptr  = new_tail + i_control_bits;
                        DENSITY_ASSERT_INTERNAL(
                          raw_atomic_load(&new_block->m_next, mem_relaxed) == 0);
                        raw_atomic_store(&new_block->m_next, next_ptr, mem_release);

                        // commit the allocation
                        m_tail = new_tail;
                        return {new_block, next_ptr, user_storage};
                    }
                    else if (
                      i_size + (i_alignment - min_alignment) <=
                      s_max_size_inpage) // if this allocation may fit in a page
                    {
                        /* allocate another page. Note: on success page_overflow sets m_tail to the beginning of
                            the new page */
                        auto const result = page_overflow(PROGRESS_GUARANTEE);
                        if (guarantee != LfQueue_Throwing)
                        {
                            if (result == 0)
                            {
                                return Allocation{};
                            }
                        }
                        else
                        {
                            // with LfQueue_Throwing page_overflow throws on failure
                            DENSITY_ASSUME(result != 0);
                        }
                    }
                    else // this allocation would never fit in a page, allocate an external block
                    {
                        // legacy heap allocations can only be blocking
                        if (guarantee == LfQueue_LockFree || guarantee == LfQueue_WaitFree)
                            return Allocation{};

                        return Base::template external_allocate<PROGRESS_GUARANTEE>(
                          i_control_bits, i_size, i_alignment);
                    }
                }
            }

            /** Overload of try_inplace_allocate_impl that can be used when all parameters are compile time constants */
            template <
              LfQueue_ProgressGuarantee PROGRESS_GUARANTEE,
              uintptr_t                 CONTROL_BITS,
              bool                      INCLUDE_TYPE,
              size_t                    SIZE,
              size_t                    ALIGNMENT>
            Allocation try_inplace_allocate_impl() noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee =
                  PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                static_assert(
                  (CONTROL_BITS & ~(LfQueue_Dead | LfQueue_External | LfQueue_Busy)) == 0,
                  "internal error");
                static_assert(
                  is_power_of_2(ALIGNMENT) && (SIZE % ALIGNMENT) == 0, "internal error");

                constexpr auto alignment = size_max(ALIGNMENT, min_alignment);
                constexpr auto size      = uint_upper_align(SIZE, alignment);
                constexpr auto can_fit_in_a_page =
                  size + (alignment - min_alignment) <= s_max_size_inpage;
                constexpr auto over_aligned = alignment > min_alignment;

                for (;;)
                {
                    DENSITY_ASSERT_INTERNAL(
                      m_tail != 0 && uint_is_aligned(m_tail, s_alloc_granularity));

                    // allocate space for the control block (and possibly the runtime type)
                    auto new_tail =
                      m_tail + (INCLUDE_TYPE ? s_element_min_offset : s_rawblock_min_offset);

                    // allocate the user space
                    if (over_aligned)
                        new_tail = uint_upper_align(new_tail, alignment);
                    DENSITY_ASSUME_UINT_ALIGNED(new_tail, alignment);
                    auto const user_storage = reinterpret_cast<void *>(new_tail);
                    new_tail = uint_upper_align(new_tail + size, s_alloc_granularity);

                    // check for page overflow
                    auto const page_start =
                      uint_lower_align(m_tail, ALLOCATOR_TYPE::page_alignment);
                    DENSITY_ASSUME(new_tail > page_start);
                    auto const new_tail_offset = new_tail - page_start;
                    if (DENSITY_LIKELY(new_tail_offset <= s_end_control_offset))
                    {
                        /* null-terminate the next control-block before updating the new one, to prevent
                        consumers from reaching an unitialized area. No memory ordering is required here */
                        raw_atomic_store(
                          &reinterpret_cast<ControlBlock *>(new_tail)->m_next,
                          uintptr_t(0),
                          mem_relaxed);
                        /* to investigate: this may probably be a non-atomic operation, because consumers can only
                            read this after acquiring the next write (to new_block->m_next). Anyway clang tsan
                            has reported it has a data race. Furthermore atomicity in this case should have no
                            consequences on the generated code. */

                        /* setup the new control block. Here we use release ordering so that consumers
                            acquiring this control block can see the previous write. */
                        auto const new_block = reinterpret_cast<ControlBlock *>(m_tail);
                        auto const next_ptr  = new_tail + CONTROL_BITS;
                        DENSITY_ASSERT_INTERNAL(
                          raw_atomic_load(&new_block->m_next, mem_relaxed) == 0);
                        raw_atomic_store(&new_block->m_next, next_ptr, mem_release);

                        // commit the allocation
                        m_tail = new_tail;
                        return {new_block, next_ptr, user_storage};
                    }
                    else if (can_fit_in_a_page) // if this allocation may fit in a page
                    {
                        /* allocate another page. Note: on success page_overflow sets m_tail to the beginning of
                            the new page */
                        auto const result = page_overflow(PROGRESS_GUARANTEE);
                        if (guarantee != LfQueue_Throwing)
                        {
                            if (result == 0)
                            {
                                return Allocation{};
                            }
                        }
                        else
                        {
                            // with LfQueue_Throwing page_overflow throws on failure
                            DENSITY_ASSUME(result != 0);
                        }
                    }
                    else // this allocation would never fit in a page, allocate an external block
                    {
                        // legacy heap allocations can only be blocking
                        if (guarantee == LfQueue_LockFree || guarantee == LfQueue_WaitFree)
                            return Allocation{};

                        return Base::template external_allocate<PROGRESS_GUARANTEE>(
                          CONTROL_BITS, size, alignment);
                    }
                }
            }

            /** This function is used by the consume layer to initialize the head on the first allocated page */
            ControlBlock * get_initial_page() const noexcept { return m_initial_page.load(); }

          private:
            /** Handles a page overflow of the tail. This function may allocate a new page.
                @param i_progress_guarantee progress guarantee. If the function can't provide this guarantee, the function fails
                @param i_tail the value read from m_tail. Note that other threads may have updated m_tail
                    in then meanwhile.
                @return the new tail, or 0 in case of failure. */
            DENSITY_NO_INLINE uintptr_t
                              page_overflow(LfQueue_ProgressGuarantee i_progress_guarantee)
            {
                auto const new_page = static_cast<ControlBlock *>(
                  i_progress_guarantee == LfQueue_Throwing
                    ? ALLOCATOR_TYPE::allocate_page()
                    : ALLOCATOR_TYPE::try_allocate_page(ToDenGuarantee(i_progress_guarantee)));
                DENSITY_ASSUME_ALIGNED(new_page, ALLOCATOR_TYPE::page_alignment);
                if (new_page == nullptr)
                {
                    // allocation failed
                    return 0;
                }

                // zero the first block of the new page
                raw_atomic_store(&new_page->m_next, uintptr_t(0), mem_relaxed);

                if (m_tail == s_invalid_control_block)
                {
                    /* virgin queue: this happens only once in the entire lifetime of the queue */
                    DENSITY_ASSERT_INTERNAL(m_initial_page.load() == nullptr);
                    m_initial_page.store(new_page, mem_release);
                }
                else
                {
                    /* Setup a dead block with the pointer to the new page. Unlike the multiple-producer cases,
                        this block is not allocated at a fixed position, and no padding dead blocks are necessary. */
                    auto const prev_block = reinterpret_cast<ControlBlock *>(m_tail);
                    DENSITY_ASSERT_INTERNAL(
                      m_tail + sizeof(ControlBlock) <=
                      uint_lower_align(m_tail, ALLOCATOR_TYPE::page_alignment) +
                        ALLOCATOR_TYPE::page_size);
                    raw_atomic_store(
                      &prev_block->m_next,
                      reinterpret_cast<uintptr_t>(new_page) + LfQueue_Dead,
                      mem_release);
                }

                // done
                m_tail = reinterpret_cast<uintptr_t>(new_page);
                return m_tail;
            }

          private: // data members
            alignas(destructive_interference_size) uintptr_t m_tail;
            std::atomic<ControlBlock *> m_initial_page;
        };

    } // namespace detail

} // namespace density
