#pragma once
#include "density_test_common.h"
#include "progress.h"
#include "easy_random.h"
#include "line_updater_stream_adapter.h"
#include "test_objects.h"
#include "histogram.h"
#include <density/density_common.h> // for density::concurrent_alignment
#include <vector>
#include <thread>
#include <unordered_map>
#include <ostream>
#include <numeric>
#include <memory>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable:4324) // structure was padded due to alignment specifier
#endif

namespace density_tests
{
	template <typename QUEUE>
		class QueueGenericTester
	{
	public:

		using PutTestCase = void (*)(QUEUE & i_queue, EasyRandom &);
		using ConsumeTestCase = void (*)(const typename QUEUE::consume_operation & i_queue);

		using ReentrantPutTestCase = typename QUEUE::template reentrant_put_transaction<void> (*)(QUEUE & i_queue, EasyRandom &);
		using ReentrantConsumeTestCase = void (*)(const typename QUEUE::reentrant_consume_operation & i_queue);

		QueueGenericTester(std::ostream & i_output, size_t m_thread_count)
			: m_output(i_output), m_thread_count(m_thread_count)
		{

		}

		QueueGenericTester(const QueueGenericTester &) = delete;
		QueueGenericTester & operator = (const QueueGenericTester &) = delete;

		template <typename PUT_CASE>
			void add_test_case()
		{
			using ElementType = typename PUT_CASE::ElementType;
			add_test_case(QUEUE::runtime_type::template make<ElementType>(),
				&PUT_CASE::put, &PUT_CASE::reentrant_put, &PUT_CASE::consume, &PUT_CASE::reentrant_consume);
		}

		void add_test_case(const typename QUEUE::runtime_type & i_type, 
			PutTestCase i_put_func, ReentrantPutTestCase i_reentrant_put_func, 
			ConsumeTestCase i_consume_func, ReentrantConsumeTestCase i_reentrant_consume_func)
		{
			auto result = m_element_types.insert(std::make_pair(i_type, m_put_cases.size()));
			DENSITY_TEST_ASSERT(result.second);

			m_put_cases.push_back(i_put_func);
			m_reentrant_put_cases.push_back(i_reentrant_put_func);
			m_consume_cases.push_back(i_consume_func);			
			m_reentrant_consume_cases.push_back(i_reentrant_consume_func);
		}

		/** Runs a test session. This function does not alter the object. */
		void run(QueueTesterFlags i_flags,
			EasyRandom & i_random,
			size_t i_target_put_count) const
		{
			bool const with_exceptions = (i_flags & QueueTesterFlags::eTestExceptions) != QueueTesterFlags::eNone;

			m_output << "starting queue generic test with " << m_thread_count << " threads and ";
			m_output << i_target_put_count << " total puts";
			m_output << "\nheterogeneous_queue: " << truncated_type_name<QUEUE>();
			m_output << "\ncommon_type: " << truncated_type_name<typename QUEUE::common_type>();
			m_output << "\nruntime_type: " << truncated_type_name<typename QUEUE::runtime_type>();
			m_output << "\nallocator_type: " << truncated_type_name<typename QUEUE::allocator_type>();
			m_output << "\npage_alignment: " << QUEUE::allocator_type::page_alignment;
			m_output << "\npage_size: " << QUEUE::allocator_type::page_size;
			m_output << "\nconc puts: " << QUEUE::concurrent_puts << "\t\t\tconc consume: " << QUEUE::concurrent_consumes;
			m_output << "\nconc put-consumes: " << QUEUE::concurrent_put_consumes << "\t\t\tis_seq_cst: " << QUEUE::is_seq_cst;
			m_output << "\nwith_exceptions: " << with_exceptions;
			m_output << std::endl;

			InstanceCounted::ScopedLeakCheck objecty_leak_check;
			run_impl(i_flags, i_random, i_target_put_count);

			m_output << "--------------------------------------------\n" << std::endl;
		}

	private:

