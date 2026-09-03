#include "editor/editor.hpp"
#include "editor/glass.hpp"
#include "editor/theme.hpp"
#include "core/log.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using nlohmann::json;

namespace eng {

namespace fs = std::filesystem;

// Load Helvetica for the UI. We ship Liberation Sans (metric-identical, OFL) so
// the editor looks the same everywhere; if the machine has real Helvetica or
// Arial we use that instead.
static void load_editor_font() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    const char* candidates[] = {
        "C:/Windows/Fonts/helvetica.ttf",                       // real Helvetica, if installed
        "/System/Library/Fonts/Helvetica.ttc",
        "assets/fonts/LiberationSans-Regular.ttf",              // shipped clone (OFL)
        ENGINE_ASSET_DIR "/assets/fonts/LiberationSans-Regular.ttf",
        "C:/Windows/Fonts/arial.ttf",                           // Windows' Helvetica
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
    for (const char* p : candidates)
        if (fs::exists(p)) { io.Fonts->AddFontFromFileTTF(p, 16.0f, &cfg); return; }
    eng::log::warn("editor: no Helvetica/Arial found, using ImGui default font");
}

Editor::Editor(GLFWwindow* window) : window_(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;   // we build our own layout every run
    load_editor_font();
    apply_liquid_glass_theme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
    bp_ = std::make_unique<BlueprintEditor>();
    glass_ = std::make_unique<GlassLayer>();
}

void Editor::background(unsigned scene_tex, int w, int h) {
    win_w_ = w; win_h_ = h;
    blur_tex_ = glass_->frame(scene_tex, w, h);
}

Editor::~Editor() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::begin_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Editor::end_frame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ---------------- undo / redo ----------------
bool Editor::is_selected(const std::string& name) const {
    if (name == selected_) return true;
    for (auto& s : multi_) if (s == name) return true;
    return false;
}

void Editor::commit_history(CommandContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    bool settled = !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing() &&
                   !ImGui::IsMouseDown(ImGuiMouseButton_Left);
    std::string cur = ctx.scene.to_json().dump();
    if (hist_snap_.empty()) { hist_snap_ = cur; return; }
    if (!settled || cur == hist_snap_) return;
    undo_.push_back(hist_snap_);
    if (undo_.size() > 64) undo_.erase(undo_.begin());
    redo_.clear();
    hist_snap_ = std::move(cur);
    (void)io;
}

void Editor::do_undo(CommandContext& ctx) {
    if (undo_.empty()) return;
    redo_.push_back(ctx.scene.to_json().dump());
    std::string s = std::move(undo_.back());
    undo_.pop_back();
    ctx.scene.load_json(json::parse(s));
    ctx.physics.sync(ctx.scene);
    hist_snap_ = s;
    console_log_.push_back("[undo]");
}

void Editor::do_redo(CommandContext& ctx) {
    if (redo_.empty()) return;
    undo_.push_back(ctx.scene.to_json().dump());
    std::string s = std::move(redo_.back());
    redo_.pop_back();
    ctx.scene.load_json(json::parse(s));
    ctx.physics.sync(ctx.scene);
    hist_snap_ = s;
    console_log_.push_back("[redo]");
}

// ---------------- helpers ----------------
void Editor::run_console(CommandContext& ctx, const std::string& line) {
    try {
        json res = dispatch(ctx, json::parse(line));
        console_log_.push_back("> " + line);
        console_log_.push_back("  " + res.dump());
    } catch (const std::exception& ex) {
        console_log_.push_back("! " + std::string(ex.what()));
    }
    if (console_log_.size() > 300)
        console_log_.erase(console_log_.begin(), console_log_.begin() + 150);
}

void Editor::spawn(CommandContext& ctx, const char* primitive) {
    json r = dispatch(ctx, {{"method", "entity.spawn"},
                            {"params", {{"primitive", primitive}, {"position", {0, 1, 0}}}}});
    if (r.value("ok", false)) { selected_ = r["result"].value("name", std::string()); multi_.clear(); }
}

void Editor::update_orbit_camera(CommandContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        cam_yaw_ += io.MouseDelta.x * 0.4f;
        cam_pitch_ = std::clamp(cam_pitch_ - io.MouseDelta.y * 0.4f, -89.0f, 89.0f);
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        float cy = std::cos(glm::radians(cam_yaw_)), sy = std::sin(glm::radians(cam_yaw_));
        pivot_[0] -= io.MouseDelta.x * cy * cam_dist_ * 0.002f;
        pivot_[2] -= io.MouseDelta.x * sy * cam_dist_ * 0.002f;
        pivot_[1] += io.MouseDelta.y * cam_dist_ * 0.002f;
    }
    if (io.MouseWheel != 0.0f)
        cam_dist_ = std::clamp(cam_dist_ * (1.0f - io.MouseWheel * 0.1f), 1.0f, 400.0f);

