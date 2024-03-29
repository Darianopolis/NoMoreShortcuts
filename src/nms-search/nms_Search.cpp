#include "nms_Search.hpp"

#include <nova/core/nova_Guards.hpp>

#include <stb_image.h>

#include <imgui.h>

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

    context = nova::Context::Create({ .debug = false });
    queue = context.Queue(nova::QueueFlags::Graphics, 0);
    imDraw = std::make_unique<nova::draw::Draw2D>(context);

    {
        char module_filename[4096];
        GetModuleFileNameA(nullptr, module_filename, sizeof(module_filename));
        NOVA_LOG("Module filename: {}", module_filename);
        exe_dir = std::filesystem::path(module_filename).parent_path();
        NOVA_LOG(" exe dir: {}", exe_dir.string());
    }

    std::filesystem::create_directories(std::filesystem::path(indexFile).parent_path());

NOVA_DEBUG();
    searcher.init(context, context.Queue(nova::QueueFlags::Compute, 0));
NOVA_DEBUG();

    {
        GLFWimage iconImage;

        i32 channels;
        iconImage.pixels = stbi_load("favicon.png", &iconImage.width, &iconImage.height, &channels, STBI_rgb_alpha);
        NOVA_DEFER(&) { stbi_image_free(iconImage.pixels); };

        glfwSetWindowIcon(window, 1, &iconImage);
    }

    swapchain = nova::Swapchain::Create(context, glfwGetWin32Window(window),
        nova::ImageUsage::TransferDst
        | nova::ImageUsage::ColorAttach,
        nova::PresentMode::Fifo);

    commandPool = nova::CommandPool::Create(context, queue);
    fence = nova::Fence::Create(context);

// -----------------------------------------------------------------------------

    i32 count;
    auto mode = glfwGetVideoMode(glfwGetMonitors(&count)[0]);
    mWidth = mode->width;
    mHeight = mode->height;

// -----------------------------------------------------------------------------

    font = imDraw->LoadFont("SEGUISB.TTF", 35.f * ui_scale);
    fontSmall = imDraw->LoadFont("SEGOEUI.TTF", 18.f * ui_scale);

// -----------------------------------------------------------------------------

    keywords.push_back("");

    using namespace std::chrono;

NOVA_DEBUG();
    resultList = std::make_unique<ResultListPriorityCollector>();
    favResultList = std::make_unique<FavResultList>();
    NOVA_LOGEXPR(favResultList);
    fileResultList = std::make_unique<FileResultList>(&searcher, favResultList.get());
    resultList->AddList(favResultList.get());
    resultList->AddList(fileResultList.get());
NOVA_DEBUG();

    show = false;

NOVA_DEBUG();
    UpdateIndex();
NOVA_DEBUG();
    ResetItems();
NOVA_DEBUG();
    UpdateQuery();
NOVA_DEBUG();
}

