cmake_minimum_required(VERSION 3.10)
project(Lossless_Audio_Compression_Project)

set(CMAKE_CXX_STANDARD 17)

# 1. Define where ImGui lives
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)

# 2. Gather the ImGui source files and the GLFW/OpenGL3 backend files
set(IMGUI_SOURCES
	${IMGUI_DIR}/imgui.cpp
	${IMGUI_DIR}/imgui_draw.cpp
	${IMGUI_DIR}/imgui_tables.cpp
	${IMGUI_DIR}/imgui_widgets.cpp
	${IMGUI_DIR}/imgui_demo.cpp
	${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
	${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# 3. Gather your own source files
set(APP_SOURCES
	source/main.cpp
	source/compressor.cpp
)

# 4. Create the executable
add_executable(${PROJECT_NAME} ${APP_SOURCES} ${IMGUI_SOURCES})

# 5. Include directories so you can #include "imgui.h" easily
target_include_directories(${PROJECT_NAME} PRIVATE
	${IMGUI_DIR}
	${IMGUI_DIR}/backends
	source
)

# 6. Find and link the required Linux system libraries
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	OpenGL::GL
	pthread
	dl
)