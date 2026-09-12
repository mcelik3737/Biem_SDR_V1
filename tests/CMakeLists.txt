add_executable(test_fec test_fec.cpp)
target_link_libraries(test_fec PRIVATE biem_core)
add_test(NAME test_fec COMMAND test_fec)

add_executable(test_nbfm test_nbfm.cpp)
target_link_libraries(test_nbfm PRIVATE biem_core)
add_test(NAME test_nbfm COMMAND test_nbfm)

add_executable(test_database test_database.cpp)
target_link_libraries(test_database PRIVATE biem_core)
add_test(NAME test_database COMMAND test_database)
