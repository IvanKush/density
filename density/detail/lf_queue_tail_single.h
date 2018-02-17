
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2017.
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
        template < typename COMMON_TYPE, typename RUNTIME_TYPE, typename ALLOCATOR_TYPE, consistency_model CONSISTENCY_MODEL>
            class LFQueue_Tail<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE, concurrency_single, CONSISTENCY_MODEL>
                : public LFQueue_Base<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE, 
                    LFQueue_Tail<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE, concurrency_single, CONSISTENCY_MODEL> >
        {
        public:

            using Base = LFQueue_Base<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE,
                LFQueue_Tail<COMMON_TYPE, RUNTIME_TYPE, ALLOCATOR_TYPE, concurrency_single, CONSISTENCY_MODEL> >;
            using typename Base::ControlBlock;
            using typename Base::Allocation;
            using Base::same_page;
            using Base::min_alignment;
            using Base::s_alloc_granularity;
            using Base::s_type_offset;
            using Base::s_element_min_offset;
            using Base::s_rawblock_min_offset;
            using Base::s_end_control_offset;
            using Base::s_max_size_inpage;
            using Base::s_invalid_control_block;
            using Base::type_after_control;
            using Base::get_unaligned_element;
            using Base::get_element;
            using Base::invalid_control_block;
            using Base::commit_put_impl;
            using Base::cancel_put_impl;
            using Base::cancel_put_nodestroy_impl;

            /* This specialization of LFQueue_Tail does not need zeroed pages */
            constexpr static bool s_deallocate_zeroed_pages = false;

            /* No need for the end-block synchronization */
            constexpr static bool s_needs_end_control = false;

            constexpr LFQueue_Tail()
                    noexcept(std::is_nothrow_default_constructible<Base>::value)
                : m_tail(s_invalid_control_block),
                  m_initial_page(nullptr)
            {
            }

            constexpr LFQueue_Tail(ALLOCATOR_TYPE && i_allocator) 
                    noexcept(std::is_nothrow_constructible<Base, ALLOCATOR_TYPE &&>::value)
                : Base(std::move(i_allocator)),
                  m_tail(s_invalid_control_block),
                  m_initial_page(nullptr)
            {
            }

            constexpr LFQueue_Tail(const ALLOCATOR_TYPE & i_allocator)
                    noexcept(std::is_nothrow_constructible<Base, const ALLOCATOR_TYPE &>::value)
                : Base(i_allocator),
                  m_tail(s_invalid_control_block),
                  m_initial_page(nullptr)
            {
            }

            LFQueue_Tail(LFQueue_Tail && i_source)
                    noexcept(std::is_nothrow_default_constructible<Base>::value) // this matchees the the default ctor
                : LFQueue_Tail()
            {
                static_assert(noexcept(swap(i_source)), "swap must be noexcept");
                swap(i_source);
            }

            LFQueue_Tail & operator = (LFQueue_Tail && i_source) noexcept
            {
                static_assert(noexcept(swap(i_source)), "swap must be noexcept");
                swap(i_source);
                return *this;
            }

            // this function is not required to be threadsafe
            void swap(LFQueue_Tail & i_other) noexcept
            {
                // swap the allocator
                using std::swap;
                static_assert(noexcept(
                    swap(static_cast<ALLOCATOR_TYPE&>(*this), static_cast<ALLOCATOR_TYPE&>(i_other))
                ), "swap must be noexcept");
                swap(static_cast<ALLOCATOR_TYPE&>(*this), static_cast<ALLOCATOR_TYPE&>(i_other));

                // swap m_tail
                swap(m_tail, i_other.m_tail);

                // swap m_initial_page
                auto const tmp1 = i_other.m_initial_page.load();
                i_other.m_initial_page.store(m_initial_page.load());
                m_initial_page.store(tmp1);
            }

            ~LFQueue_Tail()
            {
                if (m_tail != s_invalid_control_block)
                {
                    ALLOCATOR_TYPE::deallocate_page(reinterpret_cast<void*>(m_tail));
                }
            }

            /** Allocates a block of memory.
                The block may be allocated in the pages or in a legacy memory block, depending on the size and the alignment.
                @param i_control_bits flags to add to the control block. Only NbQueue_Busy, NbQueue_Dead and NbQueue_External are allowed
                @param i_include_type true if this is an element value, false if it's a raw allocation
                @param i_size it must be > 0 and a multiple of the alignment
                @param i_alignment is must be > 0 and a power of two */
            template<LfQueue_ProgressGuarantee PROGRESS_GUARANTEE>
                Allocation try_inplace_allocate_impl(uintptr_t i_control_bits, bool i_include_type, size_t i_size, size_t i_alignment)
                    noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee = PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                DENSITY_ASSERT_INTERNAL((i_control_bits & ~(NbQueue_Dead | NbQueue_External | NbQueue_Busy)) == 0);
                DENSITY_ASSERT_INTERNAL(is_power_of_2(i_alignment) && (i_size % i_alignment) == 0);

                if (i_alignment < min_alignment)
                {
                    i_alignment = min_alignment;
                    i_size = uint_upper_align(i_size, min_alignment);
                }

                for (;;)
                {
                    DENSITY_ASSERT_INTERNAL(m_tail != 0 && uint_is_aligned(m_tail, s_alloc_granularity));

                    // allocate space for the control block (and possibly the runtime type)
                    auto new_tail = m_tail + (i_include_type ? s_element_min_offset : s_rawblock_min_offset);

                    // allocate the user space
                    new_tail = uint_upper_align(new_tail, i_alignment);
                    auto const user_storage = reinterpret_cast<void*>(new_tail);
                    new_tail = uint_upper_align(new_tail + i_size, s_alloc_granularity);
                    
                    // check for page overflow
                    auto const page_start = uint_lower_align(m_tail, ALLOCATOR_TYPE::page_alignment);
                    DENSITY_ASSERT_INTERNAL(new_tail > page_start);
                    auto const new_tail_offset = new_tail - page_start;
                    if (DENSITY_LIKELY(new_tail_offset <= s_end_control_offset))
                    {
                        /* null-terminate the next control-block before updating the new one, to prevent 
                            consumers from reaching an unitialized area. */
                        raw_atomic_store(&reinterpret_cast<ControlBlock*>(new_tail)->m_next, uintptr_t(0));

                        /* setup the new control block (note: i_control_bits has the flag NbQueue_Busy set) */
                        auto const new_block = reinterpret_cast<ControlBlock*>(m_tail);
                        auto const next_ptr = new_tail + i_control_bits;
                        DENSITY_ASSERT_INTERNAL(raw_atomic_load(&new_block->m_next, mem_relaxed) == 0);
                        raw_atomic_store(&new_block->m_next, next_ptr, mem_release);

                        m_tail = new_tail;
                        return { new_block, next_ptr, user_storage };
                    }
                    else if (i_size + (i_alignment - min_alignment) <= s_max_size_inpage) // if this allocation may fit in a page
                    {
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
                            DENSITY_ASSERT_INTERNAL(result != 0);
                        }
                    }
                    else
                    {
                        // this allocation would never fit in a page, allocate an external block
                        return external_allocate<PROGRESS_GUARANTEE>(i_control_bits, i_size, i_alignment);
                    }
                }
            }

            /** Overload of try_inplace_allocate_impl that can be used when all parameters are compile time constants */
            template <LfQueue_ProgressGuarantee PROGRESS_GUARANTEE, uintptr_t CONTROL_BITS, bool INCLUDE_TYPE, size_t SIZE, size_t ALIGNMENT>
                Allocation try_inplace_allocate_impl()
                    noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                static_assert((CONTROL_BITS & ~(NbQueue_Busy | NbQueue_Dead | NbQueue_External)) == 0, "");
                static_assert(is_power_of_2(ALIGNMENT) && (SIZE % ALIGNMENT) == 0, "");

                return try_inplace_allocate_impl<PROGRESS_GUARANTEE>(CONTROL_BITS, INCLUDE_TYPE, SIZE, ALIGNMENT);
            }

            ControlBlock * get_initial_page() const noexcept
            {
                return m_initial_page.load();
            }
            
        private:

            /** Used by inplace_allocate when the block can't be allocated in a page. */
            template <LfQueue_ProgressGuarantee PROGRESS_GUARANTEE>
                Allocation external_allocate(uintptr_t i_control_bits, size_t i_size, size_t i_alignment)
                    noexcept(PROGRESS_GUARANTEE != LfQueue_Throwing)
            {
                auto guarantee = PROGRESS_GUARANTEE; // used to avoid warnings about constant conditional expressions

                void * external_block;
                if (guarantee == LfQueue_Throwing)
                {
                    external_block = ALLOCATOR_TYPE::allocate(i_size, i_alignment);
                }
                else
                {
                    external_block = ALLOCATOR_TYPE::try_allocate(ToDenGuarantee(PROGRESS_GUARANTEE), i_size, i_alignment);
                    if (external_block == nullptr)
                    {
                        return Allocation();
                    }
                }

                try
                {
                    /* external blocks always allocate space for the type, because it would be complicated
                        for the consumers to handle both cases*/
                    auto const inplace_put = try_inplace_allocate_impl<PROGRESS_GUARANTEE>(i_control_bits | NbQueue_External, true, sizeof(ExternalBlock), alignof(ExternalBlock));
                    if (inplace_put.m_user_storage == nullptr)
                    {
                        ALLOCATOR_TYPE::deallocate(external_block, i_size, i_alignment);
                        return Allocation();
                    }
                    new(inplace_put.m_user_storage) ExternalBlock{external_block, i_size, i_alignment};
                    return Allocation{ inplace_put.m_control_block, inplace_put.m_next_ptr, external_block };
                }
                catch (...)
                {
                    /* if inplace_allocate fails, that means that we were able to allocate the external block,
                        but we were not able to put the struct ExternalBlock in the page (because a new page was
                        necessary, but we could not allocate it). */
                    ALLOCATOR_TYPE::deallocate(external_block, i_size, i_alignment);
                    DENSITY_INTERNAL_RETHROW_FROM_NOEXCEPT;
                }
            }

            /** Handles a page overflow of the tail. This function may allocate a new page.
                @param i_progress_guarantee progress guarantee. If the function can't provide this guarantee, the function fails
                @param i_tail the value read from m_tail. Note that other threads may have updated m_tail
                    in then meanwhile.
                @return the new tail, or nullptr in case of failure. */
            DENSITY_NO_INLINE uintptr_t page_overflow(LfQueue_ProgressGuarantee i_progress_guarantee)
            {
                auto const new_page = static_cast<ControlBlock *>(
                    i_progress_guarantee == LfQueue_Throwing ? ALLOCATOR_TYPE::allocate_page() :
                    ALLOCATOR_TYPE::try_allocate_page(ToDenGuarantee(i_progress_guarantee)));
                DENSITY_ASSERT_INTERNAL(address_is_aligned(new_page, ALLOCATOR_TYPE::page_alignment));
                if (new_page == nullptr)
                {
                    // allocation failed
                    return 0;
                }
                
                // zero the first block of the new page
                raw_atomic_store(&new_page->m_next, uintptr_t(0), mem_release);
                
                if (m_tail == s_invalid_control_block)
                {
                    DENSITY_ASSERT_INTERNAL(m_initial_page.load() == nullptr);
                    m_initial_page.store(new_page);
                }
                else
                {
                    auto prev_block = reinterpret_cast<ControlBlock*>(m_tail);
                    DENSITY_ASSERT_INTERNAL(m_tail + sizeof(ControlBlock) <= 
                        uint_lower_align(m_tail, ALLOCATOR_TYPE::page_alignment) + ALLOCATOR_TYPE::page_size);
                    raw_atomic_store(&prev_block->m_next, reinterpret_cast<uintptr_t>(new_page) + NbQueue_Dead);
                }

                m_tail = reinterpret_cast<uintptr_t>(new_page);
                return m_tail;
            }

        private: // data members
            alignas(concurrent_alignment) uintptr_t m_tail;
            std::atomic<ControlBlock*> m_initial_page;
        };

    } // namespace detail

} // namespace density
