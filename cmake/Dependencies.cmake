option(HONEYCOMB_HOST_IO "Build bvm with SDL3 host I/O devices" OFF)

if(HONEYCOMB_HOST_IO)
    include(${CMAKE_SOURCE_DIR}/CPM.cmake)

    CPMAddPackage(
        NAME SDL3
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG release-3.4.0
        GIT_SHALLOW TRUE
        OPTIONS
            "SDL_SHARED ON"
            "SDL_STATIC OFF"
            "SDL_DISABLE_INSTALL ON"
            "SDL_TESTS OFF"
            "SDL_EXAMPLES OFF"
    )

endif()
