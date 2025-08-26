# Sync utility
set(SYNC_SOURCES
    rootsmagicsync_main.cpp
    rootsmagicsync.cpp
)

set(SYNC_HEADERS
    ${CMAKE_SOURCE_DIR}/include/rootsmagicsync.h
)

add_executable(rootsmagic_sync ${SYNC_SOURCES} ${SYNC_HEADERS})

target_include_directories(rootsmagic_sync
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/sqlite
)

target_link_libraries(rootsmagic_sync
    PRIVATE
    sqlite3
) 