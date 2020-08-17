set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_LIBS        OFF)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -fsanitize=undefined,address")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -fno-rtti")

#add_compile_options(-Rpass-missed=inline)
#set_source_files_properties(gc_test.cc PROPERTIES COMPILE_FLAGS -Rpass-missed=inline)


find_package( Threads )
find_package(Boost 1.67 COMPONENTS system filesystem REQUIRED)
if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
    include_directories(../../vendor)

    set(CMAKE_CXX_STANDARD 17)

    file(GLOB SOURCE_FILES
        "*.h"
        "*.cc"
        "*.c"
    )

    add_library(park_runtime ${SOURCE_FILES})

endif()

