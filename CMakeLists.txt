cmake_minimum_required(VERSION 3.20)

project(TradingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_library(trading_engine_core STATIC
    src/core/order_book.cpp
    src/market_data/kraken_parser.cpp
    src/market_data/kraken_checksum.cpp
    src/market_data/book_feed.cpp
    src/market_data/capture.cpp
    src/exchange/live_feed.cpp
)

target_include_directories(trading_engine_core PUBLIC src)
target_link_libraries(trading_engine_core PRIVATE Boost::headers)

add_executable(kraken_test
    src/main.cpp
    src/benchmark.cpp
    src/exchange/kraken_client.cpp
)

target_link_libraries(kraken_test PRIVATE
    trading_engine_core
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

include(CTest)

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(trading_engine_tests
        tests/order_book_test.cpp
        tests/kraken_parser_test.cpp
        tests/kraken_checksum_test.cpp
        tests/book_feed_test.cpp
    )

    target_link_libraries(trading_engine_tests PRIVATE
        trading_engine_core
        GTest::gtest_main
    )

    gtest_discover_tests(trading_engine_tests)

    set(EXAMPLE_CAPTURE "${CMAKE_CURRENT_SOURCE_DIR}/examples/btc-usd.ndjson")

    add_test(NAME ReplayExample COMMAND kraken_test --replay "${EXAMPLE_CAPTURE}")
    set_tests_properties(ReplayExample PROPERTIES
        PASS_REGULAR_EXPRESSION "Snapshots: 2 [|] Updates: 48 [|] Rejected sessions: 0"
    )

    add_test(NAME BenchmarkExample COMMAND kraken_test --benchmark "${EXAMPLE_CAPTURE}" 2)
    set_tests_properties(BenchmarkExample PROPERTIES
        PASS_REGULAR_EXPRESSION "Latency [(]100 samples[)]: p50=[0-9.]+ us [|] p95=[0-9.]+ us [|] p99=[0-9.]+ us"
    )
endif()
