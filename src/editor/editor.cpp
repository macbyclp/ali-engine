#include "editor/editor.hpp"
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

using nlohmann::json;

namespace eng {

Editor::Editor(GLFWwindow* window) : window_(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;   // we build our own layout every run
    apply_unreal_theme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
    bp_ = std::make_unique<BlueprintEditor>();
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
    if (r.value("ok", false)) selected_ = r["result"].value("name", std::string());
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

// ---------------- layout ----------------
void Editor::build_layout() {
    ImGuiID root = ImGui::GetID("EditorDock");
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);

    ImGuiID left, center, right, bottom, ltop, lbottom, banim, btimeline;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.19f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.30f, &bottom, &center);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.55f, &ltop, &lbottom);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.28f, &banim, &btimeline);

    ImGui::DockBuilderDockWindow("Palette", ltop);
    ImGui::DockBuilderDockWindow("Hierarchy", lbottom);
    ImGui::DockBuilderDockWindow("Details", right);
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Animations", banim);
    ImGui::DockBuilderDockWindow("Timeline", btimeline);
    ImGui::DockBuilderDockWindow("Output Log", btimeline);
    ImGui::DockBuilderFinish(root);
}

// ---------------- top bars ----------------
void Editor::main_menu(CommandContext& ctx) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) { ctx.scene.clear(); ctx.scene.camera(); selected_.clear(); }
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("##path", save_path_, sizeof(save_path_));
            if (ImGui::MenuItem("Open")) {
                if (ctx.scene.load_file(save_path_)) { ctx.scene_path = save_path_; ctx.physics.sync(ctx.scene); }
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) ctx.scene.save_file(save_path_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Delete Selected", "Del", false, !selected_.empty())) {
                ctx.scene.destroy(selected_); selected_.clear();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Asset")) { ImGui::MenuItem("(scene JSON is the asset)", nullptr, false, false); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Grid", nullptr, true, false);
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
    if (ImGui::BeginChild("tree")) {
        for (auto [e, n] : ctx.scene.registry.view<Name>().each()) {
            if (f[0] && !ImStristr(n.value.c_str(), nullptr, f, nullptr)) continue;
            if (ImGui::Selectable(n.value.c_str(), n.value == selected_))
                selected_ = n.value;
        }
    }
    ImGui::EndChild();
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
    if (ImGui::SmallButton("Delete")) { ctx.scene.destroy(selected_); selected_.clear(); ImGui::End(); return; }
    ImGui::Separator();

    if (auto* t = reg.try_get<Transform>(e)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            drag3("Location", t->position);
            drag3("Rotation", t->euler_deg, 0.5f);
            drag3("Scale", t->scale);
        }
    }
    if (auto* mr = reg.try_get<MeshRenderer>(e)) {
        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            drag3("Direction", dl->direction, 0.02f);
            ImGui::ColorEdit3("Color", &dl->color.x);
            ImGui::DragFloat("Intensity", &dl->intensity, 0.05f, 0, 30);
        }
    }
    if (auto* pl = reg.try_get<PunctualLight>(e)) {
        if (ImGui::CollapsingHeader("Point / Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
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
        if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* types[] = {"static", "dynamic", "kinematic"};
            int cur = rb->type == "static" ? 0 : rb->type == "kinematic" ? 2 : 1;
            if (ImGui::Combo("Type", &cur, types, 3)) { rb->type = types[cur]; rb->registered = false; ctx.physics.sync(ctx.scene); }
            ImGui::DragFloat("Mass", &rb->mass, 0.1f, 0.01f, 100);
            ImGui::DragFloat("Restitution", &rb->restitution, 0.02f, 0, 1);
        }
    }
    if (auto* ap = reg.try_get<AnimationPlayer>(e)) {
        if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
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

void Editor::panel_viewport(CommandContext& ctx, unsigned tex) {
    if (mode_ == 1) {   // Graph mode: Blueprint node editor fills the centre
        ImGui::Begin("Viewport");
        bp_->draw(ctx, selected_);
        ImGui::End();
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    vp_w_ = std::max(16, (int)avail.x);
    vp_h_ = std::max(16, (int)avail.y);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0, 1), ImVec2(1, 0));
    bool hovered = ImGui::IsItemHovered();

    // ruler overlay (like UMG's canvas)
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 tick = IM_COL32(120, 120, 125, 90);
    for (float x = 0; x < avail.x; x += 100) {
        dl->AddLine({pos.x + x, pos.y}, {pos.x + x, pos.y + 6}, tick);
        char b[16]; std::snprintf(b, sizeof(b), "%d", (int)x);
        dl->AddText({pos.x + x + 2, pos.y + 2}, tick, b);
    }
    for (float y = 0; y < avail.y; y += 100) {
        dl->AddLine({pos.x, pos.y + y}, {pos.x + 6, pos.y + y}, tick);
    }
    dl->AddText({pos.x + 8, pos.y + avail.y - 18}, IM_COL32(150, 150, 150, 120),
                play_ ? "PLAYING" : "EDIT");

    if (hovered) update_orbit_camera(ctx);

    entt::entity e = ctx.scene.find(selected_);
    auto* t = e != entt::null ? ctx.scene.registry.try_get<Transform>(e) : nullptr;
    if (t) {
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(pos.x, pos.y, avail.x, avail.y);
        auto& cam = ctx.scene.camera();
        glm::mat4 view = cam.view();
        glm::mat4 proj = cam.proj(avail.y > 0 ? avail.x / avail.y : 1.0f);
        glm::mat4 model = t->matrix();
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj),
                                 (ImGuizmo::OPERATION)gizmo_op_, (ImGuizmo::MODE)gizmo_mode_,
                                 glm::value_ptr(model))) {
            glm::vec3 tr, rot, sc;
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), &tr.x, &rot.x, &sc.x);
            t->position = tr; t->euler_deg = rot; t->scale = sc;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::panel_animations(CommandContext& ctx) {
    ImGui::Begin("Animations");
    ImGui::RadioButton("Move", &gizmo_op_, 7); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &gizmo_op_, 120); ImGui::SameLine();
    ImGui::RadioButton("Scale", &gizmo_op_, 896);
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

    main_menu(ctx);
    toolbar(ctx);
    status_bar(ctx);

    panel_palette(ctx);
    panel_hierarchy(ctx);
    panel_details(ctx);
    panel_viewport(ctx, scene_tex);
    panel_animations(ctx);
    panel_timeline(ctx);
    panel_output(ctx);
}

} // namespace eng
