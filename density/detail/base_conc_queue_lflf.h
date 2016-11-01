
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#ifndef DENSITY_INCLUDING_CONC_QUEUE_DETAIL
	#error "THIS IS A PRIVATE HEADER OF DENSITY. DO NOT INCLUDE IT DIRECTLY"
#endif // ! DENSITY_INCLUDING_CONC_QUEUE_DETAIL

namespace density
{
	namespace detail
	{
		template < typename PAGE_ALLOCATOR, typename RUNTIME_TYPE>
			class base_concurrent_heterogeneous_queue<PAGE_ALLOCATOR, RUNTIME_TYPE, 
				SynchronizationKind::LocklessMultiple, SynchronizationKind::LocklessMultiple> : private PAGE_ALLOCATOR
		{
			using internal_word = uint64_t;

			static_assert(PAGE_ALLOCATOR::page_size == static_cast<internal_word>(PAGE_ALLOCATOR::page_size), 
				"internal_word can't represent the page size");

            using queue_header = ou_conc_queue_header<uint64_t, RUNTIME_TYPE, 
				static_cast<internal_word>(PAGE_ALLOCATOR::page_size),
				SynchronizationKind::LocklessMultiple, SynchronizationKind::LocklessMultiple>;

        public:

			static constexpr size_t default_alignment = queue_header::default_alignment;
			
			base_concurrent_heterogeneous_queue()
            {
                auto first_queue = new_page();
                m_first.store(first_queue);
                m_last.store(reinterpret_cast<uintptr_t>(first_queue));
                m_can_delete_page.clear();
            }

			template <typename NEW_ELEMENT_COMPLETE_TYPE>
				void push(NEW_ELEMENT_COMPLETE_TYPE && i_source_element)
			{
				auto const hazard_pointer = PAGE_ALLOCATOR::get_local_hazard().get();

				using ElementType = std::decay<NEW_ELEMENT_COMPLETE_TYPE>::type;

				for (;;)
				{
					/** loads the last page of the queue, and sets it as hazard page (no view thread will delete it). If
						the last page changes has the meanwhile, we have to repeat the operation. */
					queue_header * last;
					uintptr_t last_dirt;
					do {
						last_dirt = m_last.load();
						last = reinterpret_cast<queue_header*>(last_dirt & ~static_cast<uintptr_t>(1));
						hazard_pointer->store(last);
						DENSITY_TEST_RANDOM_WAIT();
						/*  If an ABA sequence occurs (m_last is changed to another value and then is changed back to its prev value), we
							don't care: all that we need if to check that what we store in the hazard pointer now is the last page. */								
					} while (last_dirt != m_last.load());
					DENSITY_TEST_RANDOM_WAIT();

					/** try to push in the page */
					{
						queue_header::Push push_op;
						
						bool res = push_op.begin<true>(*last, sizeof(ElementType), alignof(ElementType));
						
						*hazard_pointer = nullptr;

						if (res)
						{
							auto new_element_ptr = push_op.new_element_ptr();
							new (push_op.type_ptr()) RUNTIME_TYPE(RUNTIME_TYPE::make<ElementType>());
							new (new_element_ptr) ElementType(std::forward<NEW_ELEMENT_COMPLETE_TYPE>(i_source_element));
							push_op.commit();
							break;
						}
					}

					try_to_add_a_page();
				}				
			}

		
            template <typename CONSUMER_FUNC>
                bool try_consume(CONSUMER_FUNC && i_consumer_func)
            {
                auto const hazard_pointer = PAGE_ALLOCATOR::get_local_hazard().get();

				bool result;
				for (;;)
				{
					/** loads the first page of the queue, and sets it as hazard page (no view thread will delete it). If
					the first page changes has the meanwhile, we have to repeat the operation. */
					queue_header * first;
					do {
						first = m_first.load();
						hazard_pointer->store(first);
						DENSITY_TEST_RANDOM_WAIT();
					} while (first != m_first.load());
					DENSITY_TEST_RANDOM_WAIT();

					/* try to consume an element. On success, exit the loop */
					{
						queue_header::Consume consume_op;
						result = consume_op.begin<true>(*first);
						if (result)
						{
							i_consumer_func(*consume_op.type_ptr(), consume_op.element_ptr());
							consume_op.type_ptr()->destroy(consume_op.element_ptr());
							consume_op.type_ptr()->RUNTIME_TYPE::~RUNTIME_TYPE();
							consume_op.commit();
							break;
						}
					}

					DENSITY_TEST_RANDOM_WAIT();
					auto const last = reinterpret_cast<queue_header*>(m_last.load() & ~static_cast<uintptr_t>(1));
					DENSITY_TEST_RANDOM_WAIT();
					if (first == last || !first->is_empty())
					{
						/** The consume has failed, and we can't delete this page because it is not empty, or because it
						is the only page in the queue. */
						break;
					}

					/** Try to delete the page */
					DENSITY_TEST_RANDOM_WAIT();
					try_to_delete_first(*hazard_pointer);
				}

				/** Todo : exception safeness */
				hazard_pointer->store(nullptr);
                return result;
            }

/*
			template <typename CONSTRUCTOR>
				void push(const RUNTIME_TYPE & i_source_type, CONSTRUCTOR && i_constructor, size_t i_size)
			{
				get_impicit_view().push(i_source_type, std::forward<CONSTRUCTOR>(i_constructor), i_size);
			}

            template <typename OPERATION>
                bool try_consume(OPERATION && i_operation)
            {
				return get_impicit_view().try_consume(std::forward<OPERATION>(i_operation));
            }*/

            /** Returns a reference to the allocator instance owned by the queue.
                \n\b Throws: nothing
                \n\b Complexity: constant */
			PAGE_ALLOCATOR & get_allocator_ref() noexcept
            {
                return *static_cast<PAGE_ALLOCATOR*>(this);
            }

            /** Returns a const reference to the allocator instance owned by the queue.
                \n\b Throws: nothing
                \n\b Complexity: constant */
            const PAGE_ALLOCATOR & get_allocator_ref() const noexcept
            {
                return *static_cast<const PAGE_ALLOCATOR*>(this);
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

            queue_header * new_page()
            {
                return new(get_allocator_ref().allocate_page()) queue_header();
            }

            void delete_page(queue_header * i_page) noexcept
            {
                DENSITY_ASSERT_INTERNAL(i_page->is_empty());

                // the destructor of a page header is trivial, so just deallocate it
                get_allocator_ref().deallocate_page(i_page);
            }

            bool is_safe_to_free(queue_header* i_page)
            {
				return !m_hazard_context.is_hazard_pointer(i_page);
            }

			template <typename CONSUMER_FUNC>
				bool impl_try_consume(std::atomic<void*> & io_hazard_pointer, CONSUMER_FUNC && i_consumer_func)
			{
			}

			DENSITY_NO_INLINE void try_to_add_a_page()
			{
				/* Now we try to get exclusive access on m_last. If we fail to do that, just continue the loop. */
				DENSITY_TEST_RANDOM_WAIT();
				auto prev_last = m_last.fetch_or(1);
				if ((prev_last & 1) == 0)
				{
					/* Ok, we have set the exclusive flag from 0 to 1. First allocate the page. */
					DENSITY_TEST_RANDOM_WAIT();
					auto const queue = new_page();

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

			DENSITY_NO_INLINE void try_to_delete_first(std::atomic<void*> & io_hazard_pointer)
            {
				io_hazard_pointer.store(nullptr);

                if (!m_can_delete_page.test_and_set())
                {
                    auto const first = m_first.load();
					io_hazard_pointer.store(first);

                    auto const last = reinterpret_cast<queue_header*>(m_last.load() & ~static_cast<uintptr_t>(1));

                    if (first != last && first->is_empty())
                    {
                        /* This first page is not the last (so it can't have further elements pushed) */
                        auto const next = first->m_next;
                        DENSITY_ASSERT_INTERNAL(next != nullptr);
                        DENSITY_TEST_RANDOM_WAIT();
                        m_first.store(next);
                        m_can_delete_page.clear();

						io_hazard_pointer.store(nullptr);

                        while (!is_safe_to_free(first))
                        {
                            DENSITY_STATS(++g_stats.delete_page_waits);
                        }

                        delete_page(first);
                    }
                    else
                    {
						io_hazard_pointer.store(nullptr);
                        m_can_delete_page.clear();
                    }
                }
            }

        private:
            std::atomic<queue_header*> m_first; /**< Pointer to the first page of the queue. Consumer will try to consume from this page first. */
            std::atomic<uintptr_t> m_last; /**< Pointer to the first page of the queue, plus the exclusive-access flag (the least significant bit).
                                                A thread gets exclusive-access on this pointer if it succeeds in setting this bit to 1.
                                                The address of the page is obtained by masking away the first bit. Producer threads try to push
                                                new elements in this page first. */

            std::atomic_flag m_can_delete_page; /** Atomic flag used to synchronize view threads that attempt to delete the first page. */

			HazardPointersContext m_hazard_context;

			static HazardPointersContext s_global_hazard_context;
		};

		// definition of s_global_hazard_context
		template < typename PAGE_ALLOCATOR, typename RUNTIME_TYPE>
			HazardPointersContext base_concurrent_heterogeneous_queue<PAGE_ALLOCATOR, RUNTIME_TYPE,
				SynchronizationKind::LocklessMultiple, SynchronizationKind::LocklessMultiple>::s_global_hazard_context;
						
	} // namespace detail

} // namespace density