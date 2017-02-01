
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <density/runtime_type.h>

namespace density
{
    namespace detail
    {
        template <typename BASE_CLASS, typename... TYPES>
            struct AllCovariant
        {
            static const bool value = true;
        };

        template <typename BASE_CLASS, typename FIRST_TYPE, typename... OTHER_TYPES>
            struct AllCovariant<BASE_CLASS, FIRST_TYPE, OTHER_TYPES...>
        {
            static const bool value = std::is_convertible<FIRST_TYPE*, BASE_CLASS*>::value &&
                AllCovariant<BASE_CLASS, OTHER_TYPES...>::value;
        };

        template < typename VOID_ALLOCATOR, typename RUNTIME_TYPE >
            class ArrayImpl : private VOID_ALLOCATOR
        {
        private:

            #if DENSITY_DEBUG_INTERNAL
                void check_invariants() const
                {
                    if (m_control_blocks != nullptr)
                    {
                        Header * const header = reinterpret_cast<Header*>(m_control_blocks) - 1;
                        DENSITY_ASSERT_INTERNAL(header->m_count > 0);
                    }
                }
            #endif

        public:

            struct ControlBlock : RUNTIME_TYPE
            {
                ControlBlock(const RUNTIME_TYPE & i_type, void * i_element) noexcept
                    : RUNTIME_TYPE(i_type), m_element(i_element) { }

                void * const m_element;
            };

            size_t size() const noexcept
            {
                #if DENSITY_DEBUG_INTERNAL
                    check_invariants();
                #endif

                if (m_control_blocks != nullptr)
                {
                    Header * const header = reinterpret_cast<Header*>(m_control_blocks) - 1;
                    return header->m_count;
                }
                else
                {
                    return 0;
                }
            }

            bool empty() const noexcept
            {
                #if DENSITY_DEBUG_INTERNAL
                    check_invariants();
                #endif
                return m_control_blocks == nullptr;
            }

            void clear() noexcept
            {
                #if DENSITY_DEBUG_INTERNAL
                    check_invariants();
                #endif
                destroy_impl();
                m_control_blocks = nullptr;
            }

            // default constructor
            ArrayImpl() noexcept
                : m_control_blocks(nullptr)
                    { }

            // constructor with allocator
            ArrayImpl(const VOID_ALLOCATOR & i_allocator)
                : VOID_ALLOCATOR(i_allocator), m_control_blocks(nullptr)
                    { }

            // copy constructor
            ArrayImpl(const ArrayImpl & i_source)
                : VOID_ALLOCATOR(i_source.get_allocator())
            {
                #if DENSITY_DEBUG_INTERNAL
                    i_source.check_invariants();
                #endif
                copy_impl(i_source);
            }

            // copy constructor with allocator
            ArrayImpl(const ArrayImpl & i_source, const VOID_ALLOCATOR & i_allocator)
                : VOID_ALLOCATOR(i_allocator)
            {
                #if DENSITY_DEBUG_INTERNAL
                    i_source.check_invariants();
                #endif
                copy_impl(i_source);
            }

            // move constructor
            ArrayImpl(ArrayImpl && i_source) noexcept
                : VOID_ALLOCATOR(std::move(i_source.get_allocator()))
            {
                static_assert(std::is_nothrow_move_constructible<VOID_ALLOCATOR>::value,
                    "VOID_ALLOCATOR must have a noexcept move constructor");

                #if DENSITY_DEBUG_INTERNAL
                    i_source.check_invariants();
                #endif
                move_impl(std::move(i_source));
            }

            // move constructor with allocator
            ArrayImpl(ArrayImpl && i_source, const VOID_ALLOCATOR & i_allocator) noexcept
                : VOID_ALLOCATOR(i_allocator)
            {
                #if DENSITY_DEBUG_INTERNAL
                    i_source.check_invariants();
                #endif
                move_impl(std::move(i_source));
            }

            ArrayImpl & operator = (const ArrayImpl & i_source)
            {
                DENSITY_ASSERT(this != &i_source); // self assignment not supported

                #if DENSITY_DEBUG_INTERNAL
                    i_source.check_invariants();
                #endif

                // use a copy to provide the strong exception guarantee
                auto copy(i_source);
                *this = std::move(copy);
                return *this;
            }

