#pragma once
#include "editor/history.hpp"
#include "aicontrol/commands.hpp"
#include "editor/blueprint.hpp"
#include "editor/glass.hpp"
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

namespace eng {

// Set to non-zero by headless self-tests; main() returns it as the process exit code.
inline int g_exit_code = 0;

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
    // Self-tests ask main() to grab the finished window (scene + editor UI) after end_frame().
    std::string take_shot_request() { std::string s; s.swap(shot_request_); return s; }
    // Call right after begin_frame(), BEFORE deciding whether to step the simulation: on a Play
    // edge it snapshots the scene (so the snapshot is the true pre-Play state, not one
    // physics step later), on a Stop edge it restores it and unbinds virtual input.
    void sync_play_input(CommandContext&);

private:
    GLFWwindow* window_;
    std::string selected_;                 // primary selection
    std::vector<std::string> multi_;       // additional selected entities
    std::string selected_anim_;   // entity whose animation the timeline shows

    // undo/redo: delta history (per-entity before/after, see history.hpp), committed once
    // an edit gesture ends -- never a full-scene copy per frame
    SceneHistory history_;
    bool hist_dirty_ = false;          // something may have changed since the last commit
    unsigned long hist_cmd_seq_ = 0;   // CommandContext::command_seq at the last commit
    bool hist_prev_play_ = false;
    bool hist_init_ = false;
    std::string shot_request_;
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

    // number-key camera bookmarks: Shift+1..4 stores, 1..4 recalls
    struct CamPose { float yaw = 0, pitch = 0, dist = 0, pivot[3] = {0, 0, 0}; bool set = false; };
    CamPose cam_marks_[4];

    int gizmo_op_ = 7;
    int gizmo_mode_ = 1;
    bool gizmo_snap_ = false;
    int snap_tr_idx_ = 1;   // index into {0.1, 0.25, 0.5, 1.0}

    bool prev_play_ = false;   // edge-detect the Play toggle to (un)bind gameplay input

    // Assets panel: cached listings of scenes/*.json + prefabs/*.json
    std::vector<std::string> asset_scenes_, asset_prefabs_;
    bool assets_scanned_ = false;
    void scan_assets();

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
    void panel_assets(CommandContext&);
    void update_orbit_camera(CommandContext&);
    void viewport_pick(CommandContext&);        // click-to-select in Designer mode
    void focus_selected(CommandContext&);       // F: frame the selection
    void playstop_selftest(CommandContext&);    // ALI_PLAYSTOP_SELFTEST=1
    void run_console(CommandContext&, const std::string& line);
    void spawn(CommandContext&, const char* primitive);
};

} // namespace eng
