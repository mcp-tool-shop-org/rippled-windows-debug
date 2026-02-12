# GitInfo.cmake
# Captures git information at configure time and creates compiler definitions
#
# Usage in CMakeLists.txt:
#   include(cmake/GitInfo.cmake)
#   target_compile_definitions(your_target PRIVATE ${GIT_DEFINITIONS})
#
# This will define:
#   -DGIT_COMMIT_HASH="abc123def"
#   -DGIT_BRANCH="main"
#   -DGIT_DIRTY=0 or 1
#   -DGIT_COMMIT_DATE="2024-02-12"
#   -DGIT_DESCRIBE="v1.0.0-5-gabc123"

find_package(Git QUIET)

if(GIT_FOUND)
    # Get commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short=12 HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Get branch name
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Check if dirty
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_STATUS)
        set(GIT_DIRTY 1)
    else()
        set(GIT_DIRTY 0)
    endif()

    # Get commit date
    execute_process(
        COMMAND ${GIT_EXECUTABLE} log -1 --format=%cs
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_DATE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Get describe (tag info)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    message(STATUS "Git commit: ${GIT_COMMIT_HASH} (${GIT_BRANCH})${GIT_DIRTY ? ' dirty' : ''}")
else()
    set(GIT_COMMIT_HASH "unknown")
    set(GIT_BRANCH "unknown")
    set(GIT_DIRTY 0)
    set(GIT_COMMIT_DATE "unknown")
    set(GIT_DESCRIBE "unknown")
    message(STATUS "Git not found, using defaults")
endif()

# Create definitions list
set(GIT_DEFINITIONS
    GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
    GIT_BRANCH="${GIT_BRANCH}"
    GIT_DIRTY=${GIT_DIRTY}
    GIT_COMMIT_DATE="${GIT_COMMIT_DATE}"
    GIT_DESCRIBE="${GIT_DESCRIBE}"
)
