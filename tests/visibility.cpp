#include "../src/Engine/Renderer/Visibility.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <random>
#include <iostream>

static bool Reference(const glm::mat4& matrix,const glm::vec3& low,const glm::vec3& high) {
  int outside[6]{};
  for(int k=0;k<8;++k) {
    glm::vec4 p=matrix*glm::vec4(k&1?high.x:low.x,k&2?high.y:low.y,k&4?high.z:low.z,1);
    for(int a=0;a<3;++a) {outside[a*2]+=p[a]<-p.w;outside[a*2+1]+=p[a]>p.w;}
  }
  for(int n:outside)if(n==8)return false;
  return true;
}
int main() {
  std::mt19937 random(2026);std::uniform_real_distribution<float> value(-15,15),positive(.1f,4);
  auto perspective=glm::perspective(glm::radians(87.0f),1.0f,.05f,1000.0f);
  auto ortho=glm::ortho(-10.0f,10.0f,-10.0f,10.0f,.1f,40.0f);
  glm::vec3 low(-1),high(1);
  assert(Engine::BoundsVisible(perspective*glm::translate(glm::mat4(1),{0,0,-3}),low,high));
  assert(!Engine::BoundsVisible(perspective*glm::translate(glm::mat4(1),{0,0,3}),low,high));
  for(int i=0;i<5000;++i) {
    auto model=glm::translate(glm::mat4(1),{value(random),value(random),value(random)})*
      glm::rotate(glm::mat4(1),value(random),glm::normalize(glm::vec3(.3f,.5f,.7f)))*
      glm::scale(glm::mat4(1),{positive(random),positive(random),positive(random)});
    for(const auto& projection:{perspective,ortho}) {
      auto matrix=projection*model;
      // Tolerance is intentionally conservative at a plane boundary.
      assert(!Reference(matrix,low,high)||Engine::BoundsVisible(matrix,low,high));
    }
  }
  std::cout<<"Visibility: perspective/orthographic, rotation, nonuniform scale and near plane passed.\n";
}
