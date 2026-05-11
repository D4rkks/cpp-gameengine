#include "Skeleton.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Engine {

namespace {

int FindKeyframe(const std::vector<float> &times, float t) {
  if (times.empty()) return 0;
  if (t <= times.front()) return 0;
  if (t >= times.back())  return (int)times.size() - 1;

  for (int i = 0; i + 1 < (int)times.size(); ++i) {
    if (t >= times[i] && t <= times[i + 1]) return i;
  }
  return (int)times.size() - 2;
}

glm::vec3 SampleVec3(const std::vector<float> &times,
                     const std::vector<glm::vec3> &values, float t) {
  if (values.empty()) return glm::vec3(0.0f);
  if (values.size() == 1) return values[0];
  int i = FindKeyframe(times, t);
  if (i + 1 >= (int)values.size()) return values.back();
  float dt = times[i + 1] - times[i];
  float a  = dt > 0 ? glm::clamp((t - times[i]) / dt, 0.0f, 1.0f) : 0.0f;
  return glm::mix(values[i], values[i + 1], a);
}

glm::quat SampleQuat(const std::vector<float> &times,
                     const std::vector<glm::quat> &values, float t) {
  if (values.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  if (values.size() == 1) return values[0];
  int i = FindKeyframe(times, t);
  if (i + 1 >= (int)values.size()) return values.back();
  float dt = times[i + 1] - times[i];
  float a  = dt > 0 ? glm::clamp((t - times[i]) / dt, 0.0f, 1.0f) : 0.0f;
  return glm::slerp(values[i], values[i + 1], a);
}

glm::mat4 ComposeTRS(const glm::vec3 &t, const glm::quat &r, const glm::vec3 &s) {
  glm::mat4 T = glm::translate(glm::mat4(1.0f), t);
  glm::mat4 R = glm::mat4_cast(r);
  glm::mat4 S = glm::scale(glm::mat4(1.0f), s);
  return T * R * S;
}

}

void SampleAnimation(const Skeleton &skel, const AnimationClip &clip,
                     float t, bool loop,
                     std::vector<glm::mat4> &outJointMatrices) {
  const size_t N = skel.Joints.size();
  outJointMatrices.assign(N, glm::mat4(1.0f));
  if (N == 0) return;

  if (clip.Duration > 0.0f) {
    if (loop) {
      t = std::fmod(t, clip.Duration);
      if (t < 0.0f) t += clip.Duration;
    } else {
      t = glm::clamp(t, 0.0f, clip.Duration);
    }
  }

  std::vector<glm::vec3> localT(N, glm::vec3(0.0f));
  std::vector<glm::quat> localR(N, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
  std::vector<glm::vec3> localS(N, glm::vec3(1.0f));

  for (size_t j = 0; j < N; ++j) {
    const glm::mat4 &m = skel.Joints[j].LocalBind;
    localT[j] = glm::vec3(m[3]);
    glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
    glm::vec3 sc(glm::length(c0), glm::length(c1), glm::length(c2));
    if (sc.x > 0) c0 /= sc.x;
    if (sc.y > 0) c1 /= sc.y;
    if (sc.z > 0) c2 /= sc.z;
    glm::mat3 rot(c0, c1, c2);
    localR[j] = glm::quat_cast(rot);
    localS[j] = sc;
  }

  for (const auto &ch : clip.Channels) {
    if (ch.JointIndex < 0 || ch.JointIndex >= (int)N) continue;
    switch (ch.TargetPath) {
      case AnimationChannel::Path::Translation:
        localT[ch.JointIndex] = SampleVec3(ch.Times, ch.Vec3Values, t);
        break;
      case AnimationChannel::Path::Rotation:
        localR[ch.JointIndex] = SampleQuat(ch.Times, ch.QuatValues, t);
        break;
      case AnimationChannel::Path::Scale:
        localS[ch.JointIndex] = SampleVec3(ch.Times, ch.Vec3Values, t);
        break;
    }
  }

  std::vector<glm::mat4> globals(N, glm::mat4(1.0f));
  for (size_t j = 0; j < N; ++j) {
    glm::mat4 local = ComposeTRS(localT[j], localR[j], localS[j]);
    int p = skel.Joints[j].Parent;
    if (p >= 0 && p < (int)j) globals[j] = globals[p] * local;
    else                       globals[j] = local;
  }

  for (size_t j = 0; j < N; ++j) {
    outJointMatrices[j] = globals[j] * skel.Joints[j].InverseBind;
  }
}

}
