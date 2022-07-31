// This file contains include-as-implementations

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glad/glad.h>

#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <../res/bindings/imgui_impl_glfw.cpp>
#include <../res/bindings/imgui_impl_opengl3.cpp>
