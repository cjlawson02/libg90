add_executable(g90ctl g90ctl_main.cpp)

target_link_libraries(g90ctl PRIVATE libg90::libg90)

target_compile_options(g90ctl PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)

install(TARGETS g90ctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
