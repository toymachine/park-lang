set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_LIBS        OFF)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -fsanitize=undefined,address")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -march=native")

find_package( Threads )
find_package(Boost 1.67 COMPONENTS system filesystem context REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    include_directories(../../vendor)
    include_directories(../../include)

    set(CMAKE_CXX_STANDARD 17)

    add_executable(park park.cc)
    target_link_libraries(park ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} park_runtime)

    add_executable(test_park test.cc)
    target_link_libraries(test_park ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} park_runtime)

endif()

