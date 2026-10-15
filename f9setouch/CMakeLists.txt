set(f9setouch_src
 OuchSoupBinTcp.cpp
 OuchFmtStr.cpp
 ExgOuchTypes.cpp
 ExgOuchLineMgr.cpp
 ExgOuchTradingLine.cpp
 ExgOuchLineFactory.cpp
 ExgOuchTrade.cpp
)
add_library(f9setouch_s STATIC ${f9setouch_src})
target_link_libraries(f9setouch_s pthread rt dl fon9_s)

add_library(f9setouch SHARED ${f9setouch_src})
target_link_libraries(f9setouch pthread rt dl fon9)

#########################################################################
############################### Unit Test ###############################
if(CMAKE_FON9_BUILD_UNIT_TEST)
   add_executable(f9setOuch_UT f9setOuch_UT.cpp)
   target_link_libraries(f9setOuch_UT f9setouch_s fon9_s)
endif()
############################## Unit Test END ############################
#########################################################################
