cmake_minimum_required(VERSION 3.28)
project(RFT-Client)

set(CMAKE_CXX_STANDARD 20)

set (SOURCE_FILES
        unreliableTransport.cpp
        unreliableTransport.h
        main.cpp
        logging.h
        datagram.cpp
        datagram.h
        timerC.cpp
        timerC.h
)

add_executable(rftClient ${SOURCE_FILES})

# Find the current user's name
# Create a new list to hold the full paths
set(FULL_PATHS "")

# Loop through each file and prepend the current source directory
foreach(SOURCE_FILE ${SOURCE_FILES})
    list(APPEND FULL_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE}")
endforeach()

find_program(WHOAMI whoami)
execute_process(COMMAND ${WHOAMI} OUTPUT_VARIABLE CURRENT_USER OUTPUT_STRIP_TRAILING_WHITESPACE)

# Add a custom target named 'submit'
add_custom_target(submit
        COMMAND ${CMAKE_COMMAND} -E echo "Getting the current username..."
        COMMAND ${CMAKE_COMMAND} -E echo "Username: ${CURRENT_USER}"
        COMMAND ${CMAKE_COMMAND} -E echo "Creating directory named after the user..."
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${CURRENT_USER}
        COMMAND ${CMAKE_COMMAND} -E echo "Copying source files to the users directory..."
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FULL_PATHS} ${CMAKE_BINARY_DIR}/${CURRENT_USER}
        COMMAND ${CMAKE_COMMAND} -E echo "Creating tarball of the directory..."
        COMMAND ${CMAKE_COMMAND} -E tar czf ${CMAKE_CURRENT_SOURCE_DIR}/${CURRENT_USER}.tar.gz ${CMAKE_BINARY_DIR}/${CURRENT_USER}
        COMMAND ${CMAKE_COMMAND} -E echo "Submission tarball created: ${CMAKE_CURRENT_SOURCE_DIR}/${CURRENT_USER}.tar.gz"
)
