add_library (imgui imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp)
# agent control (AgentUi.cpp): ImGui reports its items to the registry while agent control is on
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)