            ArrayImpl & operator = (ArrayImpl && i_source) noexcept
            {
                static_assert( std::is_nothrow_move_assignable<VOID_ALLOCATOR>::value,
                    "VOID_ALLOCATOR must have a noexcept move assignment" );

                DENSITY_ASSERT(this != &i_source); // self assignment not supported

                #if DENSITY_DEBUG_INTERNAL
                    this->check_invariants();
                    i_source.check_invariants();
                #endif

                VOID_ALLOCATOR::operator = (std::move(i_source.get_allocator()));
                destroy_impl();
                move_impl(std::move(i_source));
                return *this;
            }

            ~ArrayImpl() noexcept
            {
                #if DENSITY_DEBUG_INTERNAL
                    check_invariants();
                #endif

                destroy_impl();
            }

            struct IteratorBaseImpl
            {
                IteratorBaseImpl() noexcept
                #if DENSITY_DEBUG_INTERNAL
                    : m_dbg_index(0), m_dbg_count(0)
                #endif
                    { }

                #if DENSITY_DEBUG_INTERNAL
                    IteratorBaseImpl(const ControlBlock * i_curr_control_block, size_t i_dbg_index, size_t i_dbg_count) noexcept
                        : m_curr_control_block(i_curr_control_block), m_dbg_index(i_dbg_index), m_dbg_count(i_dbg_count)
                        { }
                #else
                    IteratorBaseImpl(const ControlBlock * i_curr_control_block) noexcept
                        : m_curr_control_block(i_curr_control_block)
                            { }
                #endif

                void move_next() noexcept
                {
                    #if DENSITY_DEBUG_INTERNAL
                        DENSITY_ASSERT_INTERNAL(m_dbg_index < m_dbg_count);
                    #endif
                    m_curr_control_block++;
                    #if DENSITY_DEBUG_INTERNAL
                        m_dbg_index++;
                    #endif
                }

                void * element_ptr() const noexcept
                {
                    #if DENSITY_DEBUG_INTERNAL
                        DENSITY_ASSERT_INTERNAL(m_dbg_index < m_dbg_count);
                    #endif
                    return m_curr_control_block->m_element;
                }

                const RUNTIME_TYPE & complete_type() const noexcept
                {
                    #if DENSITY_DEBUG_INTERNAL
                        DENSITY_ASSERT_INTERNAL(m_dbg_index < m_dbg_count);
                    #endif
                    return *m_curr_control_block;
                }

                const ControlBlock * control() const noexcept
                {
                    #if DENSITY_DEBUG_INTERNAL
                        DENSITY_ASSERT_INTERNAL(m_dbg_index <= m_dbg_count);
                    #endif
                    return m_curr_control_block;
                }

                bool operator == (const IteratorBaseImpl & i_other) const noexcept
                {
                    return m_curr_control_block == i_other.m_curr_control_block;
                }

                bool operator != (const IteratorBaseImpl & i_other) const noexcept
                {
                    return m_curr_control_block != i_other.m_curr_control_block;
                }

                void operator ++ () noexcept
                {
                    #if DENSITY_DEBUG_INTERNAL
                        DENSITY_ASSERT_INTERNAL(m_dbg_index < m_dbg_count);
                    #endif
                    m_curr_control_block++;
                    #if DENSITY_DEBUG_INTERNAL
                        m_dbg_index++;
                    #endif
                }

            private:
                const ControlBlock * m_curr_control_block;
                #if DENSITY_DEBUG_INTERNAL
                    size_t m_dbg_index, m_dbg_count;
                #endif

            }; // class IteratorBaseImpl

            VOID_ALLOCATOR & get_allocator() noexcept { return *this; }
            const VOID_ALLOCATOR & get_allocator() const noexcept { return *this; }