App::~App()
{
    fence.Wait();
    nms::ClearIconCache();
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
        selection = (u32)items.size() - 1;
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
    resultList->FilterStrings(keywords);
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

void App::Move(i32 delta)
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

    Vec2 hInputSize = Vec2 { 960.f, 29.f } * ui_scale;

    f32 outputItemHeight = 76.f * ui_scale;
    u32 outputCount = u32(items.size());

    f32 hOutputWidth = 600.f * ui_scale;
    f32 hOutputHeight = 0.5f * outputItemHeight * outputCount;

    f32 margin = 18.f * ui_scale;

    f32 cornerRadius = 18.f * ui_scale;
    f32 borderWidth = 2.f * ui_scale;

    Vec2 textInset = Vec2 { 74.5f, 37.f } * ui_scale;
    Vec2 textSmallInset = Vec2 { 76.f, 60.f } * ui_scale;

    f32 iconSize = 50 * ui_scale;
    f32 iconPadding = (outputItemHeight - iconSize) / 2.f;

    f32 inputTextVOffset = 17.f * ui_scale;

    f32 highlightInset = 2.f * ui_scale;

    // Input box

    imDraw->DrawRect({
        .center_color = backgroundColor,
        .border_color = borderColor,
        .center_pos = pos - Vec2(0.f, hInputSize.y),
        .half_extent = hInputSize + Vec2(borderWidth),
        .corner_radius = cornerRadius,
        .border_width = borderWidth,
    });

    // Input text

    {
        auto query = JoinQuery();
        auto bounds = imDraw->MeasureString(query, *font);

        if (!bounds.Empty())
        {
            imDraw->DrawString(query,
                pos - Vec2(bounds.Width() * 0.5f, inputTextVOffset),
                *font);
        }
    }

    if (items.empty())
        return;

    // Output box

    imDraw->DrawRect({
        .center_color = backgroundColor,
        .border_color = borderColor,
        .center_pos = pos + Vec2(0.f, hOutputHeight + margin + borderWidth),
        .half_extent = Vec2(hOutputWidth, hOutputHeight) + Vec2(borderWidth),
        .corner_radius = cornerRadius,
        .border_width = borderWidth,
    });

    // Highlight

    imDraw->DrawRect({
        .center_color = highlightColor,
        .center_pos = pos
            + Vec2(0.f, margin + borderWidth + outputItemHeight * (0.5f + f32(selection))),
        .half_extent = Vec2(hOutputWidth, outputItemHeight * 0.5f)
            - Vec2(highlightInset),
        .corner_radius = cornerRadius - borderWidth - highlightInset,
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
            icon->texture = nms::LoadIconFromPath(context, path.string());
        }
        else
        {
            icon = &iter->second;
        }

        if (icon->texture)
        {
            imDraw->DrawRect({
                .center_pos = pos
                    + Vec2(-hOutputWidth + (iconSize / 2.f) + iconPadding,
                        margin + borderWidth + outputItemHeight * (0.5f + f32(i))),
                .half_extent = Vec2(iconSize) / 2.f,

                .tex_tint = Vec4(1.f),
                .tex_idx = icon->texture.Descriptor(),
                .tex_center_pos = { 0.5f, 0.5f },
                .tex_half_extent = { 0.5f, 0.5f },
            });
        }

        // Filename

        imDraw->DrawString(
            path.filename().empty()
                ? path.string()
                : path.filename().string(),
            pos + Vec2(-hOutputWidth, margin + borderWidth)
                + Vec2(0.f, outputItemHeight * f32(i))
                + textInset,
            *font);

        // Path

        imDraw->DrawString(
            path.has_parent_path()
                ? path.parent_path().string()
                : path.string(),
            pos + Vec2(-hOutputWidth, margin + borderWidth)
                + Vec2(0.f, outputItemHeight * f32(i))
                + textSmallInset,
            *fontSmall);
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

            fence.Wait();
            commandPool.Reset();

            // Record commands

            auto cmd = commandPool.Begin();

            // Update window size, record primary buffer and present

            glfwSetWindowSize(window, i32(imDraw->Bounds().Width()), i32(imDraw->Bounds().Height()));
            glfwSetWindowPos(window, i32(imDraw->Bounds().min.x), i32(imDraw->Bounds().min.y));

            queue.Acquire({swapchain}, {fence});

            cmd.ClearColor(swapchain.Target(), Vec4(0.f, 1/255.f, 0.f, 0.f));
            imDraw->Record(cmd, swapchain.Target());

            cmd.Present(swapchain);

            queue.Submit({cmd}, {fence}, {fence});
            queue.Present({swapchain}, {fence});

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
    auto c = static_cast<c8>(codepoint);
    auto& keyword = keywords[keywords.size() - 1];
    if (c == ' ')
    {
        if (keyword.size() == 0 || keywords.size() == 8)
            return;
        keywords.push_back("");
    }
    else
    {
        if (c < ' ' || c > '~')
            return;
        keyword += c;
        resultList->FilterStrings(keywords);
    }
    UpdateQuery();
}

void App::UpdateIndex()
{
    if (std::filesystem::exists(indexFile)) {
        load_index(index, indexFile.c_str());
    } else {
        index_filesystem(index);
        sort_index(index);
        save_index(index, indexFile.c_str());
    }
    searcher.set_index(index);
    fileResultList->FilterStrings(keywords);
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

            NOVA_STACK_POINT();

            favResultList->IncrementUses(view->GetPath());
            ResetQuery();
            show = false;

            if ((GetKeyState(VK_LSHIFT) & 0x8000)
            && (GetKeyState(VK_LCONTROL) & 0x8000)) {
                ShellExecuteA(
                    nullptr,
                    "open",
                    (exe_dir / "nms-launch.exe").string().c_str(),
                    NOVA_STACK_FORMAT("runas \"{}\"", str.c_str()).data(),
                    nullptr,
                    SW_SHOW);
            } else if (GetKeyState(VK_LCONTROL) & 0x8000) {
                // open selected in explorer
                std::system(
                    NOVA_STACK_FORMAT("explorer /select, \"{}\"", str.c_str()).data());
            } else {
                // open
                auto params = NOVA_STACK_FORMAT("open \"{}\"", str.c_str());
                ShellExecuteA(
                    nullptr,
                    "open",
                    (exe_dir / "nms-launch.exe").string().c_str(),
                    params.data(),
                    nullptr,
                    SW_SHOW);
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
            [[maybe_unused]] auto matchBit = static_cast<u8>(1 << (keywords.size() - 1));
            if (keyword.length() > 0)
            {
                keyword.pop_back();
                // filter(matchBit, keyword, false);
                resultList->FilterStrings(keywords);
                UpdateQuery();
            }
            else if (keywords.size() > 1)
            {
                keywords.pop_back();
                // tree.setMatchBits(matchBit, 0, matchBit, 0);
                // tree.matchBits &= ~matchBit;
                resultList->FilterStrings(keywords);
                UpdateQuery();
            }
        }
    break;case GLFW_KEY_C:
        if ((mods & GLFW_MOD_CONTROL) && !items.empty())
        {
            auto view = items[selection].get();
            auto str = view->GetPath().string();
            NOVA_LOG("Copying {}!", str);

            favResultList->IncrementUses(view->GetPath());
            OpenClipboard(glfwGetWin32Window(window));
            EmptyClipboard();
            auto contentHandle = GlobalAlloc(GMEM_MOVEABLE, str.size() + 1);
            auto contents = GlobalLock(contentHandle);
            memcpy(contents, str.data(), str.size() + 1);
            for (auto c = (c8*)contents; *c; ++c)
                *c = *c == '\\' ? '/' : *c;
            GlobalUnlock(contentHandle);
            SetClipboardData(CF_TEXT, contentHandle);
            CloseClipboard();
        }
    break;case GLFW_KEY_F5:
        if (mods & GLFW_MOD_CONTROL)
        {
            ::ShellExecuteA(nullptr, "open", (exe_dir / "nms-index.exe").string().c_str(), nullptr, nullptr, SW_SHOW);
        }
        else
        {
            resultList = std::make_unique<ResultListPriorityCollector>();
            favResultList = std::make_unique<FavResultList>();
            fileResultList = std::make_unique<FileResultList>(&searcher, favResultList.get());
            resultList->AddList(favResultList.get());
            resultList->AddList(fileResultList.get());

            UpdateIndex();
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
    catch (const std::exception& e)
    {
        NOVA_LOG("Error: {}", e.what());
    }
    catch (...)
    {
        NOVA_LOG("Something went wrong!");
    }
}

i32 WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, i32)
{
    Main();
    return 0;
}

i32 main()
{
    Main();
    return 0;
}