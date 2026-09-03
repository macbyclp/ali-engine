#pragma once
#include <imgui.h>

namespace eng {

// An Unreal-Engine-flavoured dark theme for Dear ImGui: near-black panels, thin
// borders, minimal rounding, a single blue accent.
inline void apply_unreal_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 2.0f;
    s.PopupRounding = 2.0f;
    s.GrabRounding = 2.0f;
    s.TabRounding = 0.0f;
    s.ScrollbarRounding = 2.0f;
    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 0.0f;
    s.TabBorderSize = 0.0f;
    s.WindowPadding = ImVec2(6, 6);
    s.FramePadding = ImVec2(6, 3);
    s.ItemSpacing = ImVec2(6, 4);
    s.ItemInnerSpacing = ImVec2(4, 4);
    s.IndentSpacing = 16.0f;
    s.ScrollbarSize = 12.0f;
    s.GrabMinSize = 8.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = s.Colors;
    const ImVec4 accent(0.10f, 0.55f, 0.95f, 1.00f);
    const ImVec4 accent_dim(0.10f, 0.40f, 0.68f, 1.00f);

    c[ImGuiCol_Text] = ImVec4(0.82f, 0.82f, 0.83f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.44f, 0.44f, 0.45f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.086f, 0.086f, 0.090f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.075f, 0.078f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.105f, 1.00f);
    c[ImGuiCol_Border] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.145f, 0.15f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.20f, 0.21f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.105f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.125f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.11f, 0.115f, 1.00f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.30f, 0.31f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.36f, 0.36f, 0.37f, 1.00f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent_dim;
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.23f, 0.23f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.28f, 0.28f, 0.29f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.17f, 0.17f, 0.18f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.23f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderActive] = accent_dim;
    c[ImGuiCol_Separator] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_SeparatorHovered] = accent_dim;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(0.20f, 0.20f, 0.21f, 0.50f);
    c[ImGuiCol_ResizeGripHovered] = accent_dim;
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(0.11f, 0.11f, 0.115f, 1.00f);
    c[ImGuiCol_TabHovered] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    c[ImGuiCol_TabActive] = ImVec4(0.15f, 0.15f, 0.155f, 1.00f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.105f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.13f, 0.13f, 0.135f, 1.00f);
    c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0.055f, 0.055f, 0.058f, 1.00f);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.12f, 0.12f, 0.125f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.14f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.018f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_DragDropTarget] = accent;
}

// Apple-2026 "liquid glass": panels are transparent (the editor draws a frosted
// backdrop card behind each one), generous rounding, a single blue accent,
// translucent controls, spring-y interaction. Text stays high-contrast white.
inline void apply_liquid_glass_theme() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 16.0f;
    s.ChildRounding = 12.0f;
    s.FrameRounding = 9.0f;
    s.PopupRounding = 12.0f;
    s.GrabRounding = 9.0f;
    s.TabRounding = 9.0f;
    s.ScrollbarRounding = 9.0f;
    s.WindowBorderSize = 0.0f;   // the glass card draws its own hairline
    s.FrameBorderSize = 0.0f;
    s.TabBorderSize = 0.0f;
    s.WindowPadding = ImVec2(14, 12);
    s.FramePadding = ImVec2(9, 5);
    s.ItemSpacing = ImVec2(9, 7);
    s.ItemInnerSpacing = ImVec2(7, 5);
    s.IndentSpacing = 18.0f;
    s.ScrollbarSize = 11.0f;
    s.GrabMinSize = 10.0f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1.0f;

    ImVec4* c = s.Colors;
    const ImVec4 accent(0.16f, 0.62f, 1.00f, 1.00f);      // iOS blue
    const ImVec4 glass_ctrl(1.00f, 1.00f, 1.00f, 0.10f);  // translucent white

    c[ImGuiCol_Text] = ImVec4(0.96f, 0.97f, 0.99f, 1.00f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.96f, 0.97f, 0.99f, 0.42f);
    c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);            // fully transparent
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.14f, 0.92f);
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = glass_ctrl;
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.22f);
    c[ImGuiCol_TitleBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TitleBgActive] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_TitleBgCollapsed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_MenuBarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.24f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.32f);
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = ImVec4(1, 1, 1, 0.85f);
    c[ImGuiCol_SliderGrabActive] = accent;
    c[ImGuiCol_Button] = glass_ctrl;
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.18f);
    c[ImGuiCol_ButtonActive] = ImVec4(accent.x, accent.y, accent.z, 0.85f);
    c[ImGuiCol_Header] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_SeparatorHovered] = ImVec4(accent.x, accent.y, accent.z, 0.6f);
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(1, 1, 1, 0.26f);
    c[ImGuiCol_ResizeGripActive] = accent;
    c[ImGuiCol_Tab] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_TabHovered] = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_TabActive] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_TabUnfocused] = ImVec4(1, 1, 1, 0.04f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.5f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_TableHeaderBg] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_TableBorderLight] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.03f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_DragDropTarget] = accent;
}

} // namespace eng
