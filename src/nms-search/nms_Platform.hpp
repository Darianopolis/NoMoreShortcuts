#pragma once

#include <nova/rhi/nova_RHI.hpp>
#include <nova/core/nova_Timer.hpp>

#include <nova/ui/nova_Draw2D.hpp>

#undef UNICODE
#define UNICODE

#undef _UNICODE
#define _UNICODE

#undef NOMINMAX
#define NOMINMAX

#undef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN

#undef GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <windowsx.h>
#include <shellapi.h>
#include <wincodec.h>
#include <CommCtrl.h>
#include <shellapi.h>
#include <combaseapi.h>

using namespace nova::types;

namespace nms
{
    void ConvertToWString(std::string_view input, std::wstring& output);
    inline std::wstring ConvertToWString(std::string_view input)
    {
        std::wstring str;
        ConvertToWString(input, str);
        return str;
    }

    void ConvertToString(std::wstring_view input, std::string& output);
    inline std::string ConvertToString(std::wstring_view input)
    {
        std::string str;
        ConvertToString(input, str);
        return str;
    }

    nova::Image LoadIconFromPath(
        nova::Context context,
        std::string_view path);

    void ClearIconCache();
}