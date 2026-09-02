#include "editor/editor.hpp"
#include "core/log.hpp"
#include "scene/transform_system.hpp"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>

using nlohmann::json;

namespace eng {

Editor::Editor(GLFWwindow* window) : window_(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");
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
static bool drag_vec3(const char* label, glm::vec3& v, float speed = 0.05f) {
    return ImGui::DragFloat3(label, &v.x, speed);
}

void Editor::run_console(CommandContext& ctx, const std::string& line) {
    try {
        json req = json::parse(line);
        json res = dispatch(ctx, req);
        console_log_.push_back("> " + line);
        console_log_.push_back("  " + res.dump());
    } catch (const std::exception& ex) {
        console_log_.push_back("! " + std::string(ex.what()));
    }
    if (console_log_.size() > 200) console_log_.erase(console_log_.begin(),
                                                      console_log_.begin() + 100);
}

// ---------------- orbit camera ----------------
void Editor::update_orbit_camera(CommandContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        cam_yaw_ += io.MouseDelta.x * 0.4f;
        cam_pitch_ = std::fmax(-89.0f, std::fmin(89.0f, cam_pitch_ - io.MouseDelta.y * 0.4f));
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        float cy = std::cos(glm::radians(cam_yaw_)), sy = std::sin(glm::radians(cam_yaw_));
        pivot_[0] -= (io.MouseDelta.x * cy) * cam_dist_ * 0.002f;
        pivot_[2] -= (io.MouseDelta.x * sy) * cam_dist_ * 0.002f;
        pivot_[1] += io.MouseDelta.y * cam_dist_ * 0.002f;
    }
    if (io.MouseWheel != 0.0f)
        cam_dist_ = std::fmax(1.0f, cam_dist_ * (1.0f - io.MouseWheel * 0.1f));

    float cy = std::cos(glm::radians(cam_yaw_)), sy = std::sin(glm::radians(cam_yaw_));
    float cp = std::cos(glm::radians(cam_pitch_)), sp = std::sin(glm::radians(cam_pitch_));
    glm::vec3 pivot(pivot_[0], pivot_[1], pivot_[2]);
    glm::vec3 eye = pivot + glm::vec3(cy * cp, sp, sy * cp) * cam_dist_;
    auto& cam = ctx.scene.camera();
    cam.position = eye;
    cam.target = pivot;
}

// ---------------- panels ----------------
void Editor::menu_bar(CommandContext& ctx) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New scene")) { ctx.scene.clear(); ctx.scene.camera(); selected_.clear(); }
            ImGui::InputText("path", save_path_, sizeof(save_path_));
            if (ImGui::MenuItem("Open")) {
                if (ctx.scene.load_file(save_path_)) { ctx.scene_path = save_path_; ctx.physics.sync(ctx.scene); }
            }
            if (ImGui::MenuItem("Save")) ctx.scene.save_file(save_path_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Add")) {
            const char* prims[] = {"cube", "sphere", "plane"};
            for (const char* p : prims)
                if (ImGui::MenuItem(p)) {
                    json r = dispatch(ctx, {{"method", "entity.spawn"}, {"params", {{"primitive", p}, {"position", {0, 1, 0}}}}});
                    if (r.value("ok", false)) selected_ = r["result"].value("name", std::string());
                }
            if (ImGui::MenuItem("directional light"))
                dispatch(ctx, {{"method", "light.add"}, {"params", {{"name", "light"}, {"type", "directional"}}}});
            if (ImGui::MenuItem("point light"))
                dispatch(ctx, {{"method", "light.add"}, {"params", {{"name", "point"}, {"type", "point"}, {"position", {0, 3, 0}}}}});
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(play_ ? "Pause" : "Play")) play_ = !play_;
        ImGui::TextDisabled("  |  RMB orbit  MMB pan  wheel zoom");
        ImGui::EndMainMenuBar();
    }
}

void Editor::panel_hierarchy(CommandContext& ctx) {
    ImGui::SetNextWindowPos({8, 32}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 300}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");
    for (auto [e, n] : ctx.scene.registry.view<Name>().each()) {
        bool sel = (n.value == selected_);
        if (ImGui::Selectable(n.value.c_str(), sel)) selected_ = n.value;
    }
    ImGui::End();
}