            #if DENSITY_DEBUG_INTERNAL
                IteratorBaseImpl begin() const noexcept { return IteratorBaseImpl(m_control_blocks, 0, size() ); }
                IteratorBaseImpl end() const noexcept { auto array_size = size();
                    return IteratorBaseImpl(m_control_blocks + array_size, array_size, array_size); }
            #else
                IteratorBaseImpl begin() const noexcept { return IteratorBaseImpl(m_control_blocks); }
                IteratorBaseImpl end() const noexcept { return IteratorBaseImpl(m_control_blocks + size()); }
            #endif

            size_t get_size_not_empty() const noexcept
            {
                return (reinterpret_cast<Header*>(m_control_blocks) - 1)->m_count;
            }

            struct Header
            {
                size_t m_count;
            };

            struct ListBuilder
            {
                ListBuilder() noexcept
                    : m_control_blocks(nullptr)
                {
                }

                ListBuilder(const ListBuilder&) = delete;
                ListBuilder & operator = (const ListBuilder&) = delete;

                void init(VOID_ALLOCATOR & i_allocator, size_t i_count, size_t i_buffer_size, size_t i_buffer_alignment)
                {
                    void * const memory_block = i_allocator.allocate(i_buffer_size + sizeof(Header), i_buffer_alignment, sizeof(Header));
                    Header * header = static_cast<Header*>(memory_block);
                    header->m_count = i_count;
                    m_end_of_control_blocks = m_control_blocks = reinterpret_cast<ControlBlock*>(header + 1);
                    m_end_of_elements = m_elements = m_control_blocks + i_count;

                    #if DENSITY_DEBUG_INTERNAL
                        m_dbg_end_of_buffer = address_add(m_control_blocks, i_buffer_size);
                    #endif
                }

                /* Adds a (type-info, element) pair to the list. The new element is copy-constructed.
                    Note: ELEMENT is not the complete type of the element, as the
                    list allows polymorphic types. The use of the RUNTIME_TYPE avoid slicing or partial constructions. */
                void * add_by_copy(const RUNTIME_TYPE & i_element_info, const void * i_source)
                {
                    // copy-construct the element first (this may throw)
                    void * complete_new_element = address_upper_align(m_end_of_elements, i_element_info.alignment());
                    #if DENSITY_DEBUG_INTERNAL
                        dbg_check_range(complete_new_element, address_add(complete_new_element, i_element_info.size()));
                    #endif
                    const auto element_base = i_element_info.copy_construct(complete_new_element,
                        static_cast<const typename RUNTIME_TYPE::common_type*>(i_source) );
                    // from now on, for the whole function, we cant except
                    m_end_of_elements = address_add(complete_new_element, i_element_info.size());

                    // construct the typeinfo - if this would throw, the element just constructed would not be destroyed. A static_assert guarantees the noexcept-ness.
                    #if DENSITY_DEBUG_INTERNAL
                        dbg_check_range(m_end_of_control_blocks, m_end_of_control_blocks + 1);
                    #endif

                    static_assert(noexcept(new(m_end_of_control_blocks++) ControlBlock(i_element_info, element_base)),
                        "All the constructors of RUNTIME_TYPE are required not be noexcept");
                    new(m_end_of_control_blocks++) ControlBlock(i_element_info, element_base);
                    return element_base;
                }

                /* Adds a (element-info, element) pair to the list. The new element is move-constructed.
                    Note: ELEMENT is not the complete type of the element, as the
                    list allows polymorphic types. The use of the RUNTIME_TYPE avoid slicing or partial constructions. */
                void * add_by_move(const RUNTIME_TYPE & i_element_info, void * i_source)
                {
                    // copy-construct the element first (this may throw)
                    void * complete_new_element = address_upper_align(m_end_of_elements, i_element_info.alignment());
                    #if DENSITY_DEBUG_INTERNAL
                        dbg_check_range(complete_new_element, address_add(complete_new_element, i_element_info.size()));
                    #endif
                    const auto element_base = i_element_info.move_construct(complete_new_element,
                        static_cast<typename RUNTIME_TYPE::common_type*>(i_source));
                    // from now on, for the whole function, we cant except
                    m_end_of_elements = address_add(complete_new_element, i_element_info.size());

                    // construct the typeinfo - if this would throw, the element just constructed would not be destroyed. A static_assert guarantees the noexcept-ness.
                    #if DENSITY_DEBUG_INTERNAL
                        dbg_check_range(m_end_of_control_blocks, m_end_of_control_blocks + 1);
                    #endif

                    static_assert(noexcept(new(m_end_of_control_blocks++) ControlBlock(i_element_info, element_base)),
                        "All the constructors of RUNTIME_TYPE are required not be noexcept");
                    new(m_end_of_control_blocks++) ControlBlock(i_element_info, element_base);
                    return element_base;
                }

