cmake_minimum_required(VERSION 2.8)
add_compile_options(-std=c++11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FORCE_INLINES")
file(GLOB yasync_SRC "*.cpp")
file(GLOB yasync_HDR "*.h" "*.tcc")
add_library (yasync ${yasync_SRC})
# target_include_directories (yasync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

install(TARGETS yasync
        DESTINATION "lib"
        ) 
INSTALL(FILES ${yasync_HDR}
        DESTINATION "include/yasync"
        ) 