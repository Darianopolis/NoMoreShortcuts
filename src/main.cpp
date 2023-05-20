#include <iostream>
#include <vector>
#include <format>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <glad/glad.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_glfw.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window = glfwCreateWindow(1920, 1200, "No More Shortcuts", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    {
        // Set Icon

        int channels;
        GLFWimage image;
        image.pixels = stbi_load("IconVector.png", &image.width, &image.height, &channels, STBI_rgb_alpha);
        glfwSetWindowIcon(window, 1, &image);
        stbi_image_free(image.pixels);
    }

    {
        // Center window

        int count = 0;
        auto monitor = glfwGetVideoMode(glfwGetMonitors(&count)[0]);
        int w, h;
        glfwGetWindowSize(window, &w, &h);
        glfwSetWindowPos(window, (monitor->width - w) / 2, (monitor->height - h) / 2);
    }

    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    ImGui::GetStyle().ScaleAllSizes(1.5f);
    auto fontConfig = ImFontConfig();
    fontConfig.GlyphOffset = ImVec2(1.f, 1.67f);
    ImGui::GetIO().Fonts->ClearFonts();
    ImGui::GetIO().Fonts->AddFontFromFileTTF("CONSOLA.TTF", 20, &fontConfig);
    ImGui_ImplOpenGL3_CreateFontsTexture();

    char query[512] = {};

    std::vector<std::string> files;
    for (uint32_t i = 0; i < 4'000'000; ++i)
    {
        files.push_back(std::format("C:/Files/blah/file{}.txt", i));
    }

    bool running = true;

    auto draw = [&] {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiDockNodeFlags dockspaceFlags = 0;
            ImGuiWindowFlags windowFlags = 0;

            // TODO: More configuration

            // Make dockspace fullscreen
            auto viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
            windowFlags |= ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_NoNavFocus
                | ImGuiWindowFlags_MenuBar;

            // Pass through background
            windowFlags |= ImGuiWindowFlags_NoBackground;
            dockspaceFlags |= ImGuiDockNodeFlags_PassthruCentralNode;

            // Register dockspace
            bool visible = true;
            ImGui::Begin("Dockspace", &visible, windowFlags);
            ImGui::PopStyleVar(3);
            ImGui::DockSpace(ImGui::GetID("DockspaceID"), ImVec2(0.f, 0.f), dockspaceFlags);

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("No More Shortcuts"))
                {
                    bool selected = false;
                    if (ImGui::MenuItem("Exit", nullptr, &selected))
                    {
                        glfwSetWindowShouldClose(window, GLFW_TRUE);
                        running = false;
                    }

                    ImGui::EndMenu();
                }

                ImGui::EndMenuBar();
            }

            ImGui::End();
        }

        ImGui::ShowDemoWindow();

        {
            ImGui::Begin("Search");

            if (ImGui::InputText("##", query, 511))
            {
                std::cout << "Processing query: " << query << '\n';
            }

            ImGui::BeginChild("##");

            ImGuiListClipper clipper;
            clipper.Begin(uint32_t(files.size()));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    bool selected = false;
                    ImGui::Selectable(std::format(" - {}", files[i]).c_str(), selected);
                }
            }

            ImGui::EndChild();

            ImGui::End();
        }

        ImGui::Render();
    };

    int hotkeyID = 1;
    RegisterHotKey(glfwGetWin32Window(window), hotkeyID, MOD_CONTROL | MOD_SHIFT, VK_SPACE);

    while (running)
    {
        MSG msg { 0 };
        while (GetMessage(&msg, glfwGetWin32Window(window), 0, 0) && msg.message != WM_HOTKEY);

        std::cout << "Hotkey pressed! Showing...\n";
        glfwShowWindow(window);
        glfwSetWindowShouldClose(window, GLFW_FALSE);

        while (!glfwWindowShouldClose(window))
        {
            glfwWaitEvents();

            draw();
            draw();

            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        glfwHideWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}