    float cy = std::cos(glm::radians(cam_yaw_)), sy = std::sin(glm::radians(cam_yaw_));
    float cp = std::cos(glm::radians(cam_pitch_)), sp = std::sin(glm::radians(cam_pitch_));
    glm::vec3 pivot(pivot_[0], pivot_[1], pivot_[2]);
    auto& cam = ctx.scene.camera();
    cam.position = pivot + glm::vec3(cy * cp, sp, sy * cp) * cam_dist_;
    cam.target = pivot;
}

// largest world-space axis scale baked into a matrix (columns 0..2)
static float mat_scale(const glm::mat4& m) {
    return std::max({glm::length(glm::vec3(m[0])),
                     glm::length(glm::vec3(m[1])),
                     glm::length(glm::vec3(m[2]))});
}

// Left-click in the scene picks the nearest mesh under the cursor. Ctrl+click
// toggles it in the multi-selection; a click on empty space clears everything.
void Editor::viewport_pick(CommandContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left)) return;
    if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) return;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) return;

    float nx = 2.0f * io.MousePos.x / std::max(1, win_w_) - 1.0f;
    float ny = 1.0f - 2.0f * io.MousePos.y / std::max(1, win_h_);
    auto& cam = ctx.scene.camera();
    glm::mat4 inv = glm::inverse(cam.proj(win_h_ ? float(win_w_) / win_h_ : 1.0f) * cam.view());
    glm::vec4 a = inv * glm::vec4(nx, ny, -1.0f, 1.0f);
    glm::vec4 b = inv * glm::vec4(nx, ny, 1.0f, 1.0f);
    glm::vec3 o = glm::vec3(a) / a.w;
    glm::vec3 dir = glm::normalize(glm::vec3(b) / b.w - o);

    std::string hit;
    float best = 1e30f;
    for (auto [e, wt, mr] : ctx.scene.registry.view<WorldTransform, MeshRenderer>().each()) {
        if (!mr.gpu) continue;
        auto* n = ctx.scene.registry.try_get<Name>(e);
        if (!n) continue;
        glm::vec3 c = glm::vec3(wt.matrix * glm::vec4(mr.gpu->bounds_center(), 1.0f));
        float r = mr.gpu->bounds_radius() * mat_scale(wt.matrix);
        glm::vec3 oc = o - c;
        float hb = glm::dot(oc, dir);
        float disc = hb * hb - (glm::dot(oc, oc) - r * r);
        if (disc < 0.0f) continue;
        float t = -hb - std::sqrt(disc);
        if (t < 0.0f) t = -hb + std::sqrt(disc);
        if (t < 0.0f || t >= best) continue;
        best = t; hit = n->value;
    }

    if (hit.empty()) {
        if (!io.KeyCtrl) { selected_.clear(); multi_.clear(); }
        return;
    }
    if (io.KeyCtrl && !selected_.empty()) {
        if (hit == selected_) {
            selected_ = multi_.empty() ? std::string() : multi_.front();
            if (!multi_.empty()) multi_.erase(multi_.begin());
        } else if (auto it = std::find(multi_.begin(), multi_.end(), hit); it != multi_.end()) {
            multi_.erase(it);
        } else {
            multi_.push_back(hit);
        }
    } else {
        selected_ = hit; multi_.clear();
    }
}

// F: snap the orbit pivot onto the selection and pull the camera in to frame it.
void Editor::focus_selected(CommandContext& ctx) {
    entt::entity e = ctx.scene.find(selected_);
    if (e == entt::null) return;
    auto& reg = ctx.scene.registry;
    auto* wt = reg.try_get<WorldTransform>(e);
    glm::vec3 p = wt ? wt->position : glm::vec3(0);
    pivot_[0] = p.x; pivot_[1] = p.y; pivot_[2] = p.z;
    float r = 1.2f;
    if (auto* mr = reg.try_get<MeshRenderer>(e); mr && mr->gpu)
        r = mr->gpu->bounds_radius() * (wt ? mat_scale(wt->matrix) : 1.0f);
    cam_dist_ = std::max(3.0f, r * 2.5f);
}

// The scene ships bindings in `input_map`; feed them to the InputSystem when Play
// starts, drop virtual (AI) holds when it stops. Gameplay reads GLFW directly so
// keyboard reaches it over any panel.
void Editor::sync_play_input(CommandContext& ctx) {
    if (play_ == prev_play_) return;
    prev_play_ = play_;
    if (!ctx.input) return;
    ctx.input->clear_virtual();
    if (play_)
        for (auto& [action, keys] : ctx.scene.input_map.items())
            if (keys.is_array())
                ctx.input->bind(action, keys.get<std::vector<std::string>>());
}

