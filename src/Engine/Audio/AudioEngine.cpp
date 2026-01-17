#include "AudioEngine.h"
#include <algorithm>
#include <iostream>

namespace Engine {

SDL_AudioDeviceID AudioEngine::s_AudioDevice = 0;
SDL_AudioSpec AudioEngine::s_DeviceSpec;
std::map<std::string, AudioSourceData> AudioEngine::s_SoundBank;
std::vector<ActiveVoice *> AudioEngine::s_ActiveVoices;
std::mutex AudioEngine::s_Mutex;
glm::vec3 AudioEngine::s_ListenerPos = {0.0f, 0.0f, 0.0f};
glm::vec3 AudioEngine::s_ListenerForward = {0.0f, 0.0f, -1.0f};
glm::vec3 AudioEngine::s_ListenerUp = {0.0f, 1.0f, 0.0f};

void AudioEngine::Init() {
  if (SDL_Init(SDL_INIT_AUDIO) < 0) {
    std::cout << "[Audio] Init Failed: " << SDL_GetError() << std::endl;
    return;
  }

  SDL_AudioSpec want;
  SDL_memset(&want, 0, sizeof(want));
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 4096;
  want.callback = AudioCallback;

  s_AudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, &s_DeviceSpec,
                                      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                          SDL_AUDIO_ALLOW_SAMPLES_CHANGE);

  if (s_AudioDevice == 0) {
    std::cout << "[Audio] CRITICAL Error: " << SDL_GetError() << std::endl;
  } else {
    std::cout << "[Audio] Init Success | Device: " << s_AudioDevice << " | "
              << s_DeviceSpec.freq << "Hz | Ch: " << (int)s_DeviceSpec.channels
              << std::endl;
    SDL_PauseAudioDevice(s_AudioDevice, 0);
  }
}

void AudioEngine::Shutdown() {
  SDL_CloseAudioDevice(s_AudioDevice);
  s_SoundBank.clear();
}

void AudioEngine::LoadSound(const std::string &name,
                            const std::string &filepath) {
  if (s_SoundBank.find(name) != s_SoundBank.end())
    return;

  SDL_AudioSpec wavSpec;
  Uint32 wavLength;
  Uint8 *wavBuffer;

  std::cout << "[Audio] Loading: " << filepath << std::endl;

  if (SDL_LoadWAV(filepath.c_str(), &wavSpec, &wavBuffer, &wavLength) == NULL) {
    std::cout << "[Audio] ERR: Failed to load " << filepath << " -> "
              << SDL_GetError() << std::endl;
    return;
  }

  AudioSourceData finalData;
  SDL_AudioCVT cvt;
  int buildCVTResult = SDL_BuildAudioCVT(
      &cvt, wavSpec.format, wavSpec.channels, wavSpec.freq, s_DeviceSpec.format,
      s_DeviceSpec.channels, s_DeviceSpec.freq);

  if (buildCVTResult == -1) {
    std::cout << "[Audio] ERR: CVT Build Failed: " << SDL_GetError()
              << std::endl;
    SDL_FreeWAV(wavBuffer);
    return;
  } else if (buildCVTResult == 0) {
    finalData.buffer.assign(wavBuffer, wavBuffer + wavLength);
    finalData.spec = wavSpec;
  } else {
    cvt.len = wavLength;
    cvt.buf = (Uint8 *)SDL_malloc(cvt.len * cvt.len_mult);
    SDL_memcpy(cvt.buf, wavBuffer, wavLength);

    if (SDL_ConvertAudio(&cvt) == 0) {
      finalData.buffer.assign(cvt.buf, cvt.buf + cvt.len_cvt);
      finalData.spec = s_DeviceSpec;
    } else {
      std::cout << "[Audio] ERR: Conversion failed: " << SDL_GetError()
                << std::endl;
    }

    SDL_free(cvt.buf);
  }

  SDL_FreeWAV(wavBuffer);
  s_SoundBank[name] = finalData;
}

