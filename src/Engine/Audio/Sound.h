#pragma once

#include "../Core/EngineAPI.h"
#include <glm/glm.hpp>
#include <string>

namespace Engine {

// One-shot sounds for game scripts (.wav / .ogg, path relative to the project
// root). The file is loaded the first time it is played.
ENGINE_API void PlayOneShot(const std::string &filepath, float volume = 1.0f);

// Same, positioned in the world: fades out towards `range` metres from the
// listener (the primary camera).
ENGINE_API void PlayOneShotAt(const std::string &filepath, const glm::vec3 &position,
                              float volume = 1.0f, float range = 20.0f);

// Looping sounds (music, ambience): one voice per file. Calling PlayLoop for a
// file that is already looping only updates its volume (and, for PlayLoopAt,
// its position), so scripts can call these every frame to fade sounds in and
// out. Music goes on the Music bus, positioned loops on the SFX bus.
ENGINE_API void PlayLoop(const std::string &filepath, float volume = 1.0f);
ENGINE_API void PlayLoopAt(const std::string &filepath, const glm::vec3 &position,
                           float volume = 1.0f, float range = 20.0f);
ENGINE_API void StopLoop(const std::string &filepath);

// Decodes a file ahead of time, so the first play does not stall a frame.
ENGINE_API void PreloadSound(const std::string &filepath);

// How much spatial sounds bend in pitch when they or the listener move
// (1 = physical Doppler, 0 = off).
ENGINE_API void SetDoppler(float factor);

// The listener's real velocity (the player's body), for the Doppler shift.
// Without it the engine guesses from how the camera moves, which also counts
// the camera swinging when it turns. Call it every frame.
ENGINE_API void SetListenerVelocity(const glm::vec3 &velocity);

}