void Editor::scan_assets() {
    auto scan = [](const char* dir, std::vector<std::string>& out) {
        out.clear();
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (auto& de : fs::directory_iterator(dir, ec))
            if (de.is_regular_file() && de.path().extension() == ".json")
                out.push_back(de.path().generic_string());
        std::sort(out.begin(), out.end());
    };
    scan("scenes", asset_scenes_);
    scan("prefabs", asset_prefabs_);
    assets_scanned_ = true;
}

// ---------------- layout ----------------
void Editor::build_layout() {
    ImGuiID root = ImGui::GetID("EditorDock");
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

    // Centre stays empty -- the 3D scene shows through it. Panels hug the edges
    // as floating glass cards over the scene.
    ImGuiID left, center, right, bottom, ltop, lbottom, banim, btimeline;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.17f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.22f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, &bottom, &center);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.55f, &ltop, &lbottom);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.30f, &banim, &btimeline);

    ImGui::DockBuilderDockWindow("Palette", ltop);
    ImGui::DockBuilderDockWindow("Assets", ltop);
    ImGui::DockBuilderDockWindow("Hierarchy", lbottom);
    ImGui::DockBuilderDockWindow("Details", right);
    ImGui::DockBuilderDockWindow("Blueprint", center);
    ImGui::DockBuilderDockWindow("Animations", banim);
    ImGui::DockBuilderDockWindow("Timeline", btimeline);
    ImGui::DockBuilderDockWindow("Output Log", btimeline);
    ImGui::DockBuilderFinish(root);
}

// ---------------- top bars ----------------
void Editor::main_menu(CommandContext& ctx) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) { ctx.scene.clear(); ctx.scene.camera(); selected_.clear(); multi_.clear(); }
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##path", save_path_, sizeof(save_path_));
            if (ImGui::MenuItem("Open")) {
                if (ctx.scene.load_file(save_path_)) { ctx.scene_path = save_path_; ctx.physics.sync(ctx.scene); }
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) ctx.scene.save_file(save_path_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty())) do_undo(ctx);
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !redo_.empty())) do_redo(ctx);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Selected", "Del", false, !selected_.empty())) {
                for (auto& s : multi_) ctx.scene.destroy(s);
                ctx.scene.destroy(selected_);
                selected_.clear(); multi_.clear();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Asset")) { ImGui::MenuItem("(scene JSON is the asset)", nullptr, false, false); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid", nullptr, true, false);
            ImGui::Separator();
            bool ssao = ctx.scene.env.value("ssao", true);
            if (ImGui::MenuItem("Ambient Occlusion (SSAO)", nullptr, ssao))
                ctx.scene.env["ssao"] = !ssao;
            if (ssao) {
                float rad = ctx.scene.env.value("ssao_radius", 0.6f);
                float inten = ctx.scene.env.value("ssao_intensity", 1.1f);
                ImGui::SetNextItemWidth(140);
                if (ImGui::SliderFloat("Radius", &rad, 0.1f, 2.0f)) ctx.scene.env["ssao_radius"] = rad;
                ImGui::SetNextItemWidth(140);
                if (ImGui::SliderFloat("Strength", &inten, 0.2f, 3.0f)) ctx.scene.env["ssao_intensity"] = inten;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")) { ImGui::MenuItem("Frame stats", nullptr, false, false); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Window")) { if (ImGui::MenuItem("Reset Layout")) layout_built_ = false; ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Tools")) { ImGui::MenuItem("Console", nullptr, false, false); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Help")) { ImGui::MenuItem("ali-engine editor", nullptr, false, false); ImGui::EndMenu(); }

        // Designer / Graph toggle, right aligned
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(ImGui::GetCursorPosX() + w - 150);
        ImGui::PushStyleColor(ImGuiCol_Button, mode_ == 0 ? ImVec4(0.10f, 0.55f, 0.95f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::SmallButton("Designer")) mode_ = 0;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, mode_ == 1 ? ImVec4(0.10f, 0.55f, 0.95f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Button));
        if (ImGui::SmallButton("Graph")) mode_ = 1;
        ImGui::PopStyleColor();
        ImGui::EndMainMenuBar();
    }
}

