#include "AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>

extern "C" {
#include "stb_vorbis.c"
}

namespace Engine {

SDL_AudioDeviceID AudioEngine::s_AudioDevice = 0;
SDL_AudioSpec AudioEngine::s_DeviceSpec;
std::map<std::string, AudioSourceData> AudioEngine::s_SoundBank;
std::vector<ActiveVoice *> AudioEngine::s_ActiveVoices;
std::mutex AudioEngine::s_Mutex;

glm::vec3 AudioEngine::s_ListenerPos       = {0, 0, 0};
glm::vec3 AudioEngine::s_ListenerForward   = {0, 0, -1};
glm::vec3 AudioEngine::s_ListenerUp        = {0, 1, 0};
glm::vec3 AudioEngine::s_ListenerPrevPos   = {0, 0, 0};
glm::vec3 AudioEngine::s_ListenerVelocity  = {0, 0, 0};
bool       AudioEngine::s_ListenerPrevPosValid = false;
float      AudioEngine::s_SpeedOfSound  = 343.0f;
float      AudioEngine::s_DopplerFactor = 1.0f;
float      AudioEngine::s_BusVolumes[(int)AudioBus::COUNT] = {1.0f, 1.0f, 1.0f, 1.0f};

void AudioEngine::Init() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
    std::cerr << "[Audio] SDL_InitSubSystem failed: " << SDL_GetError() << std::endl;
    return;
  }
  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = AudioCallback;
  want.userdata = nullptr;
  s_AudioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &s_DeviceSpec,
                                      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
                                          SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
  if (s_AudioDevice == 0) {
    std::cerr << "[Audio] OpenAudioDevice failed: " << SDL_GetError() << std::endl;
    return;
  }
  SDL_PauseAudioDevice(s_AudioDevice, 0);
  std::cout << "[Audio] Init Success | Device: " << s_AudioDevice
            << " | " << s_DeviceSpec.freq << "Hz | Ch: "
            << (int)s_DeviceSpec.channels << std::endl;
}

void AudioEngine::Shutdown() {
  if (s_AudioDevice) SDL_CloseAudioDevice(s_AudioDevice);
  s_SoundBank.clear();
  for (auto *v : s_ActiveVoices) delete v;
  s_ActiveVoices.clear();
}

static bool LoadOGGData(const std::string &filepath, SDL_AudioSpec &outSpec,
                        Uint32 &outLen, Uint8 *&outBuf) {
  int channels, sampleRate;
  short *output = nullptr;
  int samples = stb_vorbis_decode_filename(filepath.c_str(), &channels,
                                           &sampleRate, &output);
  if (samples <= 0 || !output) return false;
  outLen = (Uint32)(samples * channels * sizeof(short));
  outBuf = (Uint8 *)SDL_malloc(outLen);
  SDL_memcpy(outBuf, output, outLen);
  free(output);
  SDL_zero(outSpec);
  outSpec.freq = sampleRate;
  outSpec.format = AUDIO_S16SYS;
  outSpec.channels = (Uint8)channels;
  outSpec.samples = 4096;
  return true;
}

void AudioEngine::LoadSound(const std::string &name,
                            const std::string &filepath) {
  if (s_SoundBank.find(name) != s_SoundBank.end()) return;

  SDL_AudioSpec srcSpec;
  Uint32 srcLen = 0;
  Uint8 *srcBuf = nullptr;

  std::cout << "[Audio] Loading: " << filepath << std::endl;

  bool isOgg = filepath.size() >= 4 &&
               (filepath.substr(filepath.size() - 4) == ".ogg" ||
                filepath.substr(filepath.size() - 4) == ".OGG");
  bool freedAsWav = false;

  if (isOgg) {
    if (!LoadOGGData(filepath, srcSpec, srcLen, srcBuf)) {
      std::cerr << "[Audio] OGG decode failed: " << filepath << std::endl;
      return;
    }
  } else {
    if (SDL_LoadWAV(filepath.c_str(), &srcSpec, &srcBuf, &srcLen) == nullptr) {
      std::cerr << "[Audio] WAV load failed: " << SDL_GetError() << std::endl;
      return;
    }
    freedAsWav = true;
  }

  AudioSourceData finalData;
  SDL_AudioCVT cvt;
  int rc = SDL_BuildAudioCVT(&cvt, srcSpec.format, srcSpec.channels, srcSpec.freq,
                             s_DeviceSpec.format, s_DeviceSpec.channels,
                             s_DeviceSpec.freq);
  if (rc == -1) {
    std::cerr << "[Audio] BuildAudioCVT failed: " << SDL_GetError() << std::endl;
    if (freedAsWav) SDL_FreeWAV(srcBuf); else SDL_free(srcBuf);
    return;
  } else if (rc == 0) {
    finalData.buffer.assign(srcBuf, srcBuf + srcLen);
    finalData.spec = srcSpec;
  } else {
    cvt.len = srcLen;
    cvt.buf = (Uint8 *)SDL_malloc(cvt.len * cvt.len_mult);
    SDL_memcpy(cvt.buf, srcBuf, srcLen);
    if (SDL_ConvertAudio(&cvt) == 0) {
      finalData.buffer.assign(cvt.buf, cvt.buf + cvt.len_cvt);
      finalData.spec = s_DeviceSpec;
    } else {
      std::cerr << "[Audio] ConvertAudio failed: " << SDL_GetError() << std::endl;
    }
    SDL_free(cvt.buf);
  }

  if (freedAsWav) SDL_FreeWAV(srcBuf);
  else            SDL_free(srcBuf);
  s_SoundBank[name] = finalData;
}

