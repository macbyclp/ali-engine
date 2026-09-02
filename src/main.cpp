#include "engine/app.hpp"
#include "game/game.hpp"

int main() {
    eng::App app(1280, 720, "ali-engine");
    app.run(make_game());
    return 0;
}
