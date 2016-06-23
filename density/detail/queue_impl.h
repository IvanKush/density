
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include "..\density_common.h"
#include "..\runtime_type.h"

namespace density
{
    namespace detail
    {
		enum class QueueControlBlockKind
		{
			ElementNextPointers,
			ElementNextOffsets
		};

		template <typename RUNTIME_TYPE, QueueControlBlockKind KIND>
			class QueueControlBlock;

		template <typename RUNTIME_TYPE>
			class QueueControlBlock<RUNTIME_TYPE, QueueControlBlockKind::ElementNextPointers> : public RUNTIME_TYPE
		{
		public:
			QueueControlBlock(const RUNTIME_TYPE & i_type, void * i_element, QueueControlBlock * i_next) noexcept
				: RUNTIME_TYPE(i_type), m_element(i_element), m_next(i_next) { }

			void * element() const noexcept { return m_element; }
			QueueControlBlock * next() const noexcept { return m_next; }

		private:
			void * const m_element;
			QueueControlBlock * const m_next;
		};

		template <typename RUNTIME_TYPE>
			class QueueControlBlock<RUNTIME_TYPE, QueueControlBlockKind::ElementNextOffsets> : public RUNTIME_TYPE
		{
		public:
			QueueControlBlock(const RUNTIME_TYPE & i_type, void * i_element, QueueControlBlock * i_next) noexcept
				: RUNTIME_TYPE(i_type)
			{
				const auto element_offset = reinterpret_cast<intptr_t>(i_element) - reinterpret_cast<intptr_t>(this);
				const auto next_offset = reinterpret_cast<intptr_t>(i_next) - reinterpret_cast<intptr_t>(this);
				m_element_offset = static_cast<int32_t>(element_offset);
				m_next_offset = static_cast<int32_t>(next_offset);
			}

			void * element() const noexcept { return reinterpret_cast<void*>(
				reinterpret_cast<intptr_t>(this) + m_element_offset ); }
				
			QueueControlBlock * next() const noexcept { return reinterpret_cast<QueueControlBlock *>(
				reinterpret_cast<intptr_t>(this) + m_next_offset); }

		private:
			int32_t m_element_offset;
			int32_t m_next_offset;
		};

        /** This internal class template implements an heterogeneous FIFO container that allocates the elements on an externally-owned
           memory buffer. QueueImpl is movable but not copyable.
           A null-QueueImpl is a QueueImpl with no associated memory buffer. A default constructed QueueImpl is a null-QueueImpl. The
           source of a move-constructor or move-assignment becomes a null-QueueImpl. The only way for null-QueueImpl to become a non-
           null-QueueImpl is being the destination of a move-assignment with a non-null source. A null-QueueImpl is always empty, and a
           try_push on it results in undefined behavior.
           Implementation: the layout of the buffer is composed by a linear-allocated sequence of ControlBlock-element pairs. This sequence
           wraps to the boundaries of the memory buffer. ControlBlock is a private struct that contains:
              - the RUNTIME_TYPE associated to the element. If RUNTIME_TYPE is an empty struct, no storage is wasted (ControlBlock exploits
                the empty base optimization).
              - a pointer to the element. This pointer does always not point to the end of the ControlBlock, as:
                    * the storage of each element is aligned according to its type
                    * this pointer may wrap to the beginning of the buffer, when there is not enough space in the buffer after the ControlBlock.
                    * this pointer may point to a sub-object of the element, in case of typed containers (that if the public container has
                      a non-void type). Note: the address of a sub-object (the base class "part") is not equal to the address of the
                      complete type (that is, a static-casting a pointer is not a no-operation).
              - a pointer to the ControlBlock of the next element. The content of the pointed memory is undefined if this
                element is the last one. Usually this points to the end of the element, upper-aligned according to the alignment requirement
                of ControlBlock. This pointer may wrap to the beginning of the memory buffer. */
        template <typename RUNTIME_TYPE>
            class QueueImpl final
        {
            // this causes RuntimeTypeConceptCheck<RUNTIME_TYPE> to be specialized, and eventually compilation to fail
            static_assert(sizeof(RuntimeTypeConceptCheck<RUNTIME_TYPE>)>0, "");

			#if DENSITY_COMPATCT_QUEUE
				using ControlBlock = QueueControlBlock<RUNTIME_TYPE, QueueControlBlockKind::ElementNextOffsets>;
			#else
				using ControlBlock = QueueControlBlock<RUNTIME_TYPE, QueueControlBlockKind::ElementNextPointers>;
			#endif

        public:

