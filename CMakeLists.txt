cmake_minimum_required(VERSION 3.14)
project(PyCppExchangeSim)

set(CMAKE_CXX_STANDARD 20)

# Add source directory to include paths
include_directories(${CMAKE_SOURCE_DIR})

# Create a library of all the trading components
add_library(TradingComponents INTERFACE)
target_include_directories(TradingComponents INTERFACE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

# Create the executable
add_executable(PyCppExchangeSim main.cpp)
target_link_libraries(PyCppExchangeSim PRIVATE TradingComponents)