void Editor::toolbar(CommandContext& ctx) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y});
    ImGui::SetNextWindowSize({vp->WorkSize.x, 34});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::Begin("##toolbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("Save")) ctx.scene.save_file(save_path_);
    ImGui::SameLine();
    if (ImGui::Button("Compile")) ctx.scene.resolve_gpu_meshes();   // "compile" = re-resolve assets
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    if (ImGui::Button(play_ ? "Stop" : "Play")) play_ = !play_;
    ImGui::SameLine();
    if (ImGui::Button("Step")) { /* handled by main via wants_play; single-step: */ }
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##save", "scene path", save_path_, sizeof(save_path_));
    ImGui::SameLine();
    const auto& s = ctx.renderer.stats();
    ImGui::SameLine(ImGui::GetWindowWidth() - 260);
    ImGui::TextDisabled("%d ents  %d draws  %.2f ms", s.entities, s.draw_calls, s.cpu_ms);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Editor::status_bar(CommandContext& ctx) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - 26});
    ImGui::SetNextWindowSize({vp->WorkSize.x, 26});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextDisabled("Output Log");
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    ImGui::TextDisabled("Cmd");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(420);
    if (ImGui::InputTextWithHint("##cmd", "Enter JSON Command", console_buf_, sizeof(console_buf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        run_console(ctx, console_buf_);
        console_buf_[0] = 0;
        ImGui::SetKeyboardFocusHere(-1);
    }
    const char* gop = gizmo_op_ == 7 ? "Move" : gizmo_op_ == 120 ? "Rotate" : "Scale";
    ImGui::SameLine(ImGui::GetWindowWidth() - 470);
    ImGui::TextDisabled("%.0f FPS   %d ents   sel: %s   %s",
                        ImGui::GetIO().Framerate, (int)ctx.scene.names().size(),
                        selected_.empty() ? "-" : selected_.c_str(), gop);
    ImGui::SameLine(ImGui::GetWindowWidth() - 210);
    ImGui::TextDisabled("sim: %s", play_ ? "running" : "paused");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.10f, 0.55f, 0.95f, 1), "%s", ctx.scene_path.empty() ? "unsaved" : "");
    ImGui::End();
    ImGui::PopStyleVar();
}

// ---------------- panels ----------------
void Editor::panel_palette(CommandContext& ctx) {
    ImGui::Begin("Palette");
    static char filter[64] = {0};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pf", "Search Palette", filter, sizeof(filter));
    auto item = [&](const char* label, const char* prim) {
        if (filter[0] && !ImStristr(label, nullptr, filter, nullptr)) return;
        if (ImGui::Selectable(label)) spawn(ctx, prim);
    };
    if (ImGui::CollapsingHeader("PRIMITIVE", ImGuiTreeNodeFlags_DefaultOpen)) {
        item("  Cube", "cube");
        item("  Sphere", "sphere");
        item("  Plane", "plane");
        item("  Skinned bar", "skinned");
    }
    if (ImGui::CollapsingHeader("LIGHT", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Selectable("  Directional Light"))
            dispatch(ctx, {{"method", "light.add"}, {"params", {{"name", "sun"}, {"type", "directional"}}}});
        if (ImGui::Selectable("  Point Light"))
            dispatch(ctx, {{"method", "light.add"}, {"params", {{"name", "point"}, {"type", "point"}, {"position", {0, 3, 0}}}}});
        if (ImGui::Selectable("  Spot Light"))
            dispatch(ctx, {{"method", "light.add"}, {"params", {{"name", "spot"}, {"type", "spot"}, {"position", {0, 5, 0}}}}});
    }
    if (ImGui::CollapsingHeader("FX", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Selectable("  Particle Emitter"))
            dispatch(ctx, {{"method", "particles.emit"}, {"params", {{"name", "fx"}, {"position", {0, 0.5, 0}}}}});
    }
    if (ImGui::CollapsingHeader("UI", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Selectable("  Panel"))
            dispatch(ctx, {{"method", "ui.add"}, {"params", {{"name", "panel"}, {"kind", "panel"}, {"text", "Panel"}}}});
        if (ImGui::Selectable("  Text"))
            dispatch(ctx, {{"method", "ui.add"}, {"params", {{"name", "label"}, {"kind", "text"}, {"text", "Text"}}}});
        if (ImGui::Selectable("  Progress Bar"))
            dispatch(ctx, {{"method", "ui.add"}, {"params", {{"name", "bar"}, {"kind", "bar"}, {"value", 0.6}}}});
    }
    ImGui::End();
}

