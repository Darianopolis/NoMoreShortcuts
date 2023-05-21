#include <iostream>
#include <vector>
#include <format>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <execution>
#include <future>
#include <filesystem>
#include <regex>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <combaseapi.h>

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
    static void Index(Fn&& fn)
    {
        wchar_t driveNames[1024];
        GetLogicalDriveStringsW(1023, driveNames);

        std::vector<wchar_t*> drives;
        for (wchar_t* drive = driveNames; *drive; drive += wcslen(drive) + 1)
            drives.push_back(drive);

#pragma omp parallel for
        for (uint32_t i = 0; i < drives.size(); ++i)
        {
            auto drive = drives[i];

            std::wcout << L"Drive [" << drive << L"]\n";

            // Remove trailing '\' and convert to uppercase
            drive[wcslen(drive) - 1] = L'\0';
            drive = _wcsupr(drive);

            Indexer indexer;
            indexer.path[0] = L'\\';
            indexer.path[1] = L'\\';
            indexer.path[2] = L'?';
            indexer.path[3] = L'\\';

            wcscpy(indexer.path + 4, drive);

            std::wcout << L"  searching [" << indexer.path << L"]\n";

            indexer.Search(wcslen(indexer.path), 0, std::forward<Fn>(fn));
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
                std::cout << std::format("Files = {}, path = {}\n", count, buffer);
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

bool ContainsCI(std::string_view value, std::string_view needle)
{
    auto comp = [&](size_t index, char c) {
        return std::tolower(value[index++]) == c;
    };

    const size_t nSize = needle.size();
    const size_t vSize = value.size();

    if (nSize > vSize)
        return false;

    if (vSize == 0)
        return true;

    const char first = needle[0];
    const size_t max = vSize - nSize;

    for (size_t i = 0; i <= max; ++i) {
        if (!comp(i, first)) {
            while (++i <= max && !comp(i, first));
        }

        if (i <= max) {
            size_t j = i + 1;
            const size_t trueEnd = j + nSize - 1;
            const size_t end = (vSize > trueEnd) ? trueEnd : vSize;
            for (size_t k = 1; j < end && comp(j, needle[k]); ++j, ++k);
            if (j == trueEnd)
                return true;
        }
    }

    return false;
}

int main()
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

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
        uint32_t uses = 0;
    };

    std::vector<File> files;

    std::vector<uint32_t> filtered;
    std::atomic_uint32_t filteredCount;
    filtered.resize(files.size());

    auto filter = [&](std::string query) {
        std::istringstream iss(query);
        std::string entry;
        std::vector<std::string> keywords;

        while (std::getline(iss, entry, ' '))
        {
            std::transform(entry.begin(), entry.end(), entry.begin(), [](char c) { return char(std::tolower(c)); });
            if (entry.size())
                keywords.push_back(entry);
        }

        filteredCount = 0;

        using namespace std::chrono;
        auto start = steady_clock::now();

        filtered.resize(files.size());

#pragma omp parallel for
        for (uint32_t i = 0; i < files.size(); ++i)
        {
            auto& file = files[i];

            bool show = true;
            for (auto& keyword : keywords)
            {
                if (!ContainsCI(file.path, keyword))
                {
                    show = false;
                    break;
                }
            }

            if (show)
                filtered[filteredCount++] = i;
        }

        std::sort(std::execution::par_unseq, filtered.begin(), filtered.begin() + filteredCount, [](auto l, auto r) {
            return l <= r;
        });

        auto end = steady_clock::now();

        std::cout << "Filtered results: " << filteredCount << " in " << duration_cast<milliseconds>(end - start).count() << " ms\n";
    };

    auto save = [&] {
        std::ofstream out("index.txt", std::ios::binary);
        for (auto& file : files)
        {
            out << file.uses;
            out << ' ';
            out << file.depth;
            out << ' ';
            out << file.path;
            out << '\n';
        }
    };

    auto load = [&] {
        std::ifstream fin("index.txt", std::ios::binary | std::ios::ate);
        std::string contents;
        contents.resize(fin.tellg());
        fin.seekg(0);
        fin.read(contents.data(), contents.size());
        fin.close();

        std::cout << "Loaded contents\n";

        std::istringstream in(contents);

        files.clear();

        while (!in.eof())
        {
            uint32_t uses, depth;
            in >> uses;
            in >> depth;
            std::string path;
            in.get();
            std::getline(in, path, '\n');

            if (files.size() % 10'000 == 0)
                std::cout << "Loaded " << files.size() << '\n';

            if (path.size())
                files.push_back({ std::move(path), depth, uses });
        }

        std::cout << "Loaded done\n";
    };

    auto sort = [&] {
        std::sort(
            std::execution::par_unseq,
            files.begin(), files.end(),
            [](const File& l, const File& r) {
                if (l.uses != r.uses)
                    return l.uses >= r.uses;

                if (l.depth != r.depth)
                    return l.depth <= r.depth;

                if (l.path.size() != r.path.size())
                    return l.path.size() <= r.path.size();

                return std::lexicographical_compare(l.path.begin(), l.path.end(), r.path.begin(), r.path.end());
            });
    };

    load();
    filter("");

    bool running = true;
    bool focusInput = true;
    char query[512] = {};
    int selected = -1;

    enum class Operation {
        Open,
        RunAs,
        Explore,
        ShowInExplorer,
        CopyToClipboard,
    };

    auto open = [&](File& file, Operation op, bool incrementUsage = false) {
        switch (op)
        {
        break;case Operation::Open:
            ShellExecuteA(nullptr, "open", file.path.c_str(), nullptr, nullptr, SW_SHOW);
        break;case Operation::RunAs:
            ShellExecuteA(nullptr, "runas", file.path.c_str(), nullptr, nullptr, SW_SHOW);
        break;case Operation::Explore:
            ShellExecuteA(nullptr, "explore", file.path.c_str(), nullptr, nullptr, SW_SHOW);
        break;case Operation::ShowInExplorer:
            std::system(std::format("explorer /select,\"{}\"", file.path).c_str());
        break;case Operation::CopyToClipboard:
            {
                OpenClipboard(glfwGetWin32Window(window));
                EmptyClipboard();
                auto contentHandle = GlobalAlloc(GMEM_MOVEABLE, file.path.size() + 1);
                auto contents = GlobalLock(contentHandle);
                memcpy(contents, file.path.c_str(), file.path.size() + 1);
                for (auto c = (char*)contents; *c; ++c)
                    *c = *c == '\\' ? '/' : *c;
                GlobalUnlock(contentHandle);
                SetClipboardData(CF_TEXT, contentHandle);
                CloseClipboard();
            }
        }

        if (incrementUsage)
        {
            file.uses++;
            sort();
            filter(query);
        }
    };

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
                        files.clear();
                        std::mutex mutex;
                        Indexer::Index([&](const char* path, uint32_t depth) {
                            std::string str = path;
                            std::scoped_lock lock{mutex};
                            files.emplace_back(std::move(str), depth, 0);
                        });
                        filter(query);
                    }

                    if (ImGui::MenuItem("Save", nullptr, &selected))
                    {
                        save();
                    }

                    if (ImGui::MenuItem("Load", nullptr, &selected))
                    {
                        load();
                        filter(query);
                    }

                    if (ImGui::MenuItem("Sort", nullptr, &selected))
                    {
                        sort();
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

        {
            ImGui::Begin("Search");

            if (focusInput)
            {
                ImGui::SetKeyboardFocusHere();
                focusInput = false;
            }

            if (glfwGetKey(window, GLFW_KEY_G))
                ImGui::SetKeyboardFocusHere();

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
                    int index = filtered[i];
                    auto& file = files[index];

                    ImGui::Selectable(file.path.c_str(), selected == index, ImGuiSelectableFlags_AllowDoubleClick);

                    if (ImGui::IsItemHovered())
                    {
                        if (selected != index)
                        {
                            selected = index;
                            if (ImGui::IsWindowFocused())
                                ImGui::SetKeyboardFocusHere(-1);
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || (ImGui::IsMouseDoubleClicked(GLFW_MOUSE_BUTTON_1)))
                        {
                            open(file, Operation::Open, true);
                        }

                        if (ImGui::IsKeyPressed(ImGuiKey_C, false) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
                            open(file, Operation::CopyToClipboard);

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride))
                            ImGui::SetNextWindowPos(ImGui::GetMousePos());
                    }

                    if (selected == index && ImGui::BeginPopupContextWindow("OpenPopupID",
                        ImGuiPopupFlags_MouseButtonRight))
                    {
                        if (ImGui::MenuItem("Open"))
                            open(file, Operation::Open, true);

                        ImGui::Separator();

                        if (ImGui::MenuItem("Explore"))
                            open(file, Operation::Explore, true);

                        if (ImGui::MenuItem("Show in Explorer"))
                            open(file, Operation::ShowInExplorer, true);

                        ImGui::Separator();

                        if (ImGui::MenuItem("Copy Path"))
                            open(file, Operation::CopyToClipboard);

                        ImGui::Separator();

                        if (ImGui::MenuItem("Run as Administrator"))
                            open(file, Operation::RunAs, true);

                        ImGui::EndPopup();
                    }
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

        {
            // Center window

            glfwSetWindowSize(window, 1920, 1200);

            int count = 0;
            auto monitor = glfwGetVideoMode(glfwGetMonitors(&count)[0]);
            int w, h;
            glfwGetWindowSize(window, &w, &h);
            glfwSetWindowPos(window, (monitor->width - w) / 2, (monitor->height - h) / 2);
        }

        glfwSetWindowShouldClose(window, GLFW_FALSE);

        focusInput = true;

        while (!glfwWindowShouldClose(window))
        {
            glfwWaitEvents();

            draw();
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