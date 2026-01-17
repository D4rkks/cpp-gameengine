#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_audio.h>
#include <glm/glm.hpp>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace Engine {

struct AudioSourceData {
  std::vector<uint8_t> buffer;
  SDL_AudioSpec spec;
};

struct ActiveVoice {
  std::string Name;


  uint8_t *bufferData;
  uint32_t length;
  uint32_t currentPos;

  float volume;
  bool loop = false;
  bool isSpatial = false;
  float range = 100.0f;
  glm::vec3 position;

  bool finished = false;
};

class AudioEngine {
public:
  static void Init();
  static void Shutdown();

  static void LoadSound(const std::string &name, const std::string &filepath);
  static void PlaySound(const std::string &name, float volume = 1.0f,
                        bool loop = false, bool spatial = false,
                        float range = 100.0f, glm::vec3 pos = {0, 0, 0});
  static void SetVoicePosition(const std::string &name, const glm::vec3 &pos);

  static void SetListenerTransform(const glm::vec3 &pos,
                                   const glm::vec3 &forward,
                                   const glm::vec3 &up);
  static void Update(float ts);

private:
  static void AudioCallback(void *userdata, uint8_t *stream, int len);

  static SDL_AudioDeviceID s_AudioDevice;
  static SDL_AudioSpec s_DeviceSpec;
  static std::map<std::string, AudioSourceData> s_SoundBank;
  static std::vector<ActiveVoice *> s_ActiveVoices;
  static std::mutex s_Mutex;

  static glm::vec3 s_ListenerPos;
  static glm::vec3 s_ListenerForward;
  static glm::vec3 s_ListenerUp;
};

}