void Editor::panel_inspector(CommandContext& ctx) {
    ImGui::SetNextWindowPos({8, 340}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({260, 420}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    auto& reg = ctx.scene.registry;
    entt::entity e = ctx.scene.find(selected_);
    if (e == entt::null) { ImGui::TextDisabled("no selection"); ImGui::End(); return; }

    ImGui::Text("%s", selected_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("delete")) {
        ctx.scene.destroy(selected_); selected_.clear(); ImGui::End(); return;
    }
    ImGui::Separator();

    if (auto* t = reg.try_get<Transform>(e)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            drag_vec3("position", t->position);
            drag_vec3("rotation", t->euler_deg, 0.5f);
            drag_vec3("scale", t->scale);
        }
    }
    if (auto* mr = reg.try_get<MeshRenderer>(e)) {
        if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* prims[] = {"cube", "sphere", "plane", "gltf", "skinned"};
            int cur = 0;
            for (int i = 0; i < 5; ++i) if (mr->primitive == prims[i]) cur = i;
            if (ImGui::Combo("primitive", &cur, prims, 5)) {
                mr->primitive = prims[cur];
                ctx.scene.resolve_gpu_meshes();
            }
            ImGui::ColorEdit3("base color", &mr->base_color.x);
            ImGui::SliderFloat("metallic", &mr->metallic, 0.0f, 1.0f);
            ImGui::SliderFloat("roughness", &mr->roughness, 0.02f, 1.0f);
            ImGui::ColorEdit3("emissive", &mr->emissive.x,
                              ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        }
    }
    if (auto* dl = reg.try_get<DirectionalLight>(e)) {
        if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
            drag_vec3("direction", dl->direction, 0.02f);
            ImGui::ColorEdit3("color", &dl->color.x);
            ImGui::DragFloat("intensity", &dl->intensity, 0.05f, 0.0f, 30.0f);
        }
    }
    if (auto* pl = reg.try_get<PunctualLight>(e)) {
        if (ImGui::CollapsingHeader("Point/Spot light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("spot", &pl->spot);
            ImGui::ColorEdit3("color", &pl->color.x);
            ImGui::DragFloat("intensity", &pl->intensity, 0.2f, 0.0f, 200.0f);
            ImGui::DragFloat("range", &pl->range, 0.1f, 0.1f, 100.0f);
            if (pl->spot) {
                drag_vec3("direction", pl->direction, 0.02f);
                ImGui::DragFloat("inner", &pl->inner_deg, 0.5f, 1.0f, 89.0f);
                ImGui::DragFloat("outer", &pl->outer_deg, 0.5f, 1.0f, 89.0f);
            }
        }
    }
    if (auto* rb = reg.try_get<RigidBody>(e)) {
        if (ImGui::CollapsingHeader("Rigid body", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* types[] = {"static", "dynamic", "kinematic"};
            int cur = rb->type == "static" ? 0 : rb->type == "kinematic" ? 2 : 1;
            if (ImGui::Combo("type", &cur, types, 3)) { rb->type = types[cur]; rb->registered = false; ctx.physics.sync(ctx.scene); }
            ImGui::DragFloat("mass", &rb->mass, 0.1f, 0.01f, 100.0f);
            ImGui::DragFloat("restitution", &rb->restitution, 0.02f, 0.0f, 1.0f);
        }
    }
    ImGui::Separator();
    if (ImGui::Button("+ Mesh") && !reg.all_of<MeshRenderer>(e)) {
        reg.emplace<MeshRenderer>(e); ctx.scene.resolve_gpu_meshes();
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Body") && !reg.all_of<RigidBody>(e)) {
        reg.emplace<RigidBody>(e); ctx.physics.sync(ctx.scene);
    }
    ImGui::End();
}

void Editor::panel_viewport(CommandContext& ctx, unsigned tex, int tw, int th) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::SetNextWindowPos({276, 32}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({1024, 620}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Viewport");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    vp_w_ = std::max(16, (int)avail.x);
    vp_h_ = std::max(16, (int)avail.y);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0, 1), ImVec2(1, 0));
    bool hovered = ImGui::IsItemHovered();

    if (hovered) update_orbit_camera(ctx);

    // gizmo on the selected entity
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
        ImGuizmo::OPERATION op = (ImGuizmo::OPERATION)gizmo_op_;
        if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op,
                                 (ImGuizmo::MODE)gizmo_mode_, glm::value_ptr(model))) {
            glm::vec3 tr, rot, sc;
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), &tr.x, &rot.x, &sc.x);
            t->position = tr; t->euler_deg = rot; t->scale = sc;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::panel_console(CommandContext& ctx) {
    ImGui::SetNextWindowPos({276, 660}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({1024, 260}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Console");
    ImGui::RadioButton("move", &gizmo_op_, 7); ImGui::SameLine();
    ImGui::RadioButton("rotate", &gizmo_op_, 120); ImGui::SameLine();
    ImGui::RadioButton("scale", &gizmo_op_, 896);
    ImGui::Separator();
    ImGui::BeginChild("log", ImVec2(0, -28), true);
    for (auto& l : console_log_) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    if (ImGui::InputText("json", console_buf_, sizeof(console_buf_),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        run_console(ctx, console_buf_);
        console_buf_[0] = 0;
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

void Editor::draw(CommandContext& ctx, unsigned scene_tex, int tw, int th) {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    menu_bar(ctx);
    panel_viewport(ctx, scene_tex, tw, th);
    panel_hierarchy(ctx);
    panel_inspector(ctx);
    panel_console(ctx);

    ImGui::SetNextWindowPos({1308, 32}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({284, 150}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Stats");
    const auto& s = ctx.renderer.stats();
    ImGui::Text("entities %d  visible %d  culled %d", s.entities, s.visible, s.culled);
    ImGui::Text("draw calls %d  instances %d", s.draw_calls, s.instances);
    ImGui::Text("cpu %.2f ms", s.cpu_ms);
    ImGui::Text("sim: %s", play_ ? "running" : "paused");
    ImGui::End();
}

} // namespace eng
