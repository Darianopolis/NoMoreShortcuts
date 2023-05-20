#include <iostream>
#include <vector>
#include <format>
#include <sstream>

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

struct Indexer
{
    wchar_t path[32767];
    char buffer[32767];
    WIN32_FIND_DATA result;
    size_t count = 0;

    template<class Fn>
    void Index(Fn&& fn)
    {
        wchar_t driveNames[1024];
        GetLogicalDriveStringsW(1023, driveNames);

        path[0] = L'\\';
        path[1] = L'\\';
        path[2] = L'?';
        path[3] = L'\\';

        for (wchar_t* drive = driveNames; *drive; drive += wcslen(drive) + 1)
        {
            std::wcout << L"Drive [" << drive << L"]\n";

            // Remove trailing '\' and convert to uppercase
            drive[wcslen(drive) - 1] = L'\0';
            drive = _wcsupr(drive);

            wcscpy(path + 4, drive);

            std::wcout << L"  searching [" << path << L"]\n";

            Search(wcslen(path), 0, std::forward<Fn>(fn));
        }
    }

    template<class Fn>
    void Search(size_t offset, uint32_t depth, Fn&& fn)
    {
        path[offset] = L'\\';
        path[offset + 1] = L'*';
        path[offset + 2] = L'\0';

        HANDLE findHandle = FindFirstFileEx(
            path,
            FindExInfoBasic,
            &result,
            FindExSearchNameMatch,
            nullptr,
            FIND_FIRST_EX_LARGE_FETCH);

        if (findHandle == INVALID_HANDLE_VALUE)
            return;

        do
        {
            size_t len = wcslen(result.cFileName);

            // Ignore empty, current "." and parent ".." directories
            if (len == 0 || (result.cFileName[0] == '.' && (len == 1 || (len == 2) && result.cFileName[1] == '.')))
                continue;

            wcscpy(path + offset + 1, result.cFileName);

            BOOL usedDefault = false;
            size_t utf8Len = WideCharToMultiByte(
                CP_UTF8,
                0,
                path + 4,
                int(offset - 3 + len),
                buffer,
                sizeof(buffer) - 1,
                nullptr,
                &usedDefault);

            buffer[utf8Len] = '\0';

            if (utf8Len == 0)
            {
                std::wcout << "Failed to convert " << result.cFileName << L"\n";
                continue;
            }

            if (++count % 10'000 == 0)
            {
                std::cout << "Files = " << count << ", path = " << buffer << '\n';
            }

            fn(buffer, depth + 1);

            if (result.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                memcpy(&path[offset + 1], result.cFileName, 2 * len);
                Search(offset + len + 1, depth + 1, std::forward<Fn>(fn));
            }
            else
            {
                // Clear path extension from conversion operation
                path[offset + 1] = L'*';
                path[offset + 2] = L'\0';
            }
        }
        while (FindNextFile(findHandle, &result));

        FindClose(findHandle);
    }
};

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

    struct File
    {
        std::string path;
        uint32_t depth;
        uint32_t uses;
    };

    std::vector<File> files;
    for (uint32_t i = 0; i < 4'000'000; ++i)
    {
        files.push_back({ std::format("C:/Files/blah/file{}.txt", i), 3, 0 });
    }

    std::vector<uint32_t> filtered;
    std::atomic_uint32_t filteredCount;
    filtered.resize(files.size());

    auto filter = [&](std::string query) {
        std::istringstream iss(query);
        std::string entry;
        std::vector<std::string> keywords;

        while (std::getline(iss, entry, ' '))
        {
            if (entry.size())
                keywords.push_back(entry);
        }

        filteredCount = 0;

        using namespace std::chrono;
        auto start = steady_clock::now();

#pragma omp parallel for
        for (uint32_t i = 0; i < files.size(); ++i)
        {
            auto& file = files[i];

            bool show = true;
            for (auto& keyword : keywords)
            {
                if (!file.path.contains(keyword))
                {
                    show = false;
                    break;
                }
            }

            if (show)
                filtered[filteredCount++] = i;
        }

        auto end = steady_clock::now();

        std::cout << "Filtered results: " << filteredCount << " in " << duration_cast<milliseconds>(end - start).count() << " ms\n";
    };

    filter("");

    auto sort = [&] {

    };

    bool running = true;
    bool focusInput = true;
    char query[512] = {};

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
                if (ImGui::BeginMenu("File"))
                {
                    bool selected = false;

                    if (ImGui::MenuItem("Index", nullptr, &selected))
                    {
                        Indexer indexer;
                        files.clear();
                        indexer.Index([&](const char* path, uint32_t depth) {
                            files.emplace_back(path, depth, 0);
                        });
                        filter(query);
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Shutdown", nullptr, &selected))
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

            if (focusInput)
            {
                ImGui::SetKeyboardFocusHere();
                focusInput = false;
            }

            if (ImGui::InputText("##InputSearchField", query, 511))
            {
                std::cout << "Processing query: " << query << '\n';

                filter(std::string(query));
            }

            ImGui::BeginChild("##InputSearchResults");

            ImGuiListClipper clipper;
            clipper.Begin(uint32_t(filteredCount));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    bool selected = false;
                    ImGui::Selectable(std::format(" - {}", files[filtered[i]].path).c_str(), selected);
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

        glfwShowWindow(window);
        glfwSetWindowShouldClose(window, GLFW_FALSE);

        focusInput = true;

        while (!glfwWindowShouldClose(window))
        {
            glfwWaitEvents();

            draw();
            draw();

            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);

            // if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            //     glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        glfwHideWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
}