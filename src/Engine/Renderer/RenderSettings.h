#pragma once

#include "../Core/EngineAPI.h"
#include <glm/glm.hpp>

namespace Engine {

// Render look that game scripts can drive. The standalone runtime has no
// "Rendering" panel, so this is the only way a game can set its post-process.
struct ENGINE_API RenderSettings {
  // Optional stereo look; flat menus and desktop rendering keep their own grade.
  struct StereoLook {
    bool Enabled = false;
    bool LinearMaterialColors = true;
    bool ContactAO = true;
    float Exposure = 1.1f;
    float Contrast = 1.0f;
    float Saturation = 1.04f;
    float Temperature = 0.0f;
    float Outline = 0.0f;
    float BloomStrength = 0.025f;
    float AORadius = 0.18f;
    float AOStrength = 0.32f;
    float AOBias = 0.012f;
    float AmbientSky = 1.25f;
    float AmbientGround = 0.65f;
    float MinRoughness = 0.22f;
  } VR;
  // --- Retro look (low-res pixels + limited palette). Read every frame. ---
  float PixelHeight = 0.0f;  // virtual vertical resolution; 0 = native
  float ColorLevels = 0.0f;  // posterize levels per channel; 0 = off
  float Dither      = 0.0f;  // 0..1 ordered-dither strength
  float Outline     = 0.0f;  // 0..1 depth-edge outline strength
  bool  FogAffectsSky = true; // false keeps the sky clear and only fogs geometry

  // --- Post-process values. Copied into the renderer once when ApplyPost is
  //     set, so the editor panel can still tweak them afterwards. The editor
  //     sets RestorePost when Play stops to get its own values back. ---
  bool  ApplyPost = false;
  bool  RestorePost = false;
  float Exposure = 0.75f;
  bool  AutoExposure = true;
  float BloomStrength = 0.08f;
  float BloomThreshold = 1.2f;
  float Vignette = 0.3f;
  float FilmGrain = 0.0f;
  float ChromaticAberration = 0.0f;
  float FogDensity = 0.0f;
  glm::vec3 FogColor = {0.6f, 0.65f, 0.72f};
  float FogStart = 15.0f;
  float Contrast = 1.0f;
  float Saturation = 1.0f;
  float Temperature = 0.0f;
  bool  SSAO = true;
  bool  SSR = true;
  glm::vec3 SkyTop = {0.15f, 0.32f, 0.70f};
  glm::vec3 SkyHorizon = {0.78f, 0.85f, 0.95f};
  glm::vec3 SkyGround = {0.15f, 0.14f, 0.12f};

  static RenderSettings &Get();
};

}
