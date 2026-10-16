// Render the real world-UI path into a hidden OpenGL window, without a headset.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <GL/glew.h>
#include <imgui.h>
#include "../src/Engine/VR/VRSystem.h"
#include "../src/Engine/Window/Window.h"
#include "../src/Engine/Renderer/Buffer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include <iostream>
#include <vector>

int main() {
    // Initialize GLEW inside the engine DLL as well as this test executable.
    Engine::WindowProps props("World UI test",256,256);props.VR=true;
    Engine::Window window(props);SDL_HideWindow(window.GetNativeWindow());
    glewExperimental=GL_TRUE; assert(glewInit()==GLEW_OK);
    while(glGetError()!=GL_NO_ERROR) {}
    {
        GLuint vao=0, existing=0;
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);
        glGenBuffers(1,&existing);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,existing);
        uint32_t indices[]={0,1,2};
        Engine::IndexBuffer uploaded(indices,3);
        GLint binding=0;glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&binding);
        assert((GLuint)binding==existing); // Loading another mesh preserves this VAO.
        uploaded.Bind();glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&binding);
        assert((GLuint)binding!=existing && binding!=0);
        glBindVertexArray(0);
        Engine::IndexBuffer noVAO(indices,3); // Upload also works with no VAO bound.
        assert(glGetError()==GL_NO_ERROR);
        glDeleteBuffers(1,&existing);glDeleteVertexArrays(1,&vao);
    }
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={256,256};io.DeltaTime=1.f/90;
    unsigned char* pixels; int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    GLuint white=0;glGenTextures(1,&white);glBindTexture(GL_TEXTURE_2D,white);
    const unsigned char rgba[4]={255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    ImGui::NewFrame();
    {
        Engine::VRSystem vr;
        vr.BeginWorldUIFrame();
        auto* dl=vr.GetWorldUIDrawList();
        dl->PushTexture(ImTextureRef((ImTextureID)white));
        vr.SetWorldUIAnchor({0,0,-2},{128,128},.01f);
        dl->AddRectFilled({108,124},{148,132},IM_COL32(0,255,0,255));
        dl->PopTexture();
        dl->AddText({108,140},IM_COL32(255,255,255,255),"Stereo label");
        const auto projection=glm::perspective(glm::radians(90.f),1.f,.1f,10.f);
        auto draw=[&](float eye,float depth) {
            glViewport(0,0,256,256);glDisable(GL_SCISSOR_TEST);glDepthMask(GL_TRUE);
            glClearColor(0,0,0,1);glClearDepth(depth);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            vr.RenderWorldUI(projection*glm::translate(glm::mat4(1),{-eye,0,0}));
            std::vector<unsigned char> frame(256*256*4);
            glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,frame.data());
            double centre=0;int count=0;
            for(int y=0;y<256;++y)for(int x=0;x<256;++x)
                if(frame[(y*256+x)*4+1]>200 && frame[(y*256+x)*4]<20){centre+=x;++count;}
            return std::pair<int,double>{count,count?centre/count:0};
        };
        const auto left=draw(-.032f,1),right=draw(.032f,1);
        assert(left.first>80 && right.first>80);
        assert(left.second>right.second+2); // Distinct stereo positions, not a screen overlay.
        assert(draw(0,.9f).first==0); // A nearer wall completely hides the bar.
        assert(draw(0,1).first>80); // Visible again after the wall is removed.
        assert(glGetError()==GL_NO_ERROR);
    }
    ImGui::EndFrame();
    glDeleteTextures(1,&white);
    std::cout<<"Real OpenGL world UI: visible pixels, stereo parallax and wall occlusion passed.\n";
}