void Editor::panel_hierarchy(CommandContext& ctx) {
    ImGui::Begin("Hierarchy");
    static char f[64] = {0};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##hf", "Search", f, sizeof(f));
    auto& reg = ctx.scene.registry;
    if (ImGui::BeginChild("tree")) {
        for (auto [e, n] : reg.view<Name>().each()) {
            if (f[0] && !ImStristr(n.value.c_str(), nullptr, f, nullptr)) continue;

            // component badges: M mesh, L light, B body, A anim, T terrain
            float x0 = ImGui::GetCursorPosX();
            bool any = false;
            auto tag = [&](const char* s, ImVec4 col) {
                ImGui::TextColored(col, "%s", s); ImGui::SameLine(0, 3); any = true;
            };
            if (reg.all_of<MeshRenderer>(e))                     tag("M", ImVec4(0.72f, 0.72f, 0.74f, 1));
            if (reg.any_of<DirectionalLight, PunctualLight>(e))  tag("L", ImVec4(0.95f, 0.80f, 0.32f, 1));
            if (reg.all_of<RigidBody>(e))                        tag("B", ImVec4(0.40f, 0.62f, 1.00f, 1));
            if (reg.any_of<AnimationPlayer, AnimatorController>(e)) tag("A", ImVec4(0.42f, 0.85f, 0.50f, 1));
            if (reg.all_of<TerrainComp>(e))                      tag("T", ImVec4(0.72f, 0.56f, 0.36f, 1));
            if (any) ImGui::SameLine(x0 + 62.0f); else ImGui::SetCursorPosX(x0 + 62.0f);

            if (ImGui::Selectable(n.value.c_str(), is_selected(n.value))) {
                if (ImGui::GetIO().KeyCtrl && !selected_.empty()) {
                    auto it = std::find(multi_.begin(), multi_.end(), n.value);
                    if (n.value == selected_) {
                        // demote primary; promote first extra if any
                        selected_ = multi_.empty() ? std::string() : multi_.front();
                        if (!multi_.empty()) multi_.erase(multi_.begin());
                    } else if (it != multi_.end()) {
                        multi_.erase(it);
                    } else {
                        multi_.push_back(n.value);
                    }
                } else {
                    selected_ = n.value;
                    multi_.clear();
                }
            }
        }
    }
    ImGui::EndChild();
    if (!multi_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%d selected  (Ctrl+click)", (int)multi_.size() + 1);
    }
    ImGui::End();
}

static bool drag3(const char* l, glm::vec3& v, float sp = 0.05f) { return ImGui::DragFloat3(l, &v.x, sp); }

