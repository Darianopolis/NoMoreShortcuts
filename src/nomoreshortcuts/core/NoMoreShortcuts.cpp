#include "NoMoreShortcuts.hpp"

#include "FileIndex.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <combaseapi.h>

#include <thread>

using namespace std::literals;

App::App()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    window = glfwCreateWindow(1920, 1200, "No More Shortcuts", nullptr, nullptr);

    HWND hwnd = glfwGetWin32Window(window);
    SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);

    // TODO: Chroma key is an ugly hack, use nchittest to do analytical transparency
    //   Or, do full screeen pass to filter out unintentional chroma key matches and
    //   apply chroma key based on alpha.
    SetLayeredWindowAttributes(hwnd, RGB(0, 1, 0), 0, LWA_COLORKEY);

    {
        GLFWimage iconImage;

        int channels;
        iconImage.pixels = stbi_load("favicon.png", &iconImage.width, &iconImage.height, &channels, STBI_rgb_alpha);
        NOVA_ON_SCOPE_EXIT(&) { stbi_image_free(iconImage.pixels); };

        glfwSetWindowIcon(window, 1, &iconImage);
    }

    context = nova::Context::Create({
        .debug = false,
    });

    surface = context->CreateSurface(hwnd);
    swapchain = context->CreateSwapchain(surface,
        nova::ImageUsage::TransferDst
        | nova::ImageUsage::ColorAttach,
        nova::PresentMode::Fifo);

    queue = context->graphics;
    commandPool = context->CreateCommandPool();
    fence = context->CreateFence();
    tracker = context->CreateResourceTracker();

// -----------------------------------------------------------------------------

    imDraw = nova::ImDraw2D::Create(context);

// -----------------------------------------------------------------------------

    int count;
    auto mode = glfwGetVideoMode(glfwGetMonitors(&count)[0]);
    mWidth = mode->width;
    mHeight = mode->height;

// -----------------------------------------------------------------------------

    font = imDraw->LoadFont("SEGUISB.TTF", 35.f, commandPool, tracker, fence, queue);
    fontSmall = imDraw->LoadFont("SEGOEUI.TTF", 18.f, commandPool, tracker, fence, queue);

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

    keywords.push_back("");

    using namespace std::chrono;

    resultList = std::make_unique<ResultListPriorityCollector>();
    favResultList = std::make_unique<FavResultList>(&keywords);
    NOVA_LOGEXPR(favResultList);
    fileResultList = std::make_unique<FileResultList>(favResultList.get());
    resultList->AddList(favResultList.get());
    resultList->AddList(fileResultList.get());

    show = false;

    ResetItems();
    UpdateQuery();
}

void App::ResetItems(bool end)
{
    items.clear();
    if (end)
    {
        auto item = resultList->Prev(nullptr);
        if (item)
        {
            auto itemP = item.get();
            items.push_back(std::move(item));
            while ((items.size() < 5) && (item = resultList->Prev(itemP)))
            {
                itemP = item.get();
                items.push_back(std::move(item));
            }
            std::reverse(items.begin(), items.end());
        }
        selection = (uint32_t)items.size() - 1;
    }
    else
    {
        auto item = resultList->Next(nullptr);
        if (item)
        {
            auto itemP = item.get();
            items.push_back(std::move(item));
            while ((items.size() < 5) && (item = resultList->Next(itemP)))
            {
                itemP = item.get();
                items.push_back(std::move(item));
            }
        }
        selection = 0;
    }
}

void App::ResetQuery()
{
    keywords.clear();
    keywords.emplace_back();
    // tree.setMatchBits(1, 1, 0, 0);
    // tree.matchBits = 1;
    resultList->Query(QueryAction::SET, "");
    UpdateQuery();
}

std::string App::JoinQuery()
{
    auto str = std::string{};
    for (auto& k : keywords) {
        if (k.empty())
            continue;

        if (!str.empty())
            str += ' ';

        str.append(k);
    }

    return str;
}

void App::UpdateQuery()
{
    ResetItems();
}

void App::Move(int delta)
{
    auto i = delta;
    if (i < 0)
    {
        while (MoveSelectedUp() && ++i < 0);
    }
    else if (i > 0)
    {
        while (MoveSelectedDown() && --i > 0);
    }
}

bool App::MoveSelectedUp()
{
    if (items.empty())
        return false;

    if (items.size() < 5)
    {
        if (selection == 0)
            return false;

        selection--;
    }
    else if (selection > 2)
    {
        selection--;
    }
    else
    {
        auto prev = resultList->Prev(items[0].get());
        if (prev)
        {
            std::rotate(items.rbegin(), items.rbegin() + 1, items.rend());
            items[0] = std::move(prev);
        }
        else if (selection > 0)
        {
            selection--;
        }
        else
        {
            return false;
        }
    }

    return true;
}