void AudioEngine::PlaySound(const std::string &name, float volume, bool loop,
                            bool spatial, float range, glm::vec3 pos) {
  if (s_SoundBank.find(name) == s_SoundBank.end()) {
    std::cout << "[Audio] On-demand loading: " << name << std::endl;
    LoadSound(name, name);
    if (s_SoundBank.find(name) == s_SoundBank.end())
      return;
  }

  AudioSourceData &data = s_SoundBank[name];
  ActiveVoice *voice = new ActiveVoice();
  voice->Name = name;
  voice->bufferData = data.buffer.data();
  voice->length = (uint32_t)data.buffer.size();
  voice->currentPos = 0;
  voice->volume = volume;
  voice->loop = loop;
  voice->isSpatial = spatial;
  voice->range = range;
  voice->position = pos;

  std::lock_guard<std::mutex> lock(s_Mutex);
  s_ActiveVoices.push_back(voice);
}

void AudioEngine::SetVoicePosition(const std::string &name,
                                   const glm::vec3 &pos) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  for (auto voice : s_ActiveVoices) {
    if (voice->Name == name) {
      voice->position = pos;
    }
  }
}

void AudioEngine::SetListenerTransform(const glm::vec3 &pos,
                                       const glm::vec3 &forward,
                                       const glm::vec3 &up) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  s_ListenerPos = pos;
  s_ListenerForward = glm::normalize(forward);
  s_ListenerUp = glm::normalize(up);
}

void AudioEngine::Update(float ts) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  for (auto it = s_ActiveVoices.begin(); it != s_ActiveVoices.end();) {
    if ((*it)->finished) {
      delete *it;
      it = s_ActiveVoices.erase(it);
    } else {
      ++it;
    }
  }
}

void AudioEngine::AudioCallback(void *userdata, uint8_t *stream, int len) {
  SDL_memset(stream, 0, len);

  int16_t *outputBuffer = (int16_t *)stream;
  int sampleCount = len / 2;

  std::lock_guard<std::mutex> lock(s_Mutex);

  glm::vec3 listenerPos = s_ListenerPos;
  glm::vec3 listenerFwd = s_ListenerForward;
  glm::vec3 listenerUp = s_ListenerUp;
  glm::vec3 listenerRight = glm::cross(listenerFwd, listenerUp);

  for (auto &voice : s_ActiveVoices) {
    if (voice->finished)
      continue;

    int bytesToMix = len;
    uint32_t bytesRemaining = voice->length - voice->currentPos;
    if ((uint32_t)bytesToMix > bytesRemaining)
      bytesToMix = (int)bytesRemaining;

    int16_t *sourceBuffer = (int16_t *)(voice->bufferData + voice->currentPos);
    int samplesToMix = bytesToMix / 2;

    float leftGain = 1.0f;
    float rightGain = 1.0f;

    if (voice->isSpatial) {
      float dist = glm::distance(listenerPos, voice->position);
      float maxDist = voice->range;
      float attenuation = 1.0f - (dist / maxDist);
      if (attenuation < 0.0f)
        attenuation = 0.0f;

      glm::vec3 dirToSource =
          (dist > 0.001f) ? glm::normalize(voice->position - listenerPos)
                          : listenerFwd;
      float pan = glm::dot(dirToSource, listenerRight);

      if (pan < -1.0f)
        pan = -1.0f;
      if (pan > 1.0f)
        pan = 1.0f;

      leftGain = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
      rightGain = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

      leftGain *= attenuation;
      rightGain *= attenuation;
    }

    leftGain *= voice->volume;
    rightGain *= voice->volume;

    for (int i = 0; i < samplesToMix; i += 2) {
      int32_t sampleL = sourceBuffer[i];
      int32_t outL = outputBuffer[i] + (int32_t)(sampleL * leftGain);
      if (outL > 32767)
        outL = 32767;
      if (outL < -32768)
        outL = -32768;
      outputBuffer[i] = (int16_t)outL;

      if (i + 1 < samplesToMix) {
        int32_t sampleR = sourceBuffer[i + 1];
        int32_t outR = outputBuffer[i + 1] + (int32_t)(sampleR * rightGain);
        if (outR > 32767)
          outR = 32767;
        if (outR < -32768)
          outR = -32768;
        outputBuffer[i + 1] = (int16_t)outR;
      }
    }

    voice->currentPos += bytesToMix;

    if (voice->currentPos >= voice->length) {
      if (voice->loop) {
        voice->currentPos = 0;
      } else {
        voice->finished = true;
      }
    }
  }
}

} // namespace Engine