void AudioEngine::PlaySound(const std::string &name, float volume, bool loop,
                            bool spatial, float range, glm::vec3 pos,
                            AudioBus bus) {
  if (s_SoundBank.find(name) == s_SoundBank.end()) {
    LoadSound(name, name);
    if (s_SoundBank.find(name) == s_SoundBank.end()) return;
  }
  AudioSourceData &data = s_SoundBank[name];
  ActiveVoice *voice = new ActiveVoice();
  voice->Name = name;
  voice->bufferData = data.buffer.data();
  voice->length = (uint32_t)data.buffer.size();
  voice->currentPos = 0;
  voice->sampleCursorFrames = 0.0;
  voice->volume = volume;
  voice->loop = loop;
  voice->isSpatial = spatial;
  voice->range = range;
  voice->position = pos;
  voice->previousPosition = pos;
  voice->previousPositionValid = false;
  voice->velocity = {0, 0, 0};
  voice->pitch = 1.0f;
  voice->Bus = bus;

  std::lock_guard<std::mutex> lock(s_Mutex);
  s_ActiveVoices.push_back(voice);
}

void AudioEngine::SetVoicePosition(const std::string &name, const glm::vec3 &pos) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  for (auto *v : s_ActiveVoices) if (v->Name == name) v->position = pos;
}
void AudioEngine::SetListenerTransform(const glm::vec3 &pos, const glm::vec3 &fwd,
                                       const glm::vec3 &up) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  s_ListenerPos = pos;
  s_ListenerForward = fwd;
  s_ListenerUp = up;
}
void AudioEngine::SetListenerVelocity(const glm::vec3 &v) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  s_ListenerVelocity = v;
}
void AudioEngine::SetVoicePitch(const std::string &name, float pitch) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  for (auto *v : s_ActiveVoices) if (v->Name == name) v->pitch = pitch;
}
void AudioEngine::SetSpeedOfSound(float c)  { std::lock_guard<std::mutex> lock(s_Mutex); s_SpeedOfSound = c; }
void AudioEngine::SetDopplerFactor(float f) { std::lock_guard<std::mutex> lock(s_Mutex); s_DopplerFactor = f; }
void AudioEngine::StopAllVoices() {
  std::lock_guard<std::mutex> lock(s_Mutex);
  for (auto *v : s_ActiveVoices) delete v;
  s_ActiveVoices.clear();
}
void AudioEngine::SetBusVolume(AudioBus bus, float vol) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  s_BusVolumes[(int)bus] = std::max(0.0f, std::min(1.0f, vol));
}
float AudioEngine::GetBusVolume(AudioBus bus) {
  std::lock_guard<std::mutex> lock(s_Mutex);
  return s_BusVolumes[(int)bus];
}