bool App::MoveSelectedDown()
{
    if (items.empty())
         return false;

    if (items.size() < 5)
    {
        if (selection == items.size() - 1)
            return false;

        selection++;
    }
    else if (selection < 2)
    {
        selection++;
    }
    else
    {
        auto next = resultList->Next(items[items.size() - 1].get());
        if (next)
        {
            std::rotate(items.begin(), items.begin() + 1, items.end());
            items[items.size() - 1] = std::move(next);
        }
        else if (selection < 4)
        {
            selection++;
        }
        else
        {
            return false;
        }
    }

    return true;
}

void App::Draw()
{
    Vec4 backgroundColor = { 0.1f, 0.1f, 0.1f, 1.f };
    Vec4 borderColor =  { 0.6f, 0.6f, 0.6f, 0.5f };
    Vec4 highlightColor = { 0.4f, 0.4f, 0.4f, 0.2f, };

    Vec2 pos = { mWidth * 0.5f, mHeight * 0.5f };

    Vec2 hInputSize = { 960.f, 29.f };

    f32 outputItemHeight = 76.f;
    u32 outputCount = u32(items.size());

    f32 hOutputWidth = 600.f;
    f32 hOutputHeight = 0.5f * outputItemHeight * outputCount;

    f32 margin = 18.f;

    f32 cornerRadius = 18.f;
    f32 borderWidth = 2.f;

    Vec2 textInset = { 74.5f, 37.f };
    Vec2 textSmallInset = { 76.f, 60.f };

    f32 iconSize = 50;
    f32 iconPadding = (outputItemHeight - iconSize) / 2.f;

    // Input box

    imDraw->DrawRect({
        .centerColor = backgroundColor,
        .borderColor = borderColor,
        .centerPos = pos - Vec2(0.f, hInputSize.y),
        .halfExtent = hInputSize + Vec2(borderWidth),
        .cornerRadius = cornerRadius,
        .borderWidth = borderWidth,
    });

    // Input text

    {
        auto query = JoinQuery();
        auto bounds = imDraw->MeasureString(query, font);

        if (!bounds.Empty())
        {
            imDraw->DrawString(query,
                pos - Vec2(bounds.Width() / 2.f, 17.f),
                font);
        }
    }

    if (items.empty())
        return;

    // Output box

    imDraw->DrawRect({
        .centerColor = backgroundColor,
        .borderColor = borderColor,
        .centerPos = pos + Vec2(0.f, hOutputHeight + margin + borderWidth),
        .halfExtent = Vec2(hOutputWidth, hOutputHeight) + Vec2(borderWidth),
        .cornerRadius = cornerRadius,
        .borderWidth = borderWidth,
    });

    // Highlight

    imDraw->DrawRect({
        .centerColor = highlightColor,
        .centerPos = pos
            + Vec2(0.f, margin + borderWidth + outputItemHeight * (0.5f + selection)),
        .halfExtent = Vec2(hOutputWidth, outputItemHeight * 0.5f)
            - Vec2(2.f),
        .cornerRadius = cornerRadius - borderWidth - 2.f,
    });

    for (u32 i = 0; i < outputCount; ++i)
    {
        auto path = items[i]->GetPath();

        // Icon

        IconResult* icon;
        auto iter = iconCache.find(path);
        if (iter == iconCache.end())
        {
            icon = &iconCache[path];
            icon->image = nms::LoadIconFromPath(
                context, commandPool, tracker, queue, fence,
                path.string());

            if (icon->image)
                icon->texID = imDraw->RegisterTexture(icon->image, imDraw->defaultSampler);
        }
        else
        {
            icon = &iter->second;
        }

        if (icon->image)
        {
            imDraw->DrawRect({
                .centerPos = pos
                    + Vec2(-hOutputWidth + (iconSize / 2.f) + iconPadding,
                        margin + borderWidth + outputItemHeight * (0.5f + i)),
                .halfExtent = Vec2(iconSize) / 2.f,

                .texTint = Vec4(1.f),
                .texIndex = icon->texID,
                .texCenterPos = { 0.5f, 0.5f },
                .texHalfExtent = { 0.5f, 0.5f },
            });
        }

        // Filename

        imDraw->DrawString(
            path.filename().empty()
                ? path.string()
                : path.filename().string(),
            pos + Vec2(-hOutputWidth, margin + borderWidth)
                + Vec2(0.f, outputItemHeight * i)
                + textInset,
            font);

        // Path

        imDraw->DrawString(
            path.has_parent_path()
                ? path.parent_path().string()
                : path.string(),
            pos + Vec2(-hOutputWidth, margin + borderWidth)
                + Vec2(0.f, outputItemHeight * i)
                + textSmallInset,
            fontSmall);
    }
}

