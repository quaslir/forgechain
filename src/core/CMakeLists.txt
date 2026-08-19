add_library(core
   Block.cpp
   Blockchain.cpp
   Transaction.cpp
   Ledger.cpp
   Mempool.cpp
   OrphanPool.cpp
)

target_include_directories(core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(core PUBLIC crypto project_warnings)
