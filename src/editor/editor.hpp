#pragma once
#include "aicontrol/commands.hpp"
#include "editor/blueprint.hpp"
#include "editor/glass.hpp"
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace eng {

// Dear ImGui editor. Layout after Unreal's UMG editor; visual language is Apple
// liquid-glass -- the 3D scene is the full-window backdrop, panels are frosted
// glass cards floating over it. Human-facing (palette / hierarchy / details /
// viewport / animations / timeline / output log) plus a command console that
// runs the same JSON commands the AI uses. The scene JSON stays the source of truth.
class Editor {
public:
    explicit Editor(GLFWwindow* window);
    ~Editor();

    void begin_frame();
    // Blit the scene to the window and build the frosted backdrop. Call after
    // rendering the scene to `scene_tex` (full window size), before draw().
    void background(unsigned scene_tex, int w, int h);
    void draw(CommandContext& ctx, unsigned scene_tex, int tex_w, int tex_h);
    void end_frame();

    void wanted_viewport(int& w, int& h) const { w = vp_w_; h = vp_h_; }
    bool wants_play() const { return play_; }

private:
    GLFWwindow* window_;
    std::string selected_;                 // primary selection
    std::vector<std::string> multi_;       // additional selected entities
    std::string selected_anim_;   // entity whose animation the timeline shows

    // undo/redo: full-scene JSON snapshots, committed once an edit gesture ends
    std::vector<std::string> undo_, redo_;
    std::string hist_snap_;
    void commit_history(CommandContext&);
    void do_undo(CommandContext&);
    void do_redo(CommandContext&);
    bool is_selected(const std::string& name) const;
    int vp_w_ = 1280, vp_h_ = 720;
    bool play_ = false;
    bool layout_built_ = false;
    int mode_ = 0;   // 0 Designer, 1 Graph (placeholder)

    float cam_yaw_ = -40.0f, cam_pitch_ = 25.0f, cam_dist_ = 16.0f;
    float pivot_[3] = {0, 1.5f, 0};

    int gizmo_op_ = 7;
    int gizmo_mode_ = 1;

    std::unique_ptr<BlueprintEditor> bp_;
    std::unique_ptr<GlassLayer> glass_;
    unsigned blur_tex_ = 0;
    int win_w_ = 1280, win_h_ = 720;
    std::vector<std::string> console_log_;
    char console_buf_[512] = {0};
    char save_path_[512] = "scenes/edited.json";

    void build_layout();
    void main_menu(CommandContext&);
    void toolbar(CommandContext&);
    void status_bar(CommandContext&);
    void panel_palette(CommandContext&);
    void panel_hierarchy(CommandContext&);
    void panel_details(CommandContext&);
    void panel_viewport(CommandContext&, unsigned tex);
    void panel_animations(CommandContext&);
    void panel_timeline(CommandContext&);
    void panel_output(CommandContext&);
    void update_orbit_camera(CommandContext&);
    void run_console(CommandContext&, const std::string& line);
    void spawn(CommandContext&, const char* primitive);
};

} // namespace eng