void App::Run()
{
    glfwSetWindowUserPointer(window, this);
    glfwSetCharCallback(window, [](auto w, u32 codepoint) {
        auto app = (App*)glfwGetWindowUserPointer(w);
        app->OnChar(codepoint);
    });
    glfwSetKeyCallback(window, [](auto w, i32 key, i32 scancode, i32 action, i32 mods) {
        (void)scancode;

        auto app = (App*)glfwGetWindowUserPointer(w);
        app->OnKey(key, action, mods);
    });

    auto hwnd = glfwGetWin32Window(window);
    RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_SHIFT, VK_SPACE);

    MSG msg = {};

    while (running)
    {
        show = true;
        glfwShowWindow(window);
        glfwSetWindowShouldClose(window, GLFW_FALSE);

        while (!glfwWindowShouldClose(window))
        {
            while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);

                if (msg.message == WM_HOTKEY)
                    glfwShowWindow(window);

                DispatchMessage(&msg);
            }
            glfwPollEvents();

            imDraw->Reset();
            Draw();

            // Wait for frame

            fence->Wait();
            commandPool->Reset();

            // Record commands

            auto cmd2 = commandPool->BeginSecondary(tracker, nova::RenderingDescription {
                .colorFormats = { nova::Format(swapchain->format.format) },
            });
            imDraw->Record(cmd2);
            cmd2->End();

            auto cmd = commandPool->BeginPrimary(tracker);
            cmd->SetViewport(imDraw->bounds.Size(), false);
            cmd->SetBlendState(1, true);

            // Update window size, record primary buffer and present

            glfwSetWindowSize(window, i32(imDraw->bounds.Width()), i32(imDraw->bounds.Height()));
            glfwSetWindowPos(window, i32(imDraw->bounds.min.x), i32(imDraw->bounds.min.y));

            queue->Acquire({swapchain}, {fence});

            cmd->BeginRendering({swapchain->image}, {Vec4(0.f, 1.f / 255.f, 0.f, 0.f)}, true);
            cmd->ExecuteCommands({cmd2});
            cmd->EndRendering();

            cmd->Transition(swapchain->image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_NONE, 0);

            queue->Submit({cmd}, {fence}, {fence});
            queue->Present({swapchain}, {fence});

            if (!show)
                glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        if (!running)
            break;

        glfwHideWindow(window);
        while (GetMessage(&msg, hwnd, 0, 0) && msg.message != WM_HOTKEY);

    }
}

void App::OnChar(u32 codepoint)
{
    auto c = static_cast<char>(codepoint);
    auto& keyword = keywords[keywords.size() - 1];
    if (c == ' ')
    {
        if (keyword.size() == 0 || keywords.size() == 8) return;
        [[maybe_unused]] auto set = static_cast<uint8_t>(1 << keywords.size());
        // tree.setMatchBits(set, set, set, 0);
        // tree.matchBits |= set;
        keywords.push_back("");
    }
    else
    {
        if (c < ' ' || c > '~')
            return;
        // auto matchBit = static_cast<uint8_t>(1 << (keywords.size() - 1));
        keyword += c;
        // filter(matchBit, keyword, true);
        resultList->Query(QueryAction::SET, JoinQuery());
    }
    UpdateQuery();
}

