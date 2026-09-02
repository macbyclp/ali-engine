#pragma once
#include "aicontrol/commands.hpp"
#include <string>
#include <vector>

struct GLFWwindow;

namespace eng {

// Dear ImGui editor overlaid on the engine window. Human-facing: hierarchy,
// inspector, transform gizmos, a live viewport, and a command console that runs
// the very same JSON commands the AI uses. The scene JSON stays the source of truth.
class Editor {
public:
    explicit Editor(GLFWwindow* window);
    ~Editor();

    void begin_frame();
    // Renders every panel. `scene_tex` is the colour texture of the just-rendered
    // scene; the viewport shows it and reports the size it wants next frame.
    void draw(CommandContext& ctx, unsigned scene_tex, int tex_w, int tex_h);
    void end_frame();

    void wanted_viewport(int& w, int& h) const { w = vp_w_; h = vp_h_; }
    bool wants_play() const { return play_; }

private:
    GLFWwindow* window_;
    std::string selected_;
    int vp_w_ = 1280, vp_h_ = 720;
    bool play_ = false;

    // orbit camera
    float cam_yaw_ = -35.0f, cam_pitch_ = 28.0f, cam_dist_ = 16.0f;
    float pivot_[3] = {0, 1, 0};

    int gizmo_op_ = 7;   // ImGuizmo TRANSLATE
    int gizmo_mode_ = 1; // WORLD

    std::vector<std::string> console_log_;
    char console_buf_[512] = {0};
    char save_path_[512] = "scenes/edited.json";

    void panel_hierarchy(CommandContext&);
    void panel_inspector(CommandContext&);
    void panel_viewport(CommandContext&, unsigned tex, int tw, int th);
    void panel_console(CommandContext&);
    void menu_bar(CommandContext&);
    void update_orbit_camera(CommandContext&);
    void run_console(CommandContext&, const std::string& line);
};

} // namespace eng
