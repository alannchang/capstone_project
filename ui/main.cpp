#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;

const std::string MAIMAIL_VERSION = "v1.0.0";

int main() {
  auto screen = ScreenInteractive::Fullscreen();

  // Menu
  int selected = 0;
  std::vector<std::string> options = {"Option 1", "Option 2", "Option 3"};
  auto menu = Menu(&options, &selected);

  // User Input Text Box
  std::string command_text = "";
  std::string input_string;
  Component user_input = Input(&input_string, "Type commands here...") |
      border |
      CatchEvent([&](Event event) {
          if (event == Event::Return && !input_string.empty()) {
              // Send prompt to model here
              command_text += " " + input_string;
              input_string.clear();
              return true;
          }
          return false;
      });

  auto container = Container::Vertical({
      menu, 
      user_input,
  });

  auto renderer = Renderer(container, [&] {
    return vbox({
        text("MaiMail " + MAIMAIL_VERSION) | center,
        separator(),
        // menu->Render() | flex,
        paragraphAlignLeft(command_text) | flex | border,
        separator(),
        user_input->Render()
    });
  });

  screen.Loop(renderer);
  return 0;
}
