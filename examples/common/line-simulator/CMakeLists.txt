# The shared parcel sortation line-simulator every chapter drives. See
# include/line_simulator/line_simulator.hpp for the API.
add_library(line_simulator
  src/line_simulator.cpp)
target_include_directories(line_simulator PUBLIC include)
lgtm_apply_common_options(line_simulator)

add_subdirectory(tests)
