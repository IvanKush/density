
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <cstdlib> // for std::max_align_t
#include "density_common.h"
#include "detail\hazard_pointers.h"
#if DENSITY_DEBUG_INTERNAL && DENSITY_ENV_HAS_THREADING
    #include <mutex>
    #include <unordered_set>
    #include <unordered_map>
#endif

namespace density
{
    /** This class encapsulates a memory page allocation service, modeling the \ref PagedAllocator_concept "PagedAllocator" concepts.

        page_allocator is stateless. Any instance of page_allocator compares equal to any instance of page_allocator. This implies that
        blocks and pages can be deallocated by any instance of page_allocator.

        \section Implementation
        page_allocator redirects block allocations to the language built-in operator new. Whenever the requested alignment
        is greater than alignof(std::max_align_t), page_allocator allocates an overhead whose size is the maximum between
        the requested alignment and sizeof(void*). \n

        Every thread is associated to a free-page cache. When a page is deallocated, if the cache has less than 4 pages,
        the page to deallocate is pushed in this cache. When a page allocation is requested, a page from the cache is returned (if any).
        Pushing/peeking to\from the cache is very fast, and does not require thread synchronization. \n
        Note: on win32 page_allocator is not redirecting page allocations to VirtualAlloc, since from several tests the latter resulted
        around ten times slower than the operator new.
    */
    class page_allocator
    {
    public:

		/** Size (in bytes) of a memory page. */
		static constexpr size_t page_size = 4096;

		/** Alignment (in bytes) of a memory page. */
		static constexpr size_t page_alignment = alignof(std::max_align_t);

		/** Maximum number of free page that a thread can cache */
		static const size_t free_page_cache_size = 4;

        page_allocator() noexcept = default;
        page_allocator(const page_allocator&) noexcept = default;
        page_allocator(page_allocator&&) noexcept = default;
        page_allocator & operator = (const page_allocator&) noexcept = default;
        page_allocator & operator = (page_allocator&&) noexcept = default;

        /** Allocates a memory page.
            @return address of the new memory page

            \exception std::bad_alloc if the allocation fails

            All the pages have the same size and alignment requirement (see page_size and page_alignment).
            The content of the newly allocated page is undefined. */
        void * allocate_page()
        {
            auto page = thread_page_store().peek();
            if (page == nullptr)
            {
                page = allocate_page_impl();
            }
            #if DENSITY_DEBUG_INTERNAL && DENSITY_ENV_HAS_THREADING
                dbg_data().add_page(page);
            #endif
            return page;
        }

        /** Deallocates a memory page. After the call accessing the page results in undefined behavior.
            @param i_page page to deallocate. Cannot be nullptr.

            \pre i_page is a memory page allocated with any instance of page_allocator

            \exception never throws */
        void deallocate_page(void * i_page) noexcept
        {
            #if DENSITY_DEBUG_INTERNAL && DENSITY_ENV_HAS_THREADING
                dbg_data().remove_page(i_page);
            #endif
            auto & page_store = thread_page_store();
            if (page_store.size() < free_page_cache_size)
            {
                page_store.push(i_page);
            }
            else
            {
                deallocate_page_impl(i_page);
            }
        }

        /** Returns whether the right-side allocator can be used to deallocate block and pages allocated by this allocator.
            @return always true. */
        bool operator == (const page_allocator &) const noexcept
            { return true; }

        /** Returns whether the right-side allocator cannot be used to deallocate block and pages allocated by this allocator.
            @return always false. */
        bool operator != (const page_allocator &) const noexcept
            { return false; }

		class hazard_pointer
		{
		public:

			hazard_pointer()
			{
				get_hazard_context().register_stack(m_pointer);
			}

			~hazard_pointer()
			{
				get_hazard_context().unregister_stack(m_pointer);
			}

			std::atomic<void*> * get() { return m_pointer.get(); }

			hazard_pointer(const hazard_pointer &) = delete;
			hazard_pointer & operator = (const hazard_pointer &) = delete;
			
		private:
			detail::HazardPointer m_pointer;
		};

		static hazard_pointer & get_local_hazard()
		{
			static thread_local hazard_pointer s_hazard_pointer;
			return s_hazard_pointer;
		}

    private:

        static void * allocate_page_impl()
        {
            return operator new (page_size);
        }

        static void deallocate_page_impl(void * i_page) noexcept
        {
			while (get_hazard_context().is_hazard_pointer(i_page))
			{
			}
            #if __cplusplus >= 201402L
                operator delete (i_page, page_size); // since C++14
            #else
                operator delete (i_page);
            #endif
        }

        #if DENSITY_DEBUG_INTERNAL && DENSITY_ENV_HAS_THREADING
            class DbgData
            {
            public:

                void add_page(void * i_page) noexcept
                {                    
                    if (m_enable)
                    {
                        try
                        {
							std::lock_guard<std::mutex> lock(m_mutex);
                            const bool inserted = m_pages.insert(i_page).second;
                            DENSITY_ASSERT_INTERNAL(inserted);
                        }
                        catch (...)
                        {
                            m_pages.clear();
                            m_enable = false;
                        }
                    }
                }

                void remove_page(void * i_page) noexcept
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    auto const removed = m_pages.erase(i_page);
                    if (m_enable)
                    {
                        DENSITY_ASSERT_INTERNAL(removed == 1);
                    }
                }

                ~DbgData()
                {
                    DENSITY_ASSERT_INTERNAL(m_pages.size() == 0);
                }


            private:
                std::mutex m_mutex;
                std::unordered_set<void*> m_pages;
                bool m_enable = true;
            };

            static DbgData & dbg_data()
            {
                static DbgData s_dbg_data;
                return s_dbg_data;
            }
        #endif // #if DENSITY_DEBUG_INTERNAL && DENSITY_ENV_HAS_THREADING

        class PageList
        {
        public:

            PageList() = default;
            PageList(const PageList &) = delete;
            PageList & operator = (const PageList &) = delete;

            ~PageList()
            {
                auto curr = m_first;
                while (curr != nullptr)
                {
                    auto next = curr->m_next;
                    #if __cplusplus >= 201402L
                        operator delete (curr, page_size); // since C++14
                    #else
                        operator delete (curr);
                    #endif
                    curr = next;
                }
            }

            void push(void * i_page) noexcept
            {
                Header * page = static_cast<Header*>(i_page);
                page->m_next = m_first;
                m_first = page;
                m_size++;
            }

            void * peek() noexcept
            {
                auto const result = m_first;
                if (result != nullptr)
                {
                    DENSITY_ASSERT_INTERNAL(m_size > 0);
                    m_first = result->m_next;
                    m_size--;
                }
                return result;
            }

            size_t size() const noexcept { return m_size; }

        private:
            struct Header
            {
                Header * m_next;
            };
            Header * m_first = nullptr;
            size_t m_size = 0;
        };

        static PageList & thread_page_store() noexcept
        {
            static thread_local PageList s_thread_page_store;
            return s_thread_page_store;
        }

		static detail::HazardPointersContext & get_hazard_context()
		{
			static detail::HazardPointersContext hazard_context;
			return hazard_context;
		}
    };

} // namespace density