                void add_only_control_block(const RUNTIME_TYPE & i_element_info, void * i_element) noexcept
                {
                    static_assert(noexcept(new(m_end_of_control_blocks++) ControlBlock(i_element_info, i_element)),
                        "All the constructors of RUNTIME_TYPE are required not be noexcept");

                    #if DENSITY_DEBUG_INTERNAL
                        dbg_check_range(m_end_of_control_blocks, m_end_of_control_blocks + 1);
                    #endif
                    new(m_end_of_control_blocks++) ControlBlock(i_element_info, i_element);
                }

                ControlBlock * end_of_control_blocks()
                {
                    return m_end_of_control_blocks;
                }

                ControlBlock * control_blocks()
                {
                    return m_control_blocks;
                }

                void rollback(VOID_ALLOCATOR & i_allocator, size_t i_buffer_size, size_t i_buffer_alignment) noexcept
                {
                    if (m_control_blocks != nullptr)
                    {
                        for (ControlBlock * element_info = m_control_blocks; element_info < m_end_of_control_blocks; element_info++)
                        {
                            element_info->destroy(static_cast<typename RUNTIME_TYPE::common_type*>(element_info->m_element));
                            element_info->~ControlBlock();
                        }
                        i_allocator.deallocate(reinterpret_cast<Header*>(m_control_blocks) - 1, i_buffer_size + sizeof(Header), i_buffer_alignment, sizeof(Header));
                    }
                }

                #if DENSITY_DEBUG_INTERNAL
                    void dbg_check_range(const void * i_start, const void * i_end) noexcept
                    {
                        DENSITY_ASSERT_INTERNAL(i_start >= m_control_blocks && i_end <= m_dbg_end_of_buffer);
                    }
                #endif

                ControlBlock * m_control_blocks;
                void * m_elements;
                ControlBlock * m_end_of_control_blocks;
                void * m_end_of_elements;
                #if DENSITY_DEBUG_INTERNAL
                    void * m_dbg_end_of_buffer;
                #endif
            };

            ControlBlock * & edit_control_blocks() noexcept { return m_control_blocks; }

            ControlBlock * get_control_blocks() noexcept { return m_control_blocks; }
            const ControlBlock * get_control_blocks() const noexcept { return m_control_blocks; }

            void destroy_impl() noexcept
            {
                if (m_control_blocks != nullptr)
                {
                    size_t dense_alignment = std::alignment_of<ControlBlock>::value;
                    const auto end_it = end();
                    size_t dense_size = get_size_not_empty() * sizeof(ControlBlock);
                    for (auto it = begin(); it != end_it; ++it)
                    {
                        auto control_block = it.control();
                        const auto alignment_mask = control_block->alignment() - 1;
                        dense_size = (dense_size + alignment_mask) & ~alignment_mask;
                        dense_size += control_block->size();
                        dense_alignment = detail::size_max(dense_alignment, control_block->alignment());
                        control_block->destroy(static_cast<typename RUNTIME_TYPE::common_type*>(it.element_ptr()));
                        control_block->ControlBlock::~ControlBlock();
                    }

                    Header * const header = reinterpret_cast<Header*>(m_control_blocks) - 1;
                    get_allocator().deallocate(header, dense_size + sizeof(Header), dense_alignment, sizeof(Header));
                }
            }

