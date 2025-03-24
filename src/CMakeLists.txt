set(SOURCES
    main.cpp
    rootsmagicimporter.cpp
)

set(HEADERS
    ${CMAKE_SOURCE_DIR}/include/rootsmagicimporter.h
)

add_executable(rootsmagic_utils ${SOURCES} ${HEADERS})

target_include_directories(rootsmagic_utils
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/sqlite
)

target_link_libraries(rootsmagic_utils
    PRIVATE
    sqlite3
) 