void AudioEngine::Update(float ts) {
  std::lock_guard<std::mutex> lock(s_Mutex);

  if (s_ListenerPrevPosValid && ts > 0.0f) {
    s_ListenerVelocity = (s_ListenerPos - s_ListenerPrevPos) / ts;
  } else {
    s_ListenerVelocity = {0, 0, 0};
  }
  s_ListenerPrevPos = s_ListenerPos;
  s_ListenerPrevPosValid = true;

  for (auto *v : s_ActiveVoices) {
    if (v->previousPositionValid && ts > 0.0f) {
      v->velocity = (v->position - v->previousPosition) / ts;
    } else {
      v->velocity = {0, 0, 0};
    }
    v->previousPosition = v->position;
    v->previousPositionValid = true;
  }

  for (auto it = s_ActiveVoices.begin(); it != s_ActiveVoices.end();) {
    if ((*it)->finished) {
      delete *it;
      it = s_ActiveVoices.erase(it);
    } else {
      ++it;
    }
  }
}

void AudioEngine::AudioCallback(void * , uint8_t *stream, int len) {
  SDL_memset(stream, 0, len);
  std::lock_guard<std::mutex> lock(s_Mutex);
  if (s_DeviceSpec.format != AUDIO_S16SYS) return;

  int16_t *out = (int16_t *)stream;
  int totalSamples = len / sizeof(int16_t);
  int frames = totalSamples / s_DeviceSpec.channels;

  for (auto *voice : s_ActiveVoices) {
    if (voice->finished) continue;
    int16_t *src = (int16_t *)voice->bufferData;
    int srcLenSamples = voice->length / sizeof(int16_t);
    int srcFrames = srcLenSamples / s_DeviceSpec.channels;

    glm::vec3 toListener = s_ListenerPos - voice->position;
    float dist = glm::length(toListener);
    if (dist > 0.0001f) toListener /= dist;
    float vListener = glm::dot(s_ListenerVelocity, -toListener);
    float vSource   = glm::dot(voice->velocity,    -toListener);
    float doppler = (s_SpeedOfSound + vListener * s_DopplerFactor) /
                    std::max(0.001f, s_SpeedOfSound + vSource * s_DopplerFactor);
    doppler = glm::clamp(doppler, 0.25f, 4.0f);
    double playbackSpeed = doppler * voice->pitch;

    float leftGain = 1.0f, rightGain = 1.0f;
    if (voice->isSpatial) {
      float minDist = 1.0f;
      float d = std::max(dist, minDist);
      float attenuation = minDist / d;
      if (dist > voice->range) attenuation = 0.0f;

      glm::vec3 right = glm::cross(s_ListenerForward, s_ListenerUp);
      float pan = glm::clamp(glm::dot(toListener, right) * -1.0f, -1.0f, 1.0f);
      leftGain  = std::sqrt(0.5f * (1.0f - pan));
      rightGain = std::sqrt(0.5f * (1.0f + pan));

      float frontDot = glm::dot(toListener, -s_ListenerForward);
      float frontBackGain = 0.7f + 0.3f * frontDot;
      float busVol = s_BusVolumes[(int)voice->Bus] * s_BusVolumes[(int)AudioBus::Master];
      leftGain  *= attenuation * frontBackGain * voice->volume * busVol;
      rightGain *= attenuation * frontBackGain * voice->volume * busVol;
    } else {
      float busVol = s_BusVolumes[(int)voice->Bus] * s_BusVolumes[(int)AudioBus::Master];
      leftGain  = voice->volume * busVol;
      rightGain = voice->volume * busVol;
    }

    for (int i = 0; i < frames; ++i) {
      double f = voice->sampleCursorFrames;
      int f0 = (int)f;
      int f1 = f0 + 1;
      double t = f - f0;
      if (voice->loop) {
        f0 %= srcFrames;
        f1 %= srcFrames;
      } else if (f1 >= srcFrames) {
        voice->finished = true;
        break;
      }
      int idx0 = f0 * s_DeviceSpec.channels;
      int idx1 = f1 * s_DeviceSpec.channels;
      int s0 = src[idx0];
      int s1 = src[idx1];
      int sample = (int)((1.0 - t) * s0 + t * s1);

      int outL = out[i * 2 + 0] + (int)(sample * leftGain);
      int outR = out[i * 2 + 1] + (int)(sample * rightGain);
      out[i * 2 + 0] = (int16_t)glm::clamp(outL, -32768, 32767);
      out[i * 2 + 1] = (int16_t)glm::clamp(outR, -32768, 32767);

      voice->sampleCursorFrames += playbackSpeed;
      if (voice->loop) {
        if (voice->sampleCursorFrames >= srcFrames) voice->sampleCursorFrames -= srcFrames;
      }
    }
    voice->currentPos = (uint32_t)(voice->sampleCursorFrames * s_DeviceSpec.channels * sizeof(int16_t));
  }
}

}