            void copy_impl(const ArrayImpl & i_source)
            {
                if (i_source.m_control_blocks != nullptr)
                {
                    ListBuilder builder;
                    size_t buffer_size = 0, buffer_alignment = 0;
                    i_source.compute_buffer_size_and_alignment(&buffer_size, &buffer_alignment);
                    try
                    {
                        const auto source_size = i_source.get_size_not_empty();
                        builder.init(*static_cast<VOID_ALLOCATOR*>(this), source_size, buffer_size, buffer_alignment);
                        auto const end_it = i_source.end();
                        for (auto it = i_source.begin(); it != end_it; ++it)
                        {
                            builder.add_by_copy(it.complete_type(), it.element_ptr());
                        }

                        m_control_blocks = builder.control_blocks();
                    }
                    catch (...)
                    {
                        builder.rollback(*static_cast<VOID_ALLOCATOR*>(this), buffer_size, buffer_alignment);
                        throw;
                    }
                }
                else
                {
                    m_control_blocks = nullptr;
                }
            }

            void move_impl(ArrayImpl && i_source) noexcept
            {
                m_control_blocks = i_source.m_control_blocks;
                i_source.m_control_blocks = nullptr;
            }

            template <typename ELEMENT, typename... TYPES>
                static void make_impl(ArrayImpl & o_dest_list, TYPES &&... i_args)
            {
                DENSITY_ASSERT(o_dest_list.m_control_blocks == nullptr); // precondition

                size_t const buffer_size = RecursiveSize<RecursiveHelper<ELEMENT, TYPES...>::s_element_count * sizeof(ControlBlock), TYPES...>::s_buffer_size;
                size_t const buffer_alignment = detail::size_max(RecursiveHelper<ELEMENT, TYPES...>::s_element_alignment, std::alignment_of<ControlBlock>::value);
                size_t const element_count = RecursiveHelper<ELEMENT, TYPES...>::s_element_count;

                bool not_empty = element_count != 0; // this avoids 'warning C4127: conditional expression is constant' on msvc
                if (not_empty)
                {
                    ListBuilder builder;
                    try
                    {
                        builder.init(static_cast<VOID_ALLOCATOR&>(o_dest_list), element_count, buffer_size, buffer_alignment);

                        RecursiveHelper<ELEMENT, TYPES...>::construct(builder, std::forward<TYPES>(i_args)...);

                        o_dest_list.m_control_blocks = builder.control_blocks();
                    }
                    catch (...)
                    {
                        builder.rollback(static_cast<VOID_ALLOCATOR&>(o_dest_list), buffer_size, buffer_alignment);
                        throw;
                    }
                }

                #if DENSITY_DEBUG_INTERNAL
                    size_t dbg_buffer_size = 0, dbg_buffer_alignment = 0;
                    o_dest_list.compute_buffer_size_and_alignment(&dbg_buffer_size, &dbg_buffer_alignment);
                    DENSITY_ASSERT_INTERNAL(dbg_buffer_size == buffer_size);
                    DENSITY_ASSERT_INTERNAL(dbg_buffer_alignment == buffer_alignment);
                #endif
            }

            void compute_buffer_size_and_alignment(size_t * o_buffer_size, size_t * o_buffer_alignment) const noexcept
            {
                size_t buffer_size = size() * sizeof(ControlBlock);
                size_t buffer_alignment = std::alignment_of<ControlBlock>::value;
                auto const end_it = end();
                for (auto it = begin(); it != end_it; ++it)
                {
                    const size_t curr_size = it.complete_type().size();
                    const size_t curr_alignment = it.complete_type().alignment();
                    DENSITY_ASSERT(curr_size > 0 && is_power_of_2(curr_alignment));
                    buffer_size = (buffer_size + (curr_alignment - 1)) & ~(curr_alignment - 1);
                    buffer_size += curr_size;

                    buffer_alignment = detail::size_max(buffer_alignment, curr_alignment);
                }

                *o_buffer_size = buffer_size;
                *o_buffer_alignment = buffer_alignment;
            }