void Editor::panel_details(CommandContext& ctx) {
    ImGui::Begin("Details");
    auto& reg = ctx.scene.registry;
    entt::entity e = ctx.scene.find(selected_);
    if (e == entt::null) { ImGui::TextDisabled("Select an object"); ImGui::End(); return; }

    ImGui::TextUnformatted(selected_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) { ctx.scene.destroy(selected_); selected_.clear(); multi_.clear(); ImGui::End(); return; }

    static char dfilter[64] = {0};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##detf", "Filter properties", dfilter, sizeof(dfilter));
    // a section shows when its own name or any of its row labels matches the filter
    auto sec = [&](const char* title, std::initializer_list<const char*> rows) {
        if (!dfilter[0]) return true;
        if (ImStristr(title, nullptr, dfilter, nullptr)) return true;
        for (const char* r : rows)
            if (ImStristr(r, nullptr, dfilter, nullptr)) return true;
        return false;
    };
    ImGui::Separator();

    if (auto* t = reg.try_get<Transform>(e)) {
        if (sec("Transform", {"Location", "Rotation", "Scale"}) &&
            ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            drag3("Location", t->position);
            drag3("Rotation", t->euler_deg, 0.5f);
            drag3("Scale", t->scale);
        }
    }
    if (auto* mr = reg.try_get<MeshRenderer>(e)) {
        if (sec("Mesh", {"Primitive", "Base Color", "Metallic", "Roughness", "Emissive"}) &&
            ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* prims[] = {"cube", "sphere", "plane", "gltf", "skinned"};
            int cur = 0; for (int i = 0; i < 5; ++i) if (mr->primitive == prims[i]) cur = i;
            if (ImGui::Combo("Primitive", &cur, prims, 5)) { mr->primitive = prims[cur]; ctx.scene.resolve_gpu_meshes(); }
            ImGui::ColorEdit3("Base Color", &mr->base_color.x);
            ImGui::SliderFloat("Metallic", &mr->metallic, 0, 1);
            ImGui::SliderFloat("Roughness", &mr->roughness, 0.02f, 1);
            ImGui::ColorEdit3("Emissive", &mr->emissive.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        }
    }
    if (auto* dl = reg.try_get<DirectionalLight>(e)) {
        if (sec("Directional Light", {"Direction", "Color", "Intensity"}) &&
            ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            drag3("Direction", dl->direction, 0.02f);
            ImGui::ColorEdit3("Color", &dl->color.x);
            ImGui::DragFloat("Intensity", &dl->intensity, 0.05f, 0, 30);
        }
    }
    if (auto* pl = reg.try_get<PunctualLight>(e)) {
        if (sec("Point / Spot Light", {"Spot", "Color", "Intensity", "Range", "Direction", "Inner", "Outer"}) &&
            ImGui::CollapsingHeader("Point / Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Spot", &pl->spot);
            ImGui::ColorEdit3("Color", &pl->color.x);
            ImGui::DragFloat("Intensity", &pl->intensity, 0.2f, 0, 200);
            ImGui::DragFloat("Range", &pl->range, 0.1f, 0.1f, 100);
            if (pl->spot) {
                drag3("Direction", pl->direction, 0.02f);
                ImGui::DragFloat("Inner", &pl->inner_deg, 0.5f, 1, 89);
                ImGui::DragFloat("Outer", &pl->outer_deg, 0.5f, 1, 89);
            }
        }
    }
    if (auto* rb = reg.try_get<RigidBody>(e)) {
        if (sec("Rigid Body", {"Type", "Mass", "Restitution"}) &&
            ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* types[] = {"static", "dynamic", "kinematic"};
            int cur = rb->type == "static" ? 0 : rb->type == "kinematic" ? 2 : 1;
            if (ImGui::Combo("Type", &cur, types, 3)) { rb->type = types[cur]; rb->registered = false; ctx.physics.sync(ctx.scene); }
            ImGui::DragFloat("Mass", &rb->mass, 0.1f, 0.01f, 100);
            ImGui::DragFloat("Restitution", &rb->restitution, 0.02f, 0, 1);
        }
    }
    if (auto* ap = reg.try_get<AnimationPlayer>(e)) {
        if (sec("Animation", {"clip", "Playing", "Speed"}) &&
            ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("clip: %s", ap->clip.c_str());
            ImGui::Checkbox("Playing", &ap->playing);
            ImGui::DragFloat("Speed", &ap->speed, 0.02f, 0, 5);
        }
    }
    ImGui::Separator();
    if (ImGui::Button("+ Mesh") && !reg.all_of<MeshRenderer>(e)) { reg.emplace<MeshRenderer>(e); ctx.scene.resolve_gpu_meshes(); }
    ImGui::SameLine();
    if (ImGui::Button("+ Body") && !reg.all_of<RigidBody>(e)) { reg.emplace<RigidBody>(e); ctx.physics.sync(ctx.scene); }
    ImGui::End();
}

void Editor::panel_viewport(CommandContext& ctx, unsigned) {
    // The scene is the full-window backdrop. In Graph mode a Blueprint window
    // takes the centre; otherwise we host a gizmo + camera overlay over the scene.
    if (mode_ == 1) {
        ImGui::Begin("Blueprint");
        bp_->draw(ctx, selected_);
        ImGui::End();
        return;
    }
    vp_w_ = win_w_;
    vp_h_ = win_h_;

    ImGuiIO& io = ImGui::GetIO();
    bool over_scene = !io.WantCaptureMouse;
    if (over_scene) update_orbit_camera(ctx);

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddText(ImVec2(16, win_h_ - 40.0f), IM_COL32(255, 255, 255, 130),
                play_ ? "PLAYING  --  WASD / Space" : "EDIT");

    entt::entity e = ctx.scene.find(selected_);
    auto* t = e != entt::null ? ctx.scene.registry.try_get<Transform>(e) : nullptr;
    if (t) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(fg);
        ImGuizmo::SetRect(0, 0, (float)win_w_, (float)win_h_);
        auto& cam = ctx.scene.camera();
        glm::mat4 view = cam.view();
        glm::mat4 proj = cam.proj(win_h_ ? float(win_w_) / win_h_ : 1.0f);
        glm::mat4 before = t->matrix();
        glm::mat4 model = before;
        // snap step: per-axis for translate, fixed 15deg / 0.1 for rotate / scale
        static const float kTrSteps[] = {0.1f, 0.25f, 0.5f, 1.0f};
        float step = gizmo_op_ == 7 ? kTrSteps[snap_tr_idx_] : gizmo_op_ == 120 ? 15.0f : 0.1f;
        glm::vec3 snap_v(step);
        const float* snap = gizmo_snap_ ? glm::value_ptr(snap_v) : nullptr;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 (ImGuizmo::OPERATION)gizmo_op_, (ImGuizmo::MODE)gizmo_mode_,
                                 glm::value_ptr(model), nullptr, snap)) {
            glm::vec3 tr, rot, sc;
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), &tr.x, &rot.x, &sc.x);
            t->position = tr; t->euler_deg = rot; t->scale = sc;

            // apply the same world-space delta to every other selected entity
            // (delta comes from the already-snapped primary, so snapping carries)
            if (!multi_.empty()) {
                glm::mat4 delta = model * glm::inverse(before);
                for (const std::string& name : multi_) {
                    entt::entity oe = ctx.scene.find(name);
                    auto* ot = oe != entt::null ? ctx.scene.registry.try_get<Transform>(oe) : nullptr;
                    if (!ot) continue;
                    glm::mat4 nm = delta * ot->matrix();
                    glm::vec3 otr, orot, osc;
                    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(nm), &otr.x, &orot.x, &osc.x);
                    ot->position = otr; ot->euler_deg = orot; ot->scale = osc;
                }
            }
        }
    }

    // click-to-pick: after the gizmo, so IsUsing()/IsOver() reflect this frame
    viewport_pick(ctx);
}

