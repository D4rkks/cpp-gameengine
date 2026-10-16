#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Engine {
// UI artwork uses screen pixels (+Y down); the world uses metres (+Y up).
inline glm::mat4 WorldUITransform(const glm::vec3& position, const glm::vec3& viewer,
                                  const glm::vec2& pixelOrigin, float metersPerPixel) {
    glm::vec3 back = viewer - position;
    back.y = 0;
    back = glm::length(back) > .001f ? glm::normalize(back) : glm::vec3(0,0,1);
    glm::vec3 right(back.z, 0, -back.x);
    glm::mat4 model(1);
    model[0] = glm::vec4(right * metersPerPixel, 0);
    model[1] = glm::vec4(0, -metersPerPixel, 0, 0);
    model[2] = glm::vec4(back, 0);
    model[3] = glm::vec4(position - glm::vec3(model[0]) * pixelOrigin.x - glm::vec3(model[1]) * pixelOrigin.y, 1);
    return model;
}
}
