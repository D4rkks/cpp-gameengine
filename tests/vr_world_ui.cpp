#include "../src/Engine/VR/VRUIPlacement.h"
#include <cassert>
#include <cmath>
#include <iostream>

static glm::vec3 Point(const glm::mat4& m, glm::vec2 p) { return glm::vec3(m*glm::vec4(p,0,1)); }
static float ScreenX(const glm::mat4& m, glm::vec3 p) {
    glm::vec4 clip=m*glm::vec4(p,1); return clip.x/clip.w;
}
int main() {
    const glm::vec3 viewer(0,1.6f,0), anchor(0,1.1f,-1.5f);
    // Moving the drawing origin (as the head turns) must never move the label.
    auto a=Engine::WorldUITransform(anchor,viewer,{1024,576},.008f/3);
    auto b=Engine::WorldUITransform(anchor,viewer,{-840,2900},.008f/3);
    assert(glm::distance(Point(a,{1024,576}),anchor)<.00001f);
    assert(glm::distance(Point(a,{1084,546}),Point(b,{-780,2870}))<.00001f);
    // A 40-pixel progress bar is 32 cm wide at every viewing distance.
    assert(std::abs(glm::distance(Point(a,{964,576}),Point(a,{1084,576}))-.32f)<.00001f);
    auto far=Engine::WorldUITransform(anchor,viewer+glm::vec3(0,0,5),{0,0},.008f/3);
    assert(std::abs(glm::distance(Point(far,{-60,0}),Point(far,{60,0}))-.32f)<.00001f);
    auto projection=glm::perspective(glm::radians(90.f),1.f,.05f,1000.f);
    auto left=projection*glm::lookAt(viewer+glm::vec3(-.032f,0,0),viewer+glm::vec3(-.032f,0,-1),glm::vec3(0,1,0));
    auto right=projection*glm::lookAt(viewer+glm::vec3(.032f,0,0),viewer+glm::vec3(.032f,0,-1),glm::vec3(0,1,0));
    assert(ScreenX(left,anchor)>ScreenX(right,anchor)); // Real binocular parallax.
    auto side=Engine::WorldUITransform(anchor,anchor+glm::vec3(1,1,0),{0,0},.01f);
    assert(glm::dot(glm::normalize(glm::vec3(side[2])),glm::vec3(1,0,0))>.999f);
    auto overhead=Engine::WorldUITransform(anchor,anchor+glm::vec3(0,1,0),{0,0},.01f);
    for (int c=0;c<4;++c) for(int r=0;r<4;++r) assert(std::isfinite(overhead[c][r]));
    std::cout << "VR world UI: stable anchors, physical scale, upright billboards and stereo parallax passed.\n";
}
