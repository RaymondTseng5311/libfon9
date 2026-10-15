set(f9set_src
 ExgTypes.cpp
 ExgMdMoldUdp64.cpp
 ExgMdItchParser.cpp
 ExgMdOrderBook.cpp
 ExgMdBookBuilder.cpp
 ExgMdSoupBinTcp.cpp
 ExgMdSymbs.cpp
 ExgMdRtsBuilder.cpp
 ExgMdSystem.cpp
 ExgMdReceiverSession.cpp
 ExgMdReceiverFactory.cpp
 ExgMdSystemPlugin.cpp
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

   add_executable(f9setMdSys_UT f9setMdSys_UT.cpp)
   target_link_libraries(f9setMdSys_UT f9set_s fon9_s)
endif()
############################## Unit Test END ############################
#########################################################################