            void compute_buffer_size_and_alignment_for_insert(size_t * o_buffer_size, size_t * o_buffer_alignment,
                const ControlBlock * i_insert_at, size_t i_new_element_count, const RUNTIME_TYPE & i_new_type) const noexcept
            {
                DENSITY_ASSERT(i_new_type.size() > 0 && is_power_of_2(i_new_type.alignment())); // the size must be non-zero, the alignment must be a non-zero power of 2

                auto const current_size = size();
                size_t buffer_size = (current_size + i_new_element_count) * sizeof(ControlBlock);
                size_t buffer_alignment = detail::size_max(std::alignment_of<ControlBlock>::value, i_new_type.alignment());
                auto const end_it = end();
                for (auto it = begin(); ; ++it)
                {
                    if (it.control() == i_insert_at && i_new_element_count > 0)
                    {
                        const auto alignment_mask = i_new_type.alignment() - 1;
                        buffer_size = (buffer_size + alignment_mask) & ~alignment_mask;
                        buffer_size += i_new_type.size() * i_new_element_count;
                    }

                    if (it == end_it)
                    {
                        break;
                    }

                    const size_t curr_size = it.complete_type().size();
                    const size_t curr_alignment = it.complete_type().alignment();
                    DENSITY_ASSERT(curr_size > 0 && is_power_of_2(curr_alignment)); // the size must be non-zero, the alignment must be a non-zero power of 2
                    buffer_size = (buffer_size + (curr_alignment - 1)) & ~(curr_alignment - 1);
                    buffer_size += curr_size;
                    buffer_alignment = detail::size_max(buffer_alignment, curr_alignment);
                }

                *o_buffer_size = buffer_size;
                *o_buffer_alignment = buffer_alignment;
            }

            template <typename CONSTRUCTOR>
                IteratorBaseImpl insert_n_impl(const ControlBlock * i_position, size_t i_count_to_insert,
                    const RUNTIME_TYPE & i_source_type, CONSTRUCTOR && i_constructor)
            {
                DENSITY_ASSERT(i_count_to_insert > 0);

                const ControlBlock * return_control_box = nullptr;

                size_t buffer_size = 0, buffer_alignment = 0;
                compute_buffer_size_and_alignment_for_insert(&buffer_size, &buffer_alignment, i_position, i_count_to_insert, i_source_type);

                ListBuilder builder;
                IteratorBaseImpl it = begin();
                try
                {
                    builder.init(*static_cast<VOID_ALLOCATOR*>(this), size() + i_count_to_insert, buffer_size, buffer_alignment);

                    size_t count_to_insert = i_count_to_insert;
                    auto const end_it = end();
                    for (;;)
                    {
                        if (it.control() == i_position && count_to_insert > 0)
                        {
                            auto const end_of_control_blocks = builder.end_of_control_blocks();
                            i_constructor(builder, i_source_type);
                            if (count_to_insert == i_count_to_insert)
                            {
                                return_control_box = end_of_control_blocks;
                            }
                            count_to_insert--;
                        }
                        else
                        {
                            if (it == end_it)
                            {
                                break;
                            }
                            builder.add_by_move(it.complete_type(), it.element_ptr());
                            it.move_next();
                        }
                    }

                    destroy_impl();

                    m_control_blocks = builder.control_blocks();

                    #if DENSITY_DEBUG_INTERNAL
                        return IteratorBaseImpl(return_control_box, return_control_box - m_control_blocks, size());
                    #else
                        return IteratorBaseImpl(return_control_box);
                    #endif
                }
                catch (...)
                {
                    /* we iterate this list exactly like we did in the loop interrupted by the exception,
                        but we stop at 'it', the iterator we were using */
                    size_t count_to_insert = i_count_to_insert;
                    auto this_block = this->m_control_blocks;
                    auto this_element = static_cast<void*>( m_control_blocks + size() );

                    /* in the same loop we iterate over tmp, that is the list that we were creating. The
                        elements that were moved from this list to tmp have to be moved back to this list. The elements
                        that was just constructed have to be destroyed. */
                    ArrayImpl tmp;
                    tmp.m_control_blocks = builder.control_blocks();
                    if (tmp.m_control_blocks != nullptr) // if the allocation fails builder.control_blocks() is null
                    {
                        auto tmp_it = tmp.begin();
                        auto tmp_end = builder.end_of_control_blocks();

                        for (; tmp_it.control() != tmp_end; tmp_it.move_next())
                        {
                            if (this_block == i_position && count_to_insert > 0)
                            {
                                tmp_it.complete_type().destroy(static_cast<typename RUNTIME_TYPE::common_type*>(tmp_it.element_ptr()));
                                count_to_insert--;
                            }
                            else
                            {
                                this_element = address_upper_align(this_element, this_block->alignment());

                                tmp_it.complete_type().move_construct(this_element,
                                    static_cast<typename RUNTIME_TYPE::common_type*>(tmp_it.element_ptr()));
                                this_element = address_add(this_element, this_block->size());
                                this_block++;
                            }
                        }

                        Header * const header = reinterpret_cast<Header*>(tmp.m_control_blocks) - 1;
                        static_cast<VOID_ALLOCATOR*>(this)->deallocate(header, buffer_size + sizeof(Header), buffer_alignment, sizeof(Header));
                        tmp.m_control_blocks = nullptr;
                    }
                    throw;
                }
            }

