// Copyright 2017 ETH Zurich, Thomas Schops
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.

#include "opengl/opengl_util.h"
#include "opengl/opengl_util_egl.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif

#include <glog/logging.h>

namespace opengl {
namespace {

const char kWindowClassName[] = "DatasetPipelineHiddenOpenGLWindow";

LRESULT CALLBACK HiddenOpenGLWindowProc(HWND window, UINT message,
                                        WPARAM w_param, LPARAM l_param) {
  return DefWindowProc(window, message, w_param, l_param);
}

bool RegisterHiddenWindowClass() {
  static bool registered = []() {
    WNDCLASSA window_class = {};
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = HiddenOpenGLWindowProc;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.lpszClassName = kWindowClassName;
    return RegisterClassA(&window_class) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  }();
  return registered;
}

bool SetOpenGLPixelFormat(HDC device_context) {
  PIXELFORMATDESCRIPTOR descriptor = {};
  descriptor.nSize = sizeof(descriptor);
  descriptor.nVersion = 1;
  descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  descriptor.iPixelType = PFD_TYPE_RGBA;
  descriptor.cColorBits = 32;
  descriptor.cDepthBits = 24;
  descriptor.cStencilBits = 8;
  descriptor.iLayerType = PFD_MAIN_PLANE;

  const int pixel_format = ChoosePixelFormat(device_context, &descriptor);
  if (pixel_format == 0) {
    LOG(ERROR) << "ChoosePixelFormat() failed.";
    return false;
  }
  if (!SetPixelFormat(device_context, pixel_format, &descriptor)) {
    LOG(ERROR) << "SetPixelFormat() failed.";
    return false;
  }
  return true;
}

}  // namespace

OpenGLContext::OpenGLContext() {
  impl.reset(new OpenGLContextImpl());
  impl->window = nullptr;
  impl->device_context = nullptr;
  impl->rendering_context = nullptr;
  impl->owns_context = false;
  impl->needs_glew_initialization = false;
}

bool InitializeOpenGLWindowless(int /*version*/, OpenGLContext* result) {
  CHECK_NOTNULL(result);
  if (!RegisterHiddenWindowClass()) {
    LOG(ERROR) << "RegisterClassA() failed for hidden OpenGL window.";
    return false;
  }

  HWND window = CreateWindowExA(
      0, kWindowClassName, "DatasetPipelineOpenGL", WS_OVERLAPPEDWINDOW,
      0, 0, 8, 8, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
  if (!window) {
    LOG(ERROR) << "CreateWindowExA() failed for hidden OpenGL window.";
    return false;
  }

  HDC device_context = GetDC(window);
  if (!device_context) {
    LOG(ERROR) << "GetDC() failed for hidden OpenGL window.";
    DestroyWindow(window);
    return false;
  }

  if (!SetOpenGLPixelFormat(device_context)) {
    ReleaseDC(window, device_context);
    DestroyWindow(window);
    return false;
  }

  HGLRC rendering_context = wglCreateContext(device_context);
  if (!rendering_context) {
    LOG(ERROR) << "wglCreateContext() failed.";
    ReleaseDC(window, device_context);
    DestroyWindow(window);
    return false;
  }

  result->impl->window = window;
  result->impl->device_context = device_context;
  result->impl->rendering_context = rendering_context;
  result->impl->owns_context = true;
  result->impl->needs_glew_initialization = true;
  return true;
}

OpenGLContext SwitchOpenGLContext(const OpenGLContext& context) {
  OpenGLContext current_context;
  current_context.impl->device_context = wglGetCurrentDC();
  current_context.impl->rendering_context = wglGetCurrentContext();
  current_context.impl->owns_context = false;
  current_context.impl->needs_glew_initialization = false;

  HDC device_context = static_cast<HDC>(context.impl->device_context);
  HGLRC rendering_context = static_cast<HGLRC>(context.impl->rendering_context);
  if (!wglMakeCurrent(device_context, rendering_context)) {
    LOG(FATAL) << "Cannot make WGL context current.";
  }

  if (context.impl->needs_glew_initialization) {
    const GLenum glew_init_result = glewInit();
    CHECK_EQ(static_cast<int>(glew_init_result), GLEW_OK);
    context.impl->needs_glew_initialization = false;
  }

  return current_context;
}

bool IsOpenGLContextAvailable() {
  return wglGetCurrentContext() != nullptr;
}

void releaseOpenGLContext() {
  wglMakeCurrent(nullptr, nullptr);
}

void DeinitializeOpenGL(OpenGLContext* context) {
  CHECK_NOTNULL(context);
  if (!context->impl || !context->impl->owns_context) {
    return;
  }

  HDC device_context = static_cast<HDC>(context->impl->device_context);
  HGLRC rendering_context =
      static_cast<HGLRC>(context->impl->rendering_context);
  HWND window = static_cast<HWND>(context->impl->window);

  if (wglGetCurrentContext() == rendering_context) {
    wglMakeCurrent(nullptr, nullptr);
  }
  if (rendering_context) {
    wglDeleteContext(rendering_context);
  }
  if (window && device_context) {
    ReleaseDC(window, device_context);
  }
  if (window) {
    DestroyWindow(window);
  }

  context->impl->window = nullptr;
  context->impl->device_context = nullptr;
  context->impl->rendering_context = nullptr;
  context->impl->owns_context = false;
}

}  // namespace opengl
