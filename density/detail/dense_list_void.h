
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

namespace density
{
    /** A dense-list is a polymorphic sequence container optimized to be compact in both heap memory and inline storage.
        Elements is a DenseList are allocated respecting their alignment requirements.
        In a polymorphic container every element can have a different complete type, provided that this type is covariant to the type ELEMENT.
        All the elements of a DenseList are arranged in the same memory block of the heap.
        Insertions\removals of a non-zero number of elements and clear() always reallocate the memory blocks and invalidate existing iterators.
        The inline storage of DenseList is the same of a pointer. An empty DenseList does not use heap memory.
        All the functions of DenseList gives at least the strong exception guarantee. */
    template <typename ALLOCATOR, typename RUNTIME_TYPE>
        class DenseList<void, ALLOCATOR, RUNTIME_TYPE> final
    {
    private:
        using ListImpl = detail::DenseListImpl<ALLOCATOR, RUNTIME_TYPE>;
        using IteratorImpl = typename ListImpl::IteratorBaseImpl;

    public:

        using allocator_type = ALLOCATOR;
        using difference_type = ptrdiff_t;
        using size_type = size_t;
        using value_type = void;
        using pointer = typename std::allocator_traits<allocator_type>::pointer;
        using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;

        /** Alias for the template parameters */
        using runtime_type = RUNTIME_TYPE;

        /** Creates a DenseList containing all the elements specified in the parameter list.
            For each object of the parameter pack, an element is added to the list by copy-construction or move-construction.
                @param i_args elements to add to the list.
                @return the new DenseList
            Example:
                const auto list = DenseList<int>::make(1, 2, 3);
                const auto list1 = DenseList<ListImpl>::make(Derived1(), Derived2(), Derived1()); */
        template <typename... TYPES>
            inline static DenseList make(TYPES &&... i_args)
        {
            DenseList new_list;
            ListImpl::make_impl(new_list.m_impl, std::forward<TYPES>(i_args)...);
            return std::move(new_list);
        }

        /** Creates a DenseList containing all the elements specified in the parameter list. The allocator of the new DenseList is copy-constructed from the provided one.
            For each object of the parameter pack, an element is added to the list by copy-construction or move-construction.
                @param i_args elements to add to the list.
                @return the new DenseList
            Example:
                MyAlloc<int> my_alloc;
                MyAlloc<ListImpl> my_alloc1;
                const auto list = DenseList<int>::make_with_alloc(my_alloc, 1, 2, 3);
                const auto list1 = DenseList<ListImpl>::make_with_alloc(my_alloc1, Derived1(), Derived2(), Derived1()); */
        template <typename... TYPES>
            inline static DenseList make_with_alloc(const ALLOCATOR & i_allocator, TYPES &&... i_args)
        {
            DenseList new_list;
            ListImpl::make_impl(new_list.m_impl, std::forward<TYPES>(i_args)...);
            return std::move(new_list);
        }

        size_t size() const DENSITY_NOEXCEPT
        {
            return m_impl.size();
        }

        bool empty() const DENSITY_NOEXCEPT
        {
            return m_impl.empty();
        }

        class iterator;
        class const_iterator;

        class iterator final : private IteratorImpl
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = ptrdiff_t;
            using size_type = size_t;
            using value_type = void;
            using pointer = typename std::allocator_traits<allocator_type>::pointer;
            using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;

            iterator(const IteratorImpl & i_source) DENSITY_NOEXCEPT
                : m_impl(i_source) {  }

            value_type * element() const DENSITY_NOEXCEPT
                { return static_cast<value_type *>(m_impl.element()); }

            iterator & operator ++ () DENSITY_NOEXCEPT
            {
                m_impl.move_next();
                return *this;
            }

            iterator operator++ (int) DENSITY_NOEXCEPT
            {
                iterator copy(*this);
                m_impl.move_next();
                return copy;
            }

            bool operator == (const iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_impl.m_curr_type == i_other.m_impl.m_curr_type;
            }

            bool operator != (const iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_impl.m_curr_type != i_other.m_impl.m_curr_type;
            }

            bool operator == (const const_iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_impl.m_curr_type == i_other.m_impl.m_curr_type;
            }

            bool operator != (const const_iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_impl.m_curr_type != i_other.m_impl.m_curr_type;
            }

            const RUNTIME_TYPE * curr_type() const DENSITY_NOEXCEPT { return m_impl.m_curr_type; }

        private:
            friend class const_iterator;
            IteratorImpl m_impl;
        }; // class iterator

        class const_iterator final
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type = ptrdiff_t;
            using size_type = size_t;
            using value_type = const void;
            using pointer = typename std::allocator_traits<allocator_type>::pointer;
            using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;

            const_iterator(const IteratorImpl & i_source) DENSITY_NOEXCEPT
                : IteratorImpl(i_source) {  }

            const_iterator(const iterator & i_source) DENSITY_NOEXCEPT
                : IteratorImpl(i_source) {  }

            value_type * element() const DENSITY_NOEXCEPT
                { return static_cast<value_type *>(m_impl.element()); }

            const_iterator & operator ++ () DENSITY_NOEXCEPT
            {
                m_impl.move_next();
                return *this;
            }

            const_iterator operator++ (int) DENSITY_NOEXCEPT
            {
                iterator copy(*this);
                m_impl.move_next();
                return copy;
            }

            bool operator == (const iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_curr_type == i_other.m_impl.m_curr_type;
            }

            bool operator != (const iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_curr_type != i_other.m_impl.m_curr_type;
            }

            bool operator == (const const_iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_impl.m_curr_type == i_other.m_impl.m_curr_type;
            }

