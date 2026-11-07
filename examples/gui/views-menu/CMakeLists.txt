# Views-menu example: the whole UI *and* the view-switching logic are declared
# in views_menu.fml (the menu is a ListModel + Repeater over a Column).
firml_add_target(views_menu_codegen SOURCES views_menu.fml)

add_executable(firmir-example-gui-views-menu main.cpp)
target_link_libraries(firmir-example-gui-views-menu PRIVATE views_menu_codegen)
firmir_install_example(firmir-example-gui-views-menu)
