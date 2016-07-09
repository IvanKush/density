
#include "test_session.h"
#include <fstream>
#include <ctime>
#include <iomanip>
#include <algorithm>
#ifdef _MSC_VER
	#include <time.h>
#endif

namespace testity
{
	void Results::add_result(const BenchmarkTest * i_test, size_t i_cardinality, Duration i_duration)
	{
		m_performance_results.insert(std::make_pair(TestId{ i_test, i_cardinality }, i_duration));
	}

	void Results::save_to(const char * i_filename) const
	{
		std::ofstream file(i_filename, std::ios_base::out | std::ios_base::app);
		save_to(file);
	}

	void Results::save_to(std::ostream & i_ostream) const
	{
		save_to_impl("", m_test_tree, i_ostream);
	}

	void Results::save_to_impl(std::string i_path, const TestTree & i_test_tree, std::ostream & i_ostream) const
	{
		for (const auto & performance_test_group : i_test_tree.performance_tests())
		{
			i_ostream << "-------------------------------------" << std::endl;
			i_ostream << "PERFORMANCE_TEST_GROUP:" << i_path << std::endl;
			i_ostream << "NAME:" << performance_test_group.name() << std::endl;
			i_ostream << "VERSION_LABEL:" << performance_test_group.version_label() << std::endl;
			i_ostream << "COMPILER:" << m_environment.compiler() << std::endl;
			i_ostream << "OS:" << m_environment.operating_sytem() << std::endl;
			i_ostream << "SYSTEM:" << m_environment.system_info() << std::endl;
			i_ostream << "SIZEOF_POINTER:" << m_environment.sizeof_pointer() << std::endl;
			i_ostream << "DETERMINISTIC:" << (m_session.config().m_deterministic ? "yes" : "no") << std::endl;
			i_ostream << "RANDOM_SHUFFLE:" << (m_session.config().m_random_shuffle ? "yes (with std::mt19937)" : "no") << std::endl;

			const auto date_time = std::chrono::system_clock::to_time_t(m_environment.startup_clock());
			
			#ifdef _MSC_VER
				tm local_tm;
				localtime_s(&local_tm, &date_time);
			#else
				const auto local_tm = *std::localtime(&date_time);
			#endif
			
			#ifndef __GNUC__
				i_ostream << "DATE_TIME:" << std::put_time(&local_tm, "%d-%m-%Y %H:%M:%S") << std::endl;
			#else
				// gcc 4.9 is missing the support for std::put_time
				const size_t temp_time_str_len = 64;
				char temp_time_str[temp_time_str_len];
				if (std::strftime(temp_time_str, temp_time_str_len, "%d-%m-%Y %H:%M:%S", &local_tm) > 0)
				{
				i_ostream << "DATE_TIME:" << temp_time_str << std::endl;
				}
				else
				{
				i_ostream << "DATE_TIME:" << "!std::strftime failed" << std::endl;
				}
			#endif

			i_ostream << "CARDINALITY_START:" << performance_test_group.cardinality_start() << std::endl;
			i_ostream << "CARDINALITY_STEP:" << performance_test_group.cardinality_step() << std::endl;
			i_ostream << "CARDINALITY_END:" << performance_test_group.cardinality_end() << std::endl;
			i_ostream << "MULTEPLICITY:" << m_session.config().m_performance_repetitions << std::endl;

			// write legend
			i_ostream << "LEGEND_START:" << std::endl;
			{
				for (auto & test : performance_test_group.tests())
				{
					i_ostream << "TEST:" << test.source_code() << std::endl;
				}
			}
			i_ostream << "LEGEND_END:" << std::endl;

			// write table
			i_ostream << "TABLE_START:-----------------------" << std::endl;
			for (size_t cardinality = performance_test_group.cardinality_start();
				cardinality < performance_test_group.cardinality_end(); cardinality += performance_test_group.cardinality_step())
			{
				i_ostream << "ROW:";
				i_ostream << cardinality << '\t';

				for (auto & test : performance_test_group.tests())
				{
					auto range = m_performance_results.equal_range(TestId{ &test, cardinality });
					bool is_first = true;
					for (auto it = range.first; it != range.second; ++it)
					{
						if (!is_first)
						{
							i_ostream << ", ";
						}
						else
						{
							is_first = false;
						}
						i_ostream << it->second.count();
					}

					i_ostream << '\t';
				}
				i_ostream << std::endl;
			}
			i_ostream << "TABLE_END:-----------------------" << std::endl;
			i_ostream << "PERFORMANCE_TEST_GROUP_END:" << i_path << std::endl;
		}

		for (const auto & node : i_test_tree.children())
		{
			save_to_impl(i_path + node.name() + '/', node, i_ostream);
		}
	}

	void Session::generate_performance_operations(const TestTree & i_test_tree, Operations & i_dest) const
	{
		for (auto & test_group : i_test_tree.performance_tests())
		{
			for (size_t cardinality = test_group.cardinality_start();
				cardinality < test_group.cardinality_end(); cardinality += test_group.cardinality_step())
			{
				for (auto & test : test_group.tests())
				{
					i_dest.push_back([&test, cardinality](Results & results) {
						using namespace std::chrono;
						const auto time_before = high_resolution_clock::now();
						test.function()(cardinality);
						const auto duration = duration_cast<nanoseconds>(high_resolution_clock::now() - time_before);
						results.add_result(&test, cardinality, duration);
					});
				}
			}
		}

		for (auto & child : i_test_tree.children())
		{
			generate_performance_operations(child, i_dest);
		}
	}

	Results Session::run(const TestTree & i_test_tree, std::ostream & i_dest_stream) const
	{
		using namespace std;

		mt19937 random = m_config.m_deterministic ? mt19937() : mt19937(random_device()());

		// generate operation array
		Operations operations;
		if (m_config.m_test_performances)
		{
			for (size_t repetition_index = 0; repetition_index < m_config.m_performance_repetitions; repetition_index++)
			{
				generate_performance_operations(i_test_tree, operations);
			}
		}

		const auto operations_size = operations.size();

		if (m_config.m_random_shuffle)
		{
			i_dest_stream << "randomizing operations..." << endl;
			std::shuffle(operations.begin(), operations.end(), random);
		}

		i_dest_stream << "performing tests..." << endl;
		Results results(i_test_tree, *this);
		double last_perc = -1.;
		const double perc_mult = 100. / operations_size;
		for (Operations::size_type index = 0; index < operations_size; index++)
		{
			const double perc = floor(index * perc_mult);
			if (perc != last_perc)
			{
				i_dest_stream << perc << "%" << endl;
				last_perc = perc;
			}
			operations[index](results);
		}
		return results;
	}
}