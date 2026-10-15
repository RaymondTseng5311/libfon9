set(f9set_src
 ExgTypes.cpp
 ExgMdMoldUdp64.cpp
 ExgMdItchParser.cpp
 ExgMdOrderBook.cpp
 ExgMdBookBuilder.cpp
)
add_library(f9set_s STATIC ${f9set_src})
target_link_libraries(f9set_s pthread rt dl fon9_s)

add_library(f9set SHARED ${f9set_src})
target_link_libraries(f9set pthread rt dl fon9)

#########################################################################
############################### Unit Test ###############################
if(CMAKE_FON9_BUILD_UNIT_TEST)
   add_executable(f9setExgMdItch_UT ExgMdItch_UT.cpp)
   target_link_libraries(f9setExgMdItch_UT fon9_s f9set_s)
endif()
############################## Unit Test END ############################
#########################################################################
