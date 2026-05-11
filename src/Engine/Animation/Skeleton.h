#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace Engine {

struct Joint {
  std::string Name;
  int Parent = -1;
  glm::mat4 InverseBind{1.0f};
  glm::mat4 LocalBind{1.0f};
  std::vector<int> Children;
};

struct AnimationChannel {
  enum class Path { Translation, Rotation, Scale };
  int JointIndex = -1;
  Path TargetPath = Path::Translation;
  std::vector<float>     Times;
  std::vector<glm::vec3> Vec3Values;
  std::vector<glm::quat> QuatValues;
};

struct AnimationClip {
  std::string Name;
  float Duration = 0.0f;
  std::vector<AnimationChannel> Channels;
};

struct Skeleton {
  std::vector<Joint> Joints;
  std::vector<AnimationClip> Animations;

  std::vector<glm::mat4> JointMatrices;
  bool IsValid() const { return !Joints.empty(); }
};

void SampleAnimation(const Skeleton &skel, const AnimationClip &clip,
                     float t, bool loop,
                     std::vector<glm::mat4> &outJointMatrices);

}