            IteratorBaseImpl erase_impl(const ControlBlock * i_from, const ControlBlock * i_to)
            {
                // test preconditions
                const auto prev_size = get_size_not_empty();
                DENSITY_ASSERT(m_control_blocks != nullptr && i_from < i_to &&
                    i_from >= m_control_blocks && i_from <= m_control_blocks + prev_size &&
                    i_to >= m_control_blocks && i_to <= m_control_blocks + prev_size);

                const size_t size_to_remove = i_to - i_from;

                DENSITY_ASSERT(size_to_remove <= prev_size);
                if (size_to_remove == prev_size)
                {
                    // erasing all the elements
                    DENSITY_ASSERT(i_from == m_control_blocks && i_to == m_control_blocks + prev_size);
                    clear();
                    return begin();
                }
                else
                {
                    size_t buffer_size = 0, buffer_alignment = 0;
                    compute_buffer_size_and_alignment_for_erase(&buffer_size, &buffer_alignment, i_from, i_to);

                    ControlBlock * return_control_block = nullptr;

                    ListBuilder builder;
                    builder.init(*static_cast<VOID_ALLOCATOR*>(this), prev_size - size_to_remove, buffer_size, buffer_alignment);

                    const auto end_it = end();
                    bool is_in_range = false;
                    bool first_in_range = false;
                    for (auto it = begin(); ; ++it)
                    {
                        if (it.control() == i_from)
                        {
                            is_in_range = true;
                            first_in_range = true;
                        }
                        if (it.control() == i_to)
                        {
                            is_in_range = false;
                        }

                        if (it == end_it)
                        {
                            DENSITY_ASSERT(!is_in_range);
                            break;
                        }

                        if (!is_in_range)
                        {
                            auto const new_element_info = builder.end_of_control_blocks();
                            builder.add_by_move(it.complete_type(), it.element_ptr());

                            if (first_in_range)
                            {
                                return_control_block = new_element_info;
                                first_in_range = false;
                            }
                        }
                    }

                    if (return_control_block == nullptr) // if no elements were copied after the erased range
                    {
                        DENSITY_ASSERT(i_to == m_control_blocks + prev_size);
                        return_control_block = builder.end_of_control_blocks();
                    }

                    destroy_impl();

                    m_control_blocks = builder.control_blocks();
                    #if DENSITY_DEBUG_INTERNAL
                        return IteratorBaseImpl(return_control_block, return_control_block - m_control_blocks, size());
                    #else
                        return IteratorBaseImpl(return_control_block);
                    #endif
                }
            }

