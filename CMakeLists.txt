cmake_minimum_required(VERSION 3.16)
project(BazaBadanObrazowych)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(DCMTK REQUIRED)

set(SOURCES main.cpp)

# ikona .exe na Windowsie
if(WIN32)
    set(SOURCES ${SOURCES} app.rc)
endif()

add_executable(BazaBadanObrazowych WIN32 ${SOURCES})

target_include_directories(BazaBadanObrazowych PRIVATE ${DCMTK_INCLUDE_DIRS})

# katalog ze statycznymi bibliotekami DCMTK
target_link_directories(BazaBadanObrazowych PRIVATE
    ${DCMTK_LIBRARY_DIRS}
    "${DCMTK_DIR}/../lib"
)

# DCMTK to kilka statycznych bibliotek ktore zaleza od siebie nawzajem,
# wiec owijamy je w grupe linkera zeby nie przejmowac sie kolejnoscia
target_link_libraries(BazaBadanObrazowych PRIVATE
    Qt6::Widgets
    Qt6::Sql
    -Wl,--start-group ${DCMTK_LIBRARIES} -Wl,--end-group
)

# DCMTK na Windowsie potrzebuje bibliotek sieciowych systemu
if(WIN32)
    target_link_libraries(BazaBadanObrazowych PRIVATE ws2_32 iphlpapi netapi32 wsock32)
endif()