            /** Minimum size of a memory buffer. This requirement avoids the need of handling the special case of very small buffers. */
            static const size_t s_minimum_buffer_size = sizeof(ControlBlock) * 4;

            /** Minimum alignment of a memory buffer */
            static const size_t s_minimum_buffer_alignment = alignof(ControlBlock);

            /* Iterator class, similar to an stl iterator */
            struct IteratorImpl
            {
                /* Construct a IteratorImpl with undefined content. */
                IteratorImpl() noexcept {}

                IteratorImpl(ControlBlock * i_curr_control) noexcept
                    : m_curr_control(i_curr_control) { }

                void operator ++ () noexcept
                {
                    m_curr_control = m_curr_control->next();
                }

                bool operator == (const IteratorImpl & i_source) const noexcept
                {
                    return m_curr_control == i_source.m_curr_control;
                }

                bool operator != (const IteratorImpl & i_source) const noexcept
                {
                    return m_curr_control != i_source.m_curr_control;
                }

                void * element() const noexcept
                {
                    return m_curr_control->element();
                }

                const RUNTIME_TYPE & complete_type() const noexcept
                {
                    return *m_curr_control;
                }

                ControlBlock * control() const noexcept
                {
                    return m_curr_control;
                }

            private:
                ControlBlock * m_curr_control;
            };

            /** Construct a null-QueueImpl. */
            QueueImpl() noexcept
                : m_head(nullptr), m_tail(nullptr), m_element_max_alignment(alignof(ControlBlock)),
                  m_buffer_start(nullptr), m_buffer_end(nullptr)
            {
            }

            /** Construct a QueueImpl providing a memory buffer.
                Preconditions:
                 * i_buffer_address can't be null
                 * the whole memory buffer must be readable and writable.
                 * i_buffer_byte_capacity must be >= s_minimum_buffer_size
                 * i_buffer_alignment must be >= s_minimum_buffer_alignment */
            QueueImpl(void * i_buffer_address, size_t i_buffer_byte_capacity, size_t i_buffer_alignment = s_minimum_buffer_alignment) noexcept
            {
                DENSITY_ASSERT(i_buffer_address != nullptr &&
                    i_buffer_byte_capacity >= s_minimum_buffer_size &&
                    i_buffer_alignment >= s_minimum_buffer_alignment); // preconditions

                m_buffer_start = i_buffer_address;
                m_buffer_end = address_add(m_buffer_start, i_buffer_byte_capacity);
                m_head = static_cast<ControlBlock*>( address_upper_align(m_buffer_start, i_buffer_alignment) );
                m_tail = m_head;
                m_element_max_alignment = i_buffer_alignment;

                DENSITY_ASSERT(m_head + 1 <= m_buffer_end); // postcondition
            }

            /** Move construct a QueueImpl. The source becomes a null-QueueImpl. */
            QueueImpl(QueueImpl && i_source) noexcept
                : m_head(i_source.m_head), m_tail(i_source.m_tail), m_element_max_alignment(i_source.m_element_max_alignment),
                  m_buffer_start(i_source.m_buffer_start), m_buffer_end(i_source.m_buffer_end)
            {
                i_source.m_tail = i_source.m_head = nullptr;
                i_source.m_buffer_end = i_source.m_buffer_start = nullptr;
                i_source.m_element_max_alignment = alignof(ControlBlock);
            }

            /** Move assigns a QueueImpl. The source becomes a null-QueueImpl. */
            QueueImpl & operator = (QueueImpl && i_source) noexcept
            {
                m_head = i_source.m_head;
                m_tail = i_source.m_tail;
                m_element_max_alignment = i_source.m_element_max_alignment;
                m_buffer_start = i_source.m_buffer_start;
                m_buffer_end = i_source.m_buffer_end;

                i_source.m_tail = i_source.m_head = nullptr;
                i_source.m_buffer_end = i_source.m_buffer_start = nullptr;
                i_source.m_element_max_alignment = alignof(ControlBlock);

                return *this;
            }

            QueueImpl(const QueueImpl & i_source) = delete;
            QueueImpl & operator = (const QueueImpl & i_source) = delete;