		void run_impl(QueueTesterFlags i_flags, EasyRandom & i_random, size_t i_target_put_count) const
		{
			auto const case_count = m_element_types.size();
			DENSITY_TEST_ASSERT( m_put_cases.size() == case_count &&
				m_consume_cases.size() == case_count &&
				m_reentrant_put_cases.size() == case_count &&
				m_reentrant_consume_cases.size() == case_count);

			bool const with_exceptions = (i_flags & QueueTesterFlags::eTestExceptions) != QueueTesterFlags::eNone;

			QUEUE queue;

			/* prepare the array of threads. The initialization of the random generator
				may take some time, so we do it before starting the threads. */
			std::vector<ThreadData> threads;
			threads.reserve(m_thread_count);
			for (size_t thread_index = 0; thread_index < m_thread_count; thread_index++)
			{
				threads.emplace_back(*this, queue, i_random, i_flags);
			}

			for (size_t thread_index = 0; thread_index < m_thread_count; thread_index++)
			{
				auto const thread_put_count = (i_target_put_count / m_thread_count) +
					( (thread_index == 0) ? (i_target_put_count % m_thread_count) : 0 );

				threads[thread_index].start(thread_put_count);
			}

			// wait for the test to be completed
			{
				LineUpdaterStreamAdapter line(m_output);
				bool complete = false;
				Progress progress(i_target_put_count);
				while(!complete)
				{
					size_t produced = 0, consumed = 0;
					for (auto & thread : threads)
					{
						produced += thread.incremental_stats().m_produced.load();
						consumed += thread.incremental_stats().m_consumed.load();
					}
					DENSITY_TEST_ASSERT(consumed <= i_target_put_count && produced <= i_target_put_count);
					complete = consumed >= i_target_put_count && produced >= i_target_put_count;

					progress.set_progress(consumed);
					line << "Consumed: " << consumed << " (" << progress << "), enqueued: " << produced - consumed << std::endl;
					if (!complete)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(200));
					}
				}
			}

			for (auto & thread : threads)
			{
				thread.join();
			}

			histogram<int64_t> histogram_spawned("spawned by i-th thread");
			histogram<int64_t> histogram_except_puts("exceptions_during_puts");
			histogram<int64_t> histogram_except_cons("exceptions_during_consumes");

			FinalStats final_state(m_put_cases.size());
			for (size_t thread_index = 0; thread_index < m_thread_count; thread_index++)
			{
				auto const & thread_state = threads[thread_index].final_stats();
				final_state += thread_state;

				auto const spawned = std::accumulate(
					thread_state.m_counters.begin(), 
					thread_state.m_counters.end(),
					static_cast<int64_t>(0),
					[](int64_t i_sum, const PutTypeCounters & i_counter) {
						return i_sum + i_counter.m_spawned; });
				histogram_spawned << spawned;

				if (with_exceptions)
				{
					histogram_except_puts << thread_state.m_exceptions_during_puts;
					histogram_except_cons << thread_state.m_exceptions_during_consumes;
				}
			}

			for (auto const & counter : final_state.m_counters)
			{
				DENSITY_TEST_ASSERT(counter.m_existing == 0);
			}

