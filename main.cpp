#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  int selected = 0;
  std::vector<std::string> options = {"Option 1", "Option 2", "Option 3"};

  auto menu = Menu(&options, &selected);
  std::string input_string;
  auto user_input = Input(&input_string, "Type commands here...") | border;

  auto container = Container::Vertical({
      menu, 
      user_input,
  });

  auto renderer = Renderer(container, [&] {
    return vbox({
        text("Welcome to MaiMail") | center,
        separator(),
        menu->Render() | flex,
        separator(),
        user_input->Render()
    });
  });

  screen.Loop(renderer);
  return 0;
}