void Editor::panel_animations(CommandContext& ctx) {
    ImGui::Begin("Animations");
    ImGui::RadioButton("Move", &gizmo_op_, 7); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &gizmo_op_, 120); ImGui::SameLine();
    ImGui::RadioButton("Scale", &gizmo_op_, 896);
    ImGui::Checkbox("Snap", &gizmo_snap_);
    if (gizmo_snap_ && gizmo_op_ == 7) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("##snapstep", &snap_tr_idx_, "0.1\0" "0.25\0" "0.5\0" "1.0\0");
    } else if (gizmo_snap_) {
        ImGui::SameLine();
        ImGui::TextDisabled(gizmo_op_ == 120 ? "15 deg" : "0.1");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Skinned entities");
    for (auto [e, mr, ap] : ctx.scene.registry.view<MeshRenderer, AnimationPlayer>().each()) {
        auto* n = ctx.scene.registry.try_get<Name>(e);
        if (!n) continue;
        if (ImGui::Selectable(n->value.c_str(), n->value == selected_anim_)) selected_anim_ = n->value;
    }
    ImGui::End();
}

void Editor::panel_timeline(CommandContext& ctx) {
    ImGui::Begin("Timeline");
    entt::entity e = ctx.scene.find(selected_anim_);
    auto* ap = e != entt::null ? ctx.scene.registry.try_get<AnimationPlayer>(e) : nullptr;
    auto* mr = e != entt::null ? ctx.scene.registry.try_get<MeshRenderer>(e) : nullptr;
    if (!ap || !mr || !mr->skinned) {
        ImVec2 c = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos({c.x * 0.5f - 60, c.y * 0.5f});
        ImGui::TextDisabled("No Animation Selected");
        ImGui::End();
        return;
    }
    float dur = 2.0f;
    auto it = mr->skinned->clips.find(ap->clip.empty() ? mr->skinned->first_clip() : ap->clip);
    if (it != mr->skinned->clips.end()) dur = std::max(0.01f, it->second.duration);

    ImGui::Text("%s  ·  %s", selected_anim_.c_str(), it != mr->skinned->clips.end() ? it->first.c_str() : "?");
    ImGui::SliderFloat("time", &ap->time, 0.0f, dur, "%.2f s");
    bool p = ap->playing;
    if (ImGui::Checkbox("play", &p)) ap->playing = p;
    ImGui::SameLine(); ImGui::SetNextItemWidth(120);
    ImGui::DragFloat("speed", &ap->speed, 0.02f, 0.0f, 5.0f);

    // scrubber strip
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 sz = {ImGui::GetContentRegionAvail().x, 40};
    dl->AddRectFilled(p0, {p0.x + sz.x, p0.y + sz.y}, IM_COL32(20, 20, 22, 255));
    for (float s = 0; s <= dur + 0.001f; s += 0.5f) {
        float x = p0.x + (s / dur) * sz.x;
        dl->AddLine({x, p0.y}, {x, p0.y + sz.y}, IM_COL32(70, 70, 74, 255));
        char b[16]; std::snprintf(b, sizeof(b), "%.1f", s);
        dl->AddText({x + 2, p0.y + 2}, IM_COL32(140, 140, 145, 255), b);
    }
    float px = p0.x + (ap->time / dur) * sz.x;
    dl->AddLine({px, p0.y}, {px, p0.y + sz.y}, IM_COL32(240, 180, 60, 255), 2.0f);
    ImGui::Dummy(sz);
    ImGui::End();
}

