set(HYPERC_SOURCES
    HyperC.cpp
    Lexer.cpp
    Parser.cpp
    Codegen.cpp
    Preprocessor.cpp
    ModuleRegistry.cpp
    ${CMAKE_SOURCE_DIR}/sources/assembler/Assembler.cpp
)

add_library(hyperc STATIC ${HYPERC_SOURCES})

target_include_directories(hyperc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/sources
    ${CMAKE_SOURCE_DIR}/sources/assembler
)

if(MSVC)
    target_compile_options(hyperc PRIVATE /W4 /WX)
else()
    target_compile_options(hyperc PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(hcc Main.cpp)
target_link_libraries(hcc PRIVATE hyperc)

if(MSVC)
    target_compile_options(hcc PRIVATE /W4 /WX)
else()
    target_compile_options(hcc PRIVATE -Wall -Wextra -Werror)
endif()

install(TARGETS hcc hyperc
    RUNTIME DESTINATION bin
    ARCHIVE DESTINATION lib
)
