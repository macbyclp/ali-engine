#pragma once
// Single include point for the GL API + windowing, native or web.
#ifdef ENGINE_WEB
    #include <GLES3/gl3.h>
    #include <GLFW/glfw3.h>
    #include <emscripten.h>
    #include <emscripten/html5.h>
#else
    #include <glad/gl.h>
    #include <GLFW/glfw3.h>
#endif
