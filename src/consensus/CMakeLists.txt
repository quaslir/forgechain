add_library(consensus
ProofOfWork.cpp
)

target_include_directories(consensus PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(consensus PUBLIC crypto core project_warnings)
