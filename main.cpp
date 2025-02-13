#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  int selected = 0;
  std::vector<std::string> options = {"Option 1", "Option 2", "Option 3", "Quit"};

  auto menu = Menu(&options, &selected);

  auto renderer = Renderer(menu, [&] {
    return vbox({
        text("Simple FTXUI TUI") | bold | center,
        separator(),
        menu->Render(),
    });
  });

  screen.Loop(CatchEvent(renderer, [&](Event event) {
    if (event == Event::Return && selected == 3) {
      screen.ExitLoopClosure()();
      return true;
    }
    return false;
  }));

  return 0;
}