            /** Moves the elements from i_source to this queue, move constructing them to this QueueImpl, and destroying
                them from the source. After the call, i_source will be empty.
                This queue must have enough space to allocate all the elements of i_source,
                otherwise the behavior is undefined. If you assign to this QueueImpl a memory buffer with the same size
                of the source, but with (at least) the aligned to i_source.element_max_alignment(), the space will always
                be enough.
                Precondition: this queue must be empty, and must have enough space to contain all the element of the source.
                This function never throws. */
            void move_elements_from(QueueImpl & i_source) noexcept
            {
                DENSITY_ASSERT(empty());
                auto it = i_source.begin();
                const auto end_it = i_source.end();
                while( it != end_it )
                {
                    auto const control = it.control();
                    auto const source_element = it.element(); // get_complete_type( it.control() );
                    ++it;

                    // to do: a slightly optimized nofail_push may be used here
                    bool result = try_push(*control,
                        typename detail::QueueImpl<RUNTIME_TYPE>::move_construct(source_element));
                    DENSITY_ASSERT(result);
                    (void)result;

                    control->destroy( static_cast<typename RUNTIME_TYPE::base_type*>(source_element) );
                    control->ControlBlock::~ControlBlock();
                }
                // set the source as empty
                i_source.m_tail = i_source.m_head = static_cast<ControlBlock*>(address_upper_align(i_source.m_buffer_start, alignof(ControlBlock)));
                i_source.m_element_max_alignment = alignof(ControlBlock);
            }

            /** Copies the elements from i_source to this queue. This queue must have enough space to
                allocate space for all the elements of i_source, otherwise the behavior is undefined.
                If you assign to this QueueImpl a memory buffer with the same size of the source, but
                with (at least) the aligned to i_source.element_max_alignment(), the space will always be enough.
                Precondition: this queue must be empty, and must have enough space to contain all the element of the source.
                Can throw: anything that the copy constructor of the elements can throw
                Exception guarantee: strong (if an exception is raised during this function, this object is left unchanged). */
            void copy_elements_from(const QueueImpl & i_source)
            {
                DENSITY_ASSERT(empty());
                try
                {
                    IteratorImpl it = i_source.begin();
                    const IteratorImpl end_it = i_source.end();
                    while (it != end_it)
                    {
                        auto const & type = it.complete_type();
                        auto const source_element = it.element(); // get_complete_type(it.control());
                        ++it;

                        // to do: a slightly optimized nofail_push may be used here
                        bool result = try_push(type,
                            typename detail::QueueImpl<RUNTIME_TYPE>::copy_construct(source_element));
                        DENSITY_ASSERT(result);
                        (void)result;
                    }
                }
                catch(...)
                {
                    delete_all(); // <- this call is noexcept
                    throw;
                }
            }

            /** Returns whether the queue is empty. Same to begin()==end() */
            bool empty() const noexcept
            {
                return m_head == m_tail;
            }

            IteratorImpl begin() const noexcept
            {
                return IteratorImpl(m_head);
            }

            IteratorImpl end() const noexcept
            {
                return IteratorImpl(m_tail);
            }

            struct copy_construct
            {
                const void * const m_source;

                copy_construct(const void * i_source) noexcept
                    : m_source(i_source) { }

                void * operator () (const RUNTIME_TYPE & i_element_type, void * i_dest)
                {
                    return i_element_type.copy_construct(i_dest, static_cast<const typename RUNTIME_TYPE::base_type*>(m_source) );
                }
            };

            struct move_construct
            {
                void * const m_source;

                move_construct(void * i_source) noexcept
                    : m_source(i_source) { }

                void * operator () (const RUNTIME_TYPE & i_element_type, void * i_dest) noexcept
                {
                    return i_element_type.move_construct(i_dest, static_cast<typename RUNTIME_TYPE::base_type*>(m_source));
                }
            };

