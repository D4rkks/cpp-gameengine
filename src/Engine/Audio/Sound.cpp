#include "Sound.h"
#include "AudioEngine.h"

namespace Engine {

void PlayOneShot(const std::string &filepath, float volume) {
  AudioEngine::PlaySound(filepath, volume, false, false);
}

void PlayOneShotAt(const std::string &filepath, const glm::vec3 &position,
                   float volume, float range) {
  AudioEngine::PlaySound(filepath, volume, false, true, range, position);
}

void PlayLoop(const std::string &filepath, float volume) {
  if (AudioEngine::IsPlaying(filepath))
    AudioEngine::SetVoiceVolume(filepath, volume);
  else
    AudioEngine::PlaySound(filepath, volume, true, false, 100.0f, {0, 0, 0}, AudioBus::Music);
}

void PlayLoopAt(const std::string &filepath, const glm::vec3 &position, float volume,
                float range) {
  if (AudioEngine::IsPlaying(filepath)) {
    AudioEngine::SetVoiceVolume(filepath, volume);
    AudioEngine::SetVoicePosition(filepath, position);
  } else {
    AudioEngine::PlaySound(filepath, volume, true, true, range, position);
  }
}

void StopLoop(const std::string &filepath) { AudioEngine::StopVoice(filepath); }

void PreloadSound(const std::string &filepath) {
  AudioEngine::LoadSound(filepath, filepath);
}

void SetDoppler(float factor) { AudioEngine::SetDopplerFactor(factor); }

void SetListenerVelocity(const glm::vec3 &velocity) { AudioEngine::SetListenerVelocity(velocity); }

}