            bool operator != (const const_iterator & i_other) const DENSITY_NOEXCEPT
            {
                return m_impl.m_curr_type != i_other.m_impl.m_curr_type;
            }

            const RUNTIME_TYPE * curr_type() const DENSITY_NOEXCEPT { return m_impl.m_curr_type; }

        private:
            friend class DenseList;
            IteratorImpl m_impl;
        }; // class const_iterator

        iterator begin() DENSITY_NOEXCEPT { return iterator(m_impl.begin()); }
        iterator end() DENSITY_NOEXCEPT { return iterator(m_impl.end()); }

        const_iterator begin() const DENSITY_NOEXCEPT { return const_iterator(m_impl.begin()); }
        const_iterator end() const DENSITY_NOEXCEPT{ return const_iterator(m_impl.end()); }

        const_iterator cbegin() const DENSITY_NOEXCEPT { return const_iterator(m_impl.begin()); }
        const_iterator cend() const DENSITY_NOEXCEPT { return const_iterator(m_impl.end()); }

        struct CopyConstruct
        {
            const void * const m_source;

            CopyConstruct(const void * i_source)
                : m_source(i_source) { }

            void * operator () (typename ListImpl::ListBuilder & i_builder, const runtime_type & i_element_type)
            {
                return i_builder.add_by_copy(i_element_type, m_source);
            }
        };

        struct MoveConstruct
        {
            void * const m_source;

            MoveConstruct(void * i_source)
                : m_source(i_source) { }

            void * operator () (typename ListImpl::ListBuilder & i_builder, const runtime_type & i_element_type) DENSITY_NOEXCEPT
            {
                return i_builder.add_by_move(i_element_type, m_source);
            }
        };

        template <typename ELEMENT_COMPLETE_TYPE>
            void push_back(const ELEMENT_COMPLETE_TYPE & i_source)
        {
            m_impl.insert_impl(m_impl.m_types + m_impl.size(),
                runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                CopyConstruct(&i_source) );
        }

        template <typename ELEMENT_COMPLETE_TYPE>
            void push_front(const ELEMENT_COMPLETE_TYPE & i_source)
        {
            m_impl.insert_impl(m_impl.m_types,
                runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                CopyConstruct(&i_source) );
        }

        template <typename ELEMENT_COMPLETE_TYPE>
            void push_back(ELEMENT_COMPLETE_TYPE && i_source)
        {
            m_impl.insert_impl(m_impl.m_types + m_impl.size(),
                runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                MoveConstruct(&i_source) );
        }

        template <typename ELEMENT_COMPLETE_TYPE>
            void push_front(ELEMENT_COMPLETE_TYPE && i_source)
        {
            m_impl.insert_impl(m_impl.m_types,
                runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                MoveConstruct(&i_source) );
        }

        void pop_front()
        {
            auto const types = m_impl.m_types;
            m_impl.erase_impl(types, types + 1);
        }

        void pop_back()
        {
            auto const end_type = m_impl.m_types +
                m_impl.get_size_not_empty();
            m_impl.erase_impl(end_type - 1, end_type);
        }

        template <typename ELEMENT_COMPLETE_TYPE>
            iterator insert(const_iterator i_position, const ELEMENT_COMPLETE_TYPE & i_source)
        {
            return m_impl.insert_impl(i_position.m_curr_type,
                runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                CopyConstruct(&i_source) );
        }

        template <typename ELEMENT_COMPLETE_TYPE>
            iterator insert(const_iterator i_position, size_t i_count, const ELEMENT_COMPLETE_TYPE & i_source)
        {
            if (i_count > 0)
            {
                return m_impl.insert_n_impl(i_position.m_curr_type, i_count,
                    runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                    CopyConstruct(&i_source) );
            }
            else
            {
                // inserting 0 elements
                return iterator(i_position);
            }
        }

        /* Temporary disabled
        template <typename ELEMENT_COMPLETE_TYPE, typename = typename std::enable_if<  >::value, void >
            iterator insert(const_iterator i_position, ELEMENT_COMPLETE_TYPE && i_source)
        {
            return ListImpl::insert_impl(i_position.m_curr_type,
                runtime_type::template make<ELEMENT_COMPLETE_TYPE>(),
                MoveConstruct(&i_source) );
        }*/

        iterator erase(const_iterator i_position)
        {
            return m_impl.erase_impl(i_position.m_curr_type, i_position.m_curr_type + 1);
        }

        iterator erase(const_iterator i_from, const_iterator i_to)
        {
            auto from_type = i_from.m_impl.m_curr_type;
            auto to_type = i_to.m_impl.m_curr_type;
            if (from_type != to_type)
            {
                return m_impl.erase_impl(from_type, to_type);
            }
            else
            {
                // removing 0 elements
                return iterator(i_from);
            }
        }

        void swap(DenseList & i_other) DENSITY_NOEXCEPT
        {
            std::swap(m_impl.m_types, i_other.m_types);
        }

                    /////////////////////////

        /* to do, & WARNING!: this function is slicing-comparing. Fix or delete. */
        bool equal_to(const DenseList & i_source) const
        {
            if (m_impl.size() != i_source.size())
            {
                return false;
            }
            else
            {
                const auto end_1 = cend();
                for (auto it_1 = cbegin(), it_2 = i_source.cbegin(); it_1 != end_1; ++it_1, ++it_2)
                {
                    if (*it_1 != *it_2)
                    {
                        return false;
                    }
                }
                return true;
            }
        }

        bool operator == (const DenseList & i_source) const { return equal_to(i_source); }
        bool operator != (const DenseList & i_source) const { return !equal_to(i_source); }

    private:
        detail::DenseListImpl<ALLOCATOR, RUNTIME_TYPE> m_impl;
    }; // class DenseList<void>;

} // namespace density