            /** Tries to insert a new element in the queue. If this is a null-QueueImpl, the behavior is undefined.
                @param i_source_type type of the element to insert
                @param i_constructor function object to which the construction of the new element is delegated. The
                    expected signature of the function is:
                        void * (const RUNTIME_TYPE & i_source_type, void * i_new_element_place)
                    The return value is a pointer to the constructed element. QueueImpl is not aware of the value-type
                    of the overlying container, but i_constructor may be, and may return a pointer to a sub-object of
                    the complete object. If the value-type is void or a standard-layout type, i_constructor should return
                    i_new_element_place.
                @return true if the element was successfully inserted, false in case of insufficient space.
                Preconditions: this is not a null-QueueImpl. */
            template <typename CONSTRUCTOR>
                bool try_push(const RUNTIME_TYPE & i_source_type, CONSTRUCTOR && i_constructor)
            {
                DENSITY_ASSERT(m_buffer_start != nullptr);
                DENSITY_ASSERT(m_tail + 1 <= m_buffer_end);
                DENSITY_ASSERT(i_source_type.alignment() > 0 && is_power_of_2(i_source_type.alignment()));

                const auto element_size = i_source_type.size();
                const auto element_aligment = i_source_type.alignment();
                const auto element_aligment_mask = element_aligment - 1;
                ControlBlock * const curr_control = m_tail;

                /* first try to allocate the element and the next control doing only bound and overflow
                    checking once. Note: both size and alignment of elements are constrained to be <=
                    std::numeric_limits < std::numeric_limits<uintptr_t>::max() / 4. */
                uintptr_t original_tail = reinterpret_cast<uintptr_t>(curr_control + 1);
                uintptr_t new_tail = original_tail;

                // allocate element
                new_tail += element_aligment_mask;
                new_tail &= ~element_aligment_mask;
                void * element = reinterpret_cast<void *>(new_tail);
                new_tail += element_size;

                // allocate next control
                new_tail += (std::alignment_of<ControlBlock>::value - 1);
                new_tail &= ~(std::alignment_of<ControlBlock>::value - 1);
                void * next_control = reinterpret_cast<void *>(new_tail);
                new_tail += sizeof(ControlBlock);

                const auto u_buffer_end = reinterpret_cast<uintptr_t>(m_buffer_end);
                const auto u_head = reinterpret_cast<uintptr_t>(m_head);
                uintptr_t upper_limit = u_buffer_end;
                if (m_head >= m_tail)
                {
                    upper_limit = u_head;
                }
                if (new_tail >= upper_limit || new_tail < original_tail)
                {
                    // handle: wrapping to the end of the buffer, allocation failure, pointer arithmetic overflow
                    void * tail = curr_control + 1;
                    const auto & res = double_push(tail, element_size, element_aligment);
                    element = res.m_element;
                    next_control = res.m_next_control;
                    if (element == nullptr || next_control == nullptr)
                    {
                        return false;
                    }
                }

                #if DENSITY_DEBUG_INTERNAL
                    void * dbg_new_tail = curr_control + 1;
                    void * dbg_element = single_push(dbg_new_tail, i_source_type.size(), element_aligment);
                    void * dbg_next_control = single_push(dbg_new_tail, sizeof(ControlBlock), alignof(ControlBlock) );
                    DENSITY_ASSERT_INTERNAL(dbg_element == element && dbg_next_control == next_control);
                #endif

                void * new_element = i_constructor(i_source_type, element);

                // from now on, no exception can be raised
                new(curr_control) ControlBlock(i_source_type, new_element, static_cast<ControlBlock*>(next_control));
                m_tail = static_cast<ControlBlock *>(next_control);
                m_element_max_alignment = size_max(m_element_max_alignment, element_aligment);
                return true;
            }

            /** Calls the specified function object on the first element (the oldest one), and then
                removes it from the queue without calling its destructor.
                @param i_operation function object with a signature compatible with:
                \code
                void (const RUNTIME_TYPE & i_complete_type, void * i_element_base_ptr)
                \endcode
                \n to be called for the first element. This function object is responsible of synchronously
                destroying the element.

                \pre The queue must be non-empty (otherwise the behavior is undefined).
            */
            template <typename OPERATION>
                auto manual_consume(OPERATION && i_operation)
                    noexcept(noexcept((i_operation(std::declval<RUNTIME_TYPE>(), std::declval<void*>()))))
                        -> decltype(i_operation(std::declval<RUNTIME_TYPE>(), std::declval<void*>()))
            {
                using ReturnType = decltype(i_operation(std::declval<RUNTIME_TYPE>(), std::declval<void*>()));
                return manual_consume(std::forward<OPERATION>(i_operation), std::is_same<ReturnType, void>());
            }

            /** Deletes the first element of the queue (the oldest one).
                \pre The queue must be non-empty (otherwise the behavior is undefined).
                \n\b Throws: nothing
                \n\b Complexity: constant */
            void pop() noexcept
            {
                DENSITY_ASSERT(!empty()); // the queue must not be empty

                ControlBlock * first_control = m_head;
                void * const element_ptr = first_control->element();
                m_head = first_control->next();
                first_control->destroy( static_cast<typename RUNTIME_TYPE::base_type*>(element_ptr) );
                first_control->ControlBlock::~ControlBlock();
            }

            /** Returns a pointer to the beginning of the memory buffer. Note: this is not like a data() method, as
                the data does not start here (it starts where m_head points to). */
            void * buffer() noexcept { return m_buffer_start; }

            /** Returns the size of the memory buffer assigned to the queue */
            size_t mem_capacity() const noexcept
            {
                return address_diff(m_buffer_end, m_buffer_start);
            }

