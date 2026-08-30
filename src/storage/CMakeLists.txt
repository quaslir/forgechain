add_library(storage
SqliteConnection.cpp
SqliteStatement.cpp
Storage.cpp
)

target_include_directories(storage PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(storage PUBLIC core project_warnings SQLite::SQLite3)
