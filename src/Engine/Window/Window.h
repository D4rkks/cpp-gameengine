#pragma once

#include <SDL.h>
#include <string>

namespace Engine {

struct WindowProps {
  std::string Title;
  int Width;
  int Height;

  WindowProps(const std::string &title = "My Engine", int width = 1280,
              int height = 720)
      : Title(title), Width(width), Height(height) {}
};

class Window {
public:
  Window(const WindowProps &props = WindowProps());
  ~Window();

  void OnUpdate();

  int GetWidth() const { return m_Data.Width; }
  int GetHeight() const { return m_Data.Height; }

  std::pair<int, int> GetDrawableSize() const;
  void OnResize(int width, int height);

  SDL_Window *GetNativeWindow() const { return m_Window; }
  SDL_GLContext GetContext() const { return m_Context; }

private:
  void Init(const WindowProps &props);
  void Shutdown();

private:
  SDL_Window *m_Window;
  SDL_GLContext m_Context;

  struct WindowData {
    std::string Title;
    int Width;
    int Height;
  };

  WindowData m_Data;
};

}