            void compute_buffer_size_and_alignment_for_erase(size_t * o_buffer_size, size_t * o_buffer_alignment,
                const ControlBlock * i_remove_from, const ControlBlock * i_remove_to) const noexcept
            {
                DENSITY_ASSERT(i_remove_to >= i_remove_from);
                const size_t size_to_remove = i_remove_to - i_remove_from;
                DENSITY_ASSERT(size() >= size_to_remove);
                size_t buffer_size = (size() - size_to_remove) * sizeof(ControlBlock);
                size_t buffer_alignment = std::alignment_of<ControlBlock>::value;

                bool in_range = false;
                auto const end_it = end();
                for (auto it = begin(); it != end_it; ++it)
                {
                    if (it.control() == i_remove_from)
                    {
                        in_range = true;
                    }
                    if (it.control() == i_remove_to)
                    {
                        in_range = false;
                    }

                    if (!in_range)
                    {
                        const size_t curr_size = it.complete_type().size();
                        const size_t curr_alignment = it.complete_type().alignment();
                        DENSITY_ASSERT(curr_size > 0 && is_power_of_2(curr_alignment)); // the size must be non-zero, the alignment must be a non-zero power of 2
                        buffer_size = (buffer_size + (curr_alignment - 1)) & ~(curr_alignment - 1);
                        buffer_size += curr_size;
                        buffer_alignment = detail::size_max(buffer_alignment, curr_alignment);
                    }
                }

                *o_buffer_size = buffer_size;
                *o_buffer_alignment = buffer_alignment;
            }

            template <size_t PREV_ELEMENTS_SIZE, typename...> struct RecursiveSize
            {
                static const size_t s_buffer_size = PREV_ELEMENTS_SIZE;
            };
            template <size_t PREV_ELEMENTS_SIZE, typename FIRST_TYPE, typename... OTHER_TYPES>
                struct RecursiveSize<PREV_ELEMENTS_SIZE, FIRST_TYPE, OTHER_TYPES...>
            {
                static const size_t s_aligned_prev_size = (PREV_ELEMENTS_SIZE + (std::alignment_of<FIRST_TYPE>::value - 1)) & ~(std::alignment_of<FIRST_TYPE>::value - 1);
                static const size_t s_buffer_size = RecursiveSize<s_aligned_prev_size + sizeof(FIRST_TYPE), OTHER_TYPES...>::s_buffer_size;
            };

            template <typename ELEMENT, typename... TYPES>
                struct RecursiveHelper
            {
                static const size_t s_element_count = 0;
                static const size_t s_element_alignment = 1;
                static void construct(ListBuilder &, TYPES &&...) { }
            };
            template <typename ELEMENT, typename FIRST_TYPE, typename... OTHER_TYPES>
                struct RecursiveHelper<ELEMENT, FIRST_TYPE, OTHER_TYPES...>
            {
                static_assert(std::alignment_of<FIRST_TYPE>::value > 0 && (std::alignment_of<FIRST_TYPE>::value & (std::alignment_of<FIRST_TYPE>::value - 1)) == 0,
                    "the alignment must be a non-zero integer power of 2");

                static const size_t s_element_count = 1 + RecursiveHelper<ELEMENT, OTHER_TYPES...>::s_element_count;
                static const size_t s_element_alignment = std::alignment_of<FIRST_TYPE>::value > RecursiveHelper<ELEMENT, OTHER_TYPES...>::s_element_alignment ?
                    std::alignment_of<FIRST_TYPE>::value : RecursiveHelper<ELEMENT, OTHER_TYPES...>::s_element_alignment;

                inline static void construct(ListBuilder & i_builder, FIRST_TYPE && i_source, OTHER_TYPES && ... i_other_arguments)
                    // noexcept( new (nullptr) FIRST_TYPE(std::forward<FIRST_TYPE>(std::declval<FIRST_TYPE>())) )
                {
                    void * new_element_complete = address_upper_align(i_builder.m_end_of_elements, std::alignment_of<FIRST_TYPE>::value);
                    ELEMENT * new_element = new (new_element_complete) FIRST_TYPE(std::forward<FIRST_TYPE>(i_source));
                    i_builder.m_end_of_elements = address_add(new_element, sizeof(FIRST_TYPE));
                    #if DENSITY_DEBUG_INTERNAL
                        i_builder.dbg_check_range(new_element, i_builder.m_end_of_elements);
                    #endif

                    i_builder.add_only_control_block(RUNTIME_TYPE::template make<FIRST_TYPE>(), new_element);

                    RecursiveHelper<ELEMENT, OTHER_TYPES...>::construct(i_builder, std::forward<OTHER_TYPES>(i_other_arguments)...);
                }
            };

            ControlBlock * m_control_blocks;
        };

    } // namespace detail

} // namespace density

