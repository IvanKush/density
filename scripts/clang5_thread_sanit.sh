export CC=clang-5.0
export CXX=clang++-5.0
rm test/build -r
bash scripts/test.sh TRUE Release FALSE "" "-queue_tests_cardinality:3000 -only:lf_queue,sing-mult,relaxed,page_256" -fsanitize=thread

