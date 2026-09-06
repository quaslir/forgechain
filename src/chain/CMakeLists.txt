add_library(chain
ChainManager.cpp
)

target_include_directories(chain PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(chain PUBLIC core consensus crypto project_warnings)