void App::OnKey(u32 key, i32 action, i32 mods)
{
    if (action == GLFW_RELEASE)
        return;

    switch (key)
    {
    break;case GLFW_KEY_ESCAPE:
        if (mods & GLFW_MOD_SHIFT)
            running = false;
        show = false;
    // break;case MouseLButton:
    //     if (overlay::Mouseover(e, queryBox) || overlay::Mouseover(e, resultsBox))
    //     {
    //         Update();
    //         overlay::Focus(*mainLayer);
    //     }

    //     if (menu.visible && overlay::Mouseover(e, menu))
    //     {
    //         overlay::Quit(*stage, 0);
    //     }
    //     else
    //     {
    //         menu.visible = false;
    //         overlay::Hide(*menuLayer);
    //     }
    break;case GLFW_KEY_DOWN:
        Move(1);
    break;case GLFW_KEY_UP:
        Move(-1);
    break;case GLFW_KEY_LEFT:
        ResetItems();
    break;case GLFW_KEY_RIGHT:
        ResetItems(true);
    break;case GLFW_KEY_ENTER: {
        if (!items.empty())
        {
            auto view = items[selection].get();
            auto str = view->GetPath().string();
            NOVA_LOG("Running {}!", str);

            favResultList->IncrementUses(view->GetPath());
            ResetQuery();
            show = false;
            // overlay::Hide(*mainLayer);

            // system(("explorer \""+ str +"\"").c_str());

            if ((GetKeyState(VK_LSHIFT) & 0x8000)
                && (GetKeyState(VK_LCONTROL) & 0x8000)) {
                ShellExecuteA(nullptr, "runas", str.c_str(), nullptr, nullptr, SW_SHOW);
            } else {
                ShellExecuteA(nullptr, "open", str.c_str(), nullptr, nullptr, SW_SHOW);
            }
        }
    }
    break;case GLFW_KEY_DELETE:
        if ((mods & GLFW_MOD_SHIFT) && !items.empty())
        {
            auto view = items[selection].get();
            favResultList->ResetUses(view->GetPath());
            ResetQuery();
        }
    break;case GLFW_KEY_BACKSPACE:
        if (mods & GLFW_MOD_SHIFT) {
            ResetQuery();
        }
        else
        {
            auto& keyword = keywords[keywords.size() - 1];
            [[maybe_unused]] auto matchBit = static_cast<uint8_t>(1 << (keywords.size() - 1));
            if (keyword.length() > 0)
            {
                keyword.pop_back();
                // filter(matchBit, keyword, false);
                resultList->Query(QueryAction::SET, JoinQuery());
                UpdateQuery();
            }
            else if (keywords.size() > 1)
            {
                keywords.pop_back();
                // tree.setMatchBits(matchBit, 0, matchBit, 0);
                // tree.matchBits &= ~matchBit;
                resultList->Query(QueryAction::SET, JoinQuery());
                UpdateQuery();
            }
        }
    break;case GLFW_KEY_C:
        if ((mods & GLFW_MOD_CONTROL) && !items.empty())
        {
            auto view = items[selection].get();
            auto str = view->GetPath().string();
            NOVA_LOG("Copying {}!", str);

            OpenClipboard(glfwGetWin32Window(window));
            EmptyClipboard();
            auto contentHandle = GlobalAlloc(GMEM_MOVEABLE, str.size() + 1);
            auto contents = GlobalLock(contentHandle);
            memcpy(contents, str.data(), str.size() + 1);
            for (auto c = (char*)contents; *c; ++c)
                *c = *c == '\\' ? '/' : *c;
            GlobalUnlock(contentHandle);
            SetClipboardData(CF_TEXT, contentHandle);
            CloseClipboard();
        }
    break;case GLFW_KEY_F5:
        if (mods & GLFW_MOD_CONTROL)
        {
            static std::atomic_bool indexing = false;

            bool expected = false;
            if (indexing.compare_exchange_strong(expected, true))
            {
                std::thread t([=, this] {
                    AllocConsole();
                    freopen("CONOUT$", "w", stdout);

                    std::vector<char> drives;
                    wchar_t driveNames[1024];
                    GetLogicalDriveStringsW(1023, driveNames);
                    for (wchar_t* drive = driveNames; *drive; drive += wcslen(drive) + 1)
                        drives.push_back((char)drive[0]);

                    for (auto& d : drives)
                    {
                        auto* node = IndexDrive(d);
                        auto saveLoc = std::format("{}\\.nms\\{}.index", getenv("USERPROFILE"), d);
                        NOVA_LOG("Saving to {}", saveLoc);
                        (void)node;
                        node->Save(saveLoc);
                    }

                    NOVA_LOG("Indexing complete. Close this window and refresh the index with F5 in app.");
                    FreeConsole();

                    indexing = false;
                });

                t.detach();
            }
        }
        else
        {
            resultList = std::make_unique<ResultListPriorityCollector>();
            favResultList = std::make_unique<FavResultList>(&keywords);
            fileResultList = std::make_unique<FileResultList>(favResultList.get());
            resultList->AddList(favResultList.get());
            resultList->AddList(fileResultList.get());

            ResetItems();
            UpdateQuery();
        }
    }
}

// -----------------------------------------------------------------------------
//                               Entry point
// -----------------------------------------------------------------------------

void Main()
{
    try
    {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        App app;
        app.Run();
    }
    catch (std::exception& e)
    {
        NOVA_LOG("Error: {}", e.what());
    }
    catch (...)
    {
        NOVA_LOG("Something went wrong!");
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    Main();
    return 0;
}

int main()
{
    Main();

    // nms::FileIndex index;
    // // index.Index();
    // // index.Save("index.dat");
    // NOVA_LOG("-----------------------------------------");
    // NOVA_TIMEIT_RESET();

    // index.Load("index.dat");
    // NOVA_TIMEIT("loaded-index");

    // index.Flatten();
    // NOVA_TIMEIT("flatten-index");

    // index.Query({ "BeamNG" });
    // NOVA_TIMEIT("query-index");

    // auto first = index.First();
    // NOVA_LOG("Result - {}", (void*)first->node);
    // NOVA_TIMEIT("find-index");
    // NOVA_LOG("Index loaded");

    return 0;
}