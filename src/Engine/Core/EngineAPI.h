#pragma once

#if defined(_WIN32)
  #if defined(ENGINELIB_BUILD)
    #define ENGINE_API __declspec(dllexport)
  #else
    #define ENGINE_API __declspec(dllimport)
  #endif
#else
  #define ENGINE_API
#endif
