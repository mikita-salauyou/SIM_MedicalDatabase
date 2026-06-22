cmake_minimum_required(VERSION 3.16)
project(BazaBadanObrazowych)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
find_package(DCMTK REQUIRED)

set(SOURCES main.cpp)

# Windows: ikona .exe + okno bez konsoli
if(WIN32)
    list(APPEND SOURCES app.rc)
    add_executable(BazaBadanObrazowych WIN32 ${SOURCES})
else()
    add_executable(BazaBadanObrazowych ${SOURCES})
endif()

target_include_directories(BazaBadanObrazowych PRIVATE ${DCMTK_INCLUDE_DIRS})

# sciezki do bibliotek DCMTK
if(WIN32 AND DCMTK_DIR)
    target_link_directories(BazaBadanObrazowych PRIVATE
        ${DCMTK_LIBRARY_DIRS}
        "${DCMTK_DIR}/../lib"
    )
elseif(DCMTK_LIBRARY_DIRS)
    target_link_directories(BazaBadanObrazowych PRIVATE ${DCMTK_LIBRARY_DIRS})
endif()

target_link_libraries(BazaBadanObrazowych PRIVATE
    Qt6::Widgets
    Qt6::Sql
)

# DCMTK: na g++/MinGW czasem trzeba grupy linkera (statyczne biblioteki)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(BazaBadanObrazowych PRIVATE
        -Wl,--start-group ${DCMTK_LIBRARIES} -Wl,--end-group
    )
else()
    target_link_libraries(BazaBadanObrazowych PRIVATE ${DCMTK_LIBRARIES})
endif()

# DCMTK na Windowsie potrzebuje bibliotek sieciowych systemu
if(WIN32)
    target_link_libraries(BazaBadanObrazowych PRIVATE ws2_32 iphlpapi netapi32 wsock32)
endif()