			m_output << histogram_spawned;
			if (with_exceptions)
			{
				m_output << histogram_except_puts;
				m_output << histogram_except_cons;
			}
		}


		/**< An instance of this struct is used by a single thread to keep the counters associated 
			of a single element type. Since these data a re thread-specific, counters can even be negative.
			When the test is completed, the sum of the counters of all the threads must be coherent (that
			is m_existing must be 0 and m_spawned must be the total count for the given type).*/
		struct PutTypeCounters
		{
			int64_t m_existing{0}; /**< How many elements of this type currently exist in the queue. */
			int64_t m_spawned{0}; /**< How many elements of this type have been put in the queue. */
		};

		struct IncrementalStats
		{
			std::atomic<size_t> m_produced{0};
			std::atomic<size_t> m_consumed{0};
		};

		struct FinalStats
		{
			std::vector<PutTypeCounters> m_counters;
			int64_t m_exceptions_during_puts{0};
			int64_t m_exceptions_during_consumes{0};

			FinalStats(size_t i_put_type_count)
				: m_counters(i_put_type_count)
			{
			}

			FinalStats & operator += (const FinalStats & i_source)
			{
				auto const count = i_source.m_counters.size();
				for (size_t i = 0; i < count; i++)
				{
					m_counters[i].m_existing += i_source.m_counters[i].m_existing;
					m_counters[i].m_spawned += i_source.m_counters[i].m_spawned;
				}
				m_exceptions_during_puts += i_source.m_exceptions_during_puts;
				m_exceptions_during_consumes += i_source.m_exceptions_during_consumes;
				return *this;
			}
		};

		class alignas(density::concurrent_alignment) ThreadData
		{
		public:

			ThreadData(const QueueGenericTester & i_parent_tester, QUEUE & i_queue, 
				EasyRandom & i_main_random, QueueTesterFlags i_flags)
					: m_queue(i_queue), m_parent_tester(i_parent_tester), m_flags(i_flags),
					  m_final_stats(i_parent_tester.m_put_cases.size()), 
					  m_random(i_main_random.fork())
			{
				m_incremental_stats = std::unique_ptr<IncrementalStats>(new IncrementalStats);
			}

			void start(size_t i_target_put_count)
			{
				m_thread = std::thread([=] { thread_procedure(i_target_put_count); });
			}

			void join()
			{
				m_thread.join();
			}

			const IncrementalStats & incremental_stats() const
			{
				return *m_incremental_stats;
			}

			const FinalStats & final_stats() const
			{
				return m_final_stats;
			}

		private: // internal data

			QUEUE & m_queue;
			const QueueGenericTester & m_parent_tester;
			QueueTesterFlags const m_flags;
			std::thread m_thread;
			FinalStats m_final_stats;
			std::unique_ptr<IncrementalStats> m_incremental_stats;
			EasyRandom m_random;

			size_t m_put_committed = 0;
			size_t m_consumes_committed = 0;

			struct ReentrantPut
			{
				typename QUEUE::template reentrant_put_transaction<void> m_transaction;
				size_t m_type_index;

				ReentrantPut(size_t i_type_index, typename QUEUE::template reentrant_put_transaction<void> && i_transaction)
					: m_transaction(std::move(i_transaction)), m_type_index(i_type_index)
				{

				}
			};

			struct ReentrantConsume
			{
				typename QUEUE::reentrant_consume_operation m_operation;
				size_t m_type_index;
				
				ReentrantConsume(size_t i_type_index, typename QUEUE::reentrant_consume_operation && i_operation)
					: m_operation(std::move(i_operation)), m_type_index(i_type_index)
				{
				}
			};

			std::vector<ReentrantPut> m_pending_reentrant_puts;
			std::vector<ReentrantConsume> m_pending_reentrant_consumes;

		private:
			
			void thread_procedure(size_t i_target_put_count)
			{
				for (size_t cycles = 0; m_put_committed < i_target_put_count || m_consumes_committed < i_target_put_count; cycles++)
				{
					// decide between put and consume

					auto const pending_put_index = m_random.get_int<size_t>(15);
					if (pending_put_index < m_pending_reentrant_puts.size() &&
						m_put_committed < i_target_put_count)
					{
						handle_pending_put(pending_put_index);
					}

					auto const pending_consume_index = m_random.get_int<size_t>(15);
					if (pending_consume_index < m_pending_reentrant_consumes.size() &&
						m_consumes_committed < i_target_put_count)
					{
						handle_pending_consume(pending_consume_index);
					}

					if (m_put_committed < i_target_put_count && m_random.get_bool())
					{
						put_one();						
					}
					else if(m_consumes_committed < i_target_put_count)
					{
						try_consume_one();
					}

					// update the progress periodically
					if ((cycles & 4095) == 0)
					{
						m_incremental_stats->m_produced.store(m_put_committed, std::memory_order_relaxed);
						m_incremental_stats->m_consumed.store(m_consumes_committed, std::memory_order_relaxed);
					}
				}

				m_incremental_stats->m_produced.store(m_put_committed, std::memory_order_relaxed);
				m_incremental_stats->m_consumed.store(m_consumes_committed, std::memory_order_relaxed);

				/* we destroy the pending operation (they get canceled), because otherwise the
					destructor of the queue would trigger an undefined behavior */
				m_pending_reentrant_consumes.clear();
				m_pending_reentrant_puts.clear();
			}

			void put_one()
			{
				/* pick a random type (outside the exception loop) to have a deterministic and exhaustive 
					exception test at least in isolation (in singlethread tests). */
				const auto type_index = m_random.get_int<size_t>(m_parent_tester.m_put_cases.size() - 1);

				auto put_func = [&] {

					if (m_random.get_bool())
					{
						(*m_parent_tester.m_put_cases[type_index])(m_queue, m_random);

						// done! From now on no exception can occur
						auto & counters = m_final_stats.m_counters[type_index];
						counters.m_existing++;
						counters.m_spawned++;

						m_put_committed++;
					}
					else
					{
						auto transaction = (*m_parent_tester.m_reentrant_put_cases[type_index])(m_queue, m_random);
						m_pending_reentrant_puts.push_back(ReentrantPut(type_index, std::move(transaction)));
					}
				};

				if ((m_flags & QueueTesterFlags::eTestExceptions) == QueueTesterFlags::eTestExceptions)
				{
					m_final_stats.m_exceptions_during_puts += run_exception_test(put_func);
				}
				else
				{
					put_func();
				}
			}

			void try_consume_one()
			{
				auto consume_func = [&] {
					if (m_random.get_bool())
					{
						if (auto consume = m_queue.try_start_consume())
						{
							// find the type to get the index
							auto const type = consume.complete_type();
							auto const type_it = m_parent_tester.m_element_types.find(type);
							DENSITY_TEST_ASSERT(type_it != m_parent_tester.m_element_types.end());

							// call the user-provided callback
							auto const type_index = type_it->second;
							(*m_parent_tester.m_consume_cases[type_index])(consume);

							// in case of exception the element is not consumed (because we don't call commit)
							exception_checkpoint();

							// done! From now on no exception can occur
							consume.commit();
							m_final_stats.m_counters[type_index].m_existing--;
							m_consumes_committed++;
						}
					}
					else
					{
						if (auto consume = m_queue.try_start_reentrant_consume())
						{
							// find the type to get the index
							auto const type = consume.complete_type();
							auto const type_it = m_parent_tester.m_element_types.find(type);
							DENSITY_TEST_ASSERT(type_it != m_parent_tester.m_element_types.end());

							// call the user-provided callback
							auto const type_index = type_it->second;
							(*m_parent_tester.m_reentrant_consume_cases[type_index])(consume);

							// in case of exception the element is not consumed (because we don't call commit)
							exception_checkpoint();

							m_pending_reentrant_consumes.push_back(ReentrantConsume(type_index, std::move(consume)));
						}
					}
				};

				if ((m_flags & QueueTesterFlags::eTestExceptions) == QueueTesterFlags::eTestExceptions)
				{
					m_final_stats.m_exceptions_during_consumes += run_exception_test(consume_func);
				}
				else
				{
					consume_func();
				}
			}

			void handle_pending_put(size_t const i_pending_put_index)
			{
				// commit or cancel a reentrant put
				auto & pending_put = m_pending_reentrant_puts[i_pending_put_index];
				auto const type_index = pending_put.m_type_index;
				if (m_random.get_bool())
				{
					pending_put.m_transaction.commit();
					m_final_stats.m_counters[type_index].m_existing++;
					m_final_stats.m_counters[type_index].m_spawned++;
					m_put_committed++;
				}
				else
				{
					pending_put.m_transaction.cancel();
				}

				m_pending_reentrant_puts.erase(m_pending_reentrant_puts.begin() + i_pending_put_index);
			}

			void handle_pending_consume(size_t const i_pending_consume_index)
			{
				// commit or cancel a reentrant consume
				auto & pending_consume = m_pending_reentrant_consumes[i_pending_consume_index];
				auto const type_index = pending_consume.m_type_index;

				(*m_parent_tester.m_reentrant_consume_cases[type_index])(pending_consume.m_operation);
							
				if (m_random.get_bool())
				{
					pending_consume.m_operation.commit();
					m_final_stats.m_counters[type_index].m_existing--;
					m_consumes_committed++;
				}
				else
				{
					pending_consume.m_operation.cancel();
				}

				m_pending_reentrant_consumes.erase(m_pending_reentrant_consumes.begin() + i_pending_consume_index);
			}
		};

	private:
		std::ostream & m_output;

		std::unordered_map<typename QUEUE::runtime_type, size_t> m_element_types;

		std::vector<PutTestCase> m_put_cases;
		std::vector<ConsumeTestCase> m_consume_cases;
		std::vector<ReentrantPutTestCase> m_reentrant_put_cases;
		std::vector<ReentrantConsumeTestCase> m_reentrant_consume_cases;

		size_t const m_thread_count;
	};

} // namespace density_tests

#ifdef _MSC_VER
	#pragma warning(pop)
#endif
	