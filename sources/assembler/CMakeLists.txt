# Assembler source files
set(ASM_SOURCES
    Main.cpp
    Assembler.cpp
)

# Assembler headers
set(ASM_HEADERS
    Assembler.h
)

# Create assembler executable (Honeycomb assembler driver)
add_executable(bas ${ASM_SOURCES} ${ASM_HEADERS})

# Set include directories
target_include_directories(bas PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/sources)

# Set compiler warnings
if(MSVC)
    target_compile_options(bas PRIVATE /W4 /WX)
else()
    target_compile_options(bas PRIVATE -Wall -Wextra -Werror)
endif()

# Install target
install(TARGETS bas
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
