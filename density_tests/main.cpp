
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <iostream>
#include <vector>
#include "testity\test_tree.h"
#include "testity\test_session.h"

namespace density_tests
{
    using namespace testity;

	// functionality cases
    void add_heterogeneous_array_cases(TestTree & i_dest);
	void add_lifo_cases(TestTree & i_dest);
	void add_queue_cases(TestTree & i_dest);

	// benchmarks
	void add_lifo_array_benchmarks(TestTree & i_dest);
}

int main()
{
    using namespace testity;
    using namespace density_tests;
	using namespace std;

    TestTree test_tree("density");

    add_heterogeneous_array_cases(test_tree["heterogeneous_array"]);
	add_queue_cases(test_tree["queue"]);
	add_lifo_cases(test_tree["lifo"]);
	add_lifo_array_benchmarks(test_tree["lifo"]);

	string last_label;
	auto test_results = run_session(test_tree, TestFlags::All, TestConfig(), 
		[&last_label](const Progression & i_progression) {
			if (last_label != i_progression.m_label)
			{
				last_label = i_progression.m_label;
				cout << endl << last_label << endl;
			}
			cout << static_cast<int>(i_progression.m_completion_factor * 100. + 0.5) << "%, remaining " <<
				i_progression.m_remaining_time_extimate.count() << " secs" << endl;
	});

	test_results.save_to("perf_1.txt");

    return 0;
}