void Editor::panel_output(CommandContext& ctx) {
    ImGui::Begin("Output Log");
    if (ImGui::BeginChild("log", ImVec2(0, -28), true)) {
        for (auto& l : console_log_) {
            if (!l.empty() && l[0] == '!') ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", l.c_str());
            else ImGui::TextUnformatted(l.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    static char cb[512] = {0};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##oc", "JSON command", cb, sizeof(cb), ImGuiInputTextFlags_EnterReturnsTrue)) {
        run_console(ctx, cb); cb[0] = 0; ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

void Editor::panel_assets(CommandContext& ctx) {
    ImGui::Begin("Assets");
    if (!assets_scanned_) scan_assets();
    if (ImGui::SmallButton("Refresh")) scan_assets();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("SCENES", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& p : asset_scenes_) {
            std::string stem = fs::path(p).stem().string();
            if (ImGui::Selectable(("  " + stem).c_str())) {
                json r = dispatch(ctx, {{"method", "scene.load"}, {"params", {{"path", p}}}});
                if (r.value("ok", false)) {
                    ctx.physics.sync(ctx.scene);
                    selected_.clear(); multi_.clear();
                }
            }
        }
    }
    if (ImGui::CollapsingHeader("PREFABS", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& p : asset_prefabs_) {
            std::string stem = fs::path(p).stem().string();
            if (ImGui::Selectable(("  " + stem).c_str()))
                dispatch(ctx, {{"method", "prefab.instantiate"}, {"params", {{"path", p}, {"name", stem}}}});
        }
    }
    ImGui::End();
}

// ---------------- frame ----------------
void Editor::draw(CommandContext& ctx, unsigned scene_tex, int, int) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    // dockspace host under the toolbar, above the status bar
    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + 34});
    ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - 34 - 26});
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##dockhost", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                 ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(3);
    ImGuiID dock = ImGui::GetID("EditorDock");
    if (!layout_built_) { build_layout(); layout_built_ = true; }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    // keyboard shortcuts (only when no text field is focused)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                io.KeyShift ? do_redo(ctx) : do_undo(ctx);
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false))
                do_redo(ctx);
        }
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeyAlt) {
            if (mode_ == 0 && ImGui::IsKeyPressed(ImGuiKey_F, false))
                focus_selected(ctx);
            // camera bookmarks: Shift+1..4 store, 1..4 recall
            for (int i = 0; i < 4; ++i) {
                if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) continue;
                CamPose& m = cam_marks_[i];
                if (io.KeyShift) {
                    m = {cam_yaw_, cam_pitch_, cam_dist_, {pivot_[0], pivot_[1], pivot_[2]}, true};
                } else if (m.set) {
                    cam_yaw_ = m.yaw; cam_pitch_ = m.pitch; cam_dist_ = m.dist;
                    pivot_[0] = m.pivot[0]; pivot_[1] = m.pivot[1]; pivot_[2] = m.pivot[2];
                }
            }
        }
    }

    sync_play_input(ctx);

    main_menu(ctx);
    toolbar(ctx);
    status_bar(ctx);

    panel_palette(ctx);
    panel_assets(ctx);
    panel_hierarchy(ctx);
    panel_details(ctx);
    panel_viewport(ctx, scene_tex);
    panel_animations(ctx);
    panel_timeline(ctx);
    panel_output(ctx);

    // ---- liquid-glass: a frosted card behind every panel + the chrome bars ----
    if (blur_tex_) {
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        // top menu strip + toolbar as one bar
        draw_glass_card(bg, blur_tex_, win_w_, win_h_, 0, 0, (float)win_w_,
                        vp->WorkPos.y + 34.0f, 0.0f);
        // status bar
        draw_glass_card(bg, blur_tex_, win_w_, win_h_, 0, (float)win_h_ - 26.0f,
                        (float)win_w_, (float)win_h_, 0.0f);

        // Only the docked tool panels get a card. (ImGuizmo spawns a full-window
        // "gizmo" window; the dock hosts are ##-prefixed -- neither is a panel.)
        static const char* kPanels[] = {"Palette", "Assets", "Hierarchy", "Details", "Blueprint",
                                        "Animations", "Timeline", "Output Log"};
        for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
            if (w->Hidden || !w->WasActive) continue;
            bool is_panel = false;
            for (const char* p : kPanels)
                if (std::strcmp(w->Name, p) == 0) { is_panel = true; break; }
            if (!is_panel) continue;
            // a docked panel sharing a node with tab siblings: skip the inactive tabs
            if (w->DockIsActive && w->DockTabIsVisible == false) continue;
            float in = 5.0f;   // inset so each panel reads as a floating card
            draw_glass_card(bg, blur_tex_, win_w_, win_h_,
                            w->Pos.x + in, w->Pos.y + in,
                            w->Pos.x + w->Size.x - in, w->Pos.y + w->Size.y - in,
                            14.0f);
        }
    }

    commit_history(ctx);
}

} // namespace eng
