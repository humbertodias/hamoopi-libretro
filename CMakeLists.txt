cmake_minimum_required(VERSION 3.7)
project(HAMOOPI)
add_executable(HAMOOPI src/standalone/HAMOOPI.cpp)

set(Alleg_Libraries "alleg")

# Find Allegro
find_package(Alleg4 4)

# Add directories to the include search path
target_include_directories(HAMOOPI PRIVATE ${ALLEGRO_INCLUDE_DIR})
# Link with dependencies
target_link_libraries(HAMOOPI ${Alleg_Libraries})