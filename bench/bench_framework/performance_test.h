
//   Copyright Giuseppe Campana (giu.campana@gmail.com) 2016-2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#include <string>
#include <vector>

#define BENCH_MAKE_TEST(i_cardinality, code)\
    density_bench::PerformanceTest(#code, [](size_t i_cardinality){code})

namespace density_bench
{
    class PerformanceTest
    {
    public:

        using TestFunction = void (*)(size_t i_cardinality);

        PerformanceTest(std::string i_source_code, TestFunction i_function)
            : m_source_code(std::move(i_source_code)), m_function(i_function)
                { }

        const std::string & source_code() const { return m_source_code; }
        TestFunction function() const { return m_function; }

    private:
        std::string m_source_code;
        TestFunction m_function;
    };

    class PerformanceTestGroup
    {
    public:

        PerformanceTestGroup(std::string i_name, std::string i_version_label)
            : m_name(std::move(i_name)), m_version_label(std::move(i_version_label)) { }

        void set_description(std::string i_description)
        {
            m_description = std::move(i_description);
        }

        void set_prolog_code(std::string i_code)
        {
            m_prolog_code = std::move(i_code);
        }

        void set_prolog_code(const char * i_code, size_t i_length)
        {
            m_prolog_code.assign(i_code, i_length);
        }

        void add_test(PerformanceTest i_test)
        {
            m_tests.push_back(std::move(i_test));
        }

        void add_test(const char * i_source_file, int i_start_line,
           PerformanceTest::TestFunction i_function, int i_end_line);

        const std::string & name() const { return m_name; }
        const std::string & version_label() const { return m_version_label; }
        const std::string & description() const { return m_description; }
        const std::string & prolog_code() const { return m_prolog_code; }

        size_t cardinality_start() const { return m_cardinality_start; }
        size_t cardinality_step() const { return m_cardinality_step; }
        size_t cardinality_end() const { return m_cardinality_end; }

        void set_cardinality_start(size_t i_cardinality_start) { m_cardinality_start = i_cardinality_start; }
        void set_cardinality_step(size_t i_cardinality_step) { m_cardinality_step = i_cardinality_step; }
        void set_cardinality_end(size_t i_cardinality_end) { m_cardinality_end = i_cardinality_end; }

        const std::vector<PerformanceTest> & tests() const { return m_tests; }

        static void set_source_dir(const char * i_dir);

        static const char * set_source_dir()
        {
            return s_source_dir.c_str();
        }

    private:
        size_t m_cardinality_start = 0;
        size_t m_cardinality_step = 1000;
        size_t m_cardinality_end = 80000;
        std::vector<PerformanceTest> m_tests;
        std::string m_name, m_description, m_prolog_code, m_version_label;
        
        static std::string s_source_dir;
    };
}