            /** Returns how much of the memory buffer is used. */
            size_t mem_size() const noexcept
            {
                if (m_head <= m_tail)
                {
                    return address_diff(m_tail, m_head);
                }
                else
                {
                    return address_diff(m_buffer_end, m_head) + address_diff(m_tail, m_buffer_start);
                }
            }

            /** Deletes all the element from the queue. After this call the memory buffer is still
                associated to the queue, but it is empty. */
            void delete_all() noexcept
            {
                IteratorImpl const it_end = end();
                for (IteratorImpl it = begin(); it != it_end; )
                {
                    auto const control = it.control();
                    auto const element = it.element(); // get_complete_type(it.control());
                    ++it;

                    control->destroy( static_cast<typename RUNTIME_TYPE::base_type*>(element) );
                    control->ControlBlock::~ControlBlock();
                }
                // restart from m_buffer_start, with an empty queue
                m_tail = m_head = static_cast<ControlBlock*>(address_upper_align(m_buffer_start, alignof(ControlBlock)));
            }

            size_t element_max_alignment() const noexcept { return m_element_max_alignment; }

        private:

            template <typename OPERATION>
                auto manual_consume(OPERATION && i_operation, std::false_type)
                   noexcept(noexcept((i_operation(std::declval<RUNTIME_TYPE>(), std::declval<void*>()))))
                    -> decltype(i_operation(std::declval<RUNTIME_TYPE>(), std::declval<void*>()))
            {
                DENSITY_ASSERT(!empty()); // the queue must not be empty

                ControlBlock * first_control = m_head;
                void * const element_ptr = first_control->element();
                auto && result = i_operation(static_cast<const RUNTIME_TYPE &>(*first_control), element_ptr);
                m_head = first_control->next();
                first_control->ControlBlock::~ControlBlock();
                return result;
            }

            template <typename OPERATION>
                void manual_consume(OPERATION && i_operation, std::true_type)
                   noexcept(noexcept((i_operation(std::declval<RUNTIME_TYPE>(), std::declval<void*>()))))
            {
                DENSITY_ASSERT(!empty()); // the queue must not be empty

                ControlBlock * first_control = m_head;
                void * const element_ptr = first_control->element();
                i_operation(static_cast<const RUNTIME_TYPE &>(*first_control), element_ptr);
                m_head = first_control->next();
                first_control->ControlBlock::~ControlBlock();
            }

            /* Allocates an object on the queue. The return value is the address of the new object.
               This function is used to push the ControlBlock and the element. If the required size with the
               required alignment does not fit in the queue the return value is nullptr.
               Preconditions: *io_tail can't be null, or the behavior is undefined. */
            DENSITY_STRONG_INLINE void * single_push(void * & io_tail, size_t i_size, size_t i_alignment) const noexcept
            {
                DENSITY_ASSERT(io_tail != nullptr);

                auto const prev_tail = io_tail;

                auto start_of_block = linear_alloc(io_tail, i_size, i_alignment);
                if (io_tail > m_buffer_end)
                {
                    // wrap to the start...
                    io_tail = m_buffer_start;
                    start_of_block = linear_alloc(io_tail, i_size, i_alignment);
                    if (io_tail >= m_head)
                    {
                        // ...not enough space before the head, failed!
                        start_of_block = nullptr;
                    }
                }
                else if ((prev_tail >= m_head) != (io_tail >= m_head))
                {
                    // ...crossed the head, failed!
                    start_of_block = nullptr;
                }
                return start_of_block;
            }

            /* Allocates two objects on the queue. The return value is the address of the new object. */
            struct DoublePushResult { void * m_element, *m_next_control; };
            DENSITY_NO_INLINE DoublePushResult double_push(void * & io_tail, size_t i_element_size, size_t i_element_alignment ) const noexcept
            {
                return { single_push(io_tail, i_element_size, i_element_alignment),
                    single_push(io_tail, sizeof(ControlBlock), alignof(ControlBlock)) };
            }


        private: // data members
            ControlBlock * m_head; /**< points to the first ControlBlock. If m_tail==m_head the queue is empty, otherwise this member points
                                    to a valid ControlBlock. */
            ControlBlock * m_tail; /**< end marker of the sequence. If another element is successfully added to the sequence, m_tail will
                                    be the address of the associated ControlBlock object. */
            size_t m_element_max_alignment; /**< The maximum between alignof(ControlBlock) and the maximum alignment of the alignment of the
                                            elements in the container. This field has a specific getter (element_max_alignment), and it
                                            is required to allocate a memory buffer big enough to contain all the elements. */
            void * m_buffer_start, * m_buffer_end; /**< range of the memory buffer */
        }; // class template QueueImpl

    } // namespace detail

} // namespace density
