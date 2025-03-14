#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>

using namespace ftxui;

int main() {
  // Sample long paragraph text
  std::string long_text = 
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
    "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. "
    "Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. "
    "Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum. "
    "Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam, "
    "eaque ipsa quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt explicabo. "
    "Nemo enim ipsam voluptatem quia voluptas sit aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos qui ratione voluptatem sequi nesciunt. "
    "Neque porro quisquam est, qui dolorem ipsum quia dolor sit amet, consectetur, adipisci velit, "
    "sed quia non numquam eius modi tempora incidunt ut labore et dolore magnam aliquam quaerat voluptatem. "
    "Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? "
    "Quis autem vel eum iure reprehenderit qui in ea voluptate velit esse quam nihil molestiae consequatur, "
    "vel illum qui dolorem eum fugiat quo voluptas nulla pariatur?";

  // Split the text into lines of appropriate width
  const int max_line_width = 60; // Adjust based on your terminal width
  std::vector<std::string> lines;
  
  // Break the text into words
  std::istringstream stream(long_text);
  std::string word;
  std::string current_line;
  
  while (stream >> word) {
    // If adding this word exceeds the line width, start a new line
    if (current_line.length() + word.length() + 1 > max_line_width) {
      lines.push_back(current_line);
      current_line = word;
    } else {
      // Add word to current line with a space
      if (!current_line.empty()) {
        current_line += " ";
      }
      current_line += word;
    }
  }
  
  // Add the last line if not empty
  if (!current_line.empty()) {
    lines.push_back(current_line);
  }
  
  // Track which line is at the top of the visible area
  int top_line = 0;
  const int visible_lines = 10; // Number of lines visible at once
  
  // Create a component
  auto component = Renderer([&] {
    // Determine visible range
    int end = std::min((int)lines.size(), top_line + visible_lines);
    
    // Create elements for visible lines
    std::vector<Element> elements;
    
    // Add lines of text
    for (int i = top_line; i < end; i++) {
      elements.push_back(text(lines[i]));
    }
    
    // Add scrollbar indicators
    std::string scrollbar = "";
    if (top_line > 0) scrollbar += "↑ ";
    scrollbar += "(" + std::to_string(top_line + 1) + "-" + 
                 std::to_string(std::min((int)lines.size(), top_line + visible_lines)) + 
                 "/" + std::to_string(lines.size()) + ")";
    if (top_line + visible_lines < (int)lines.size()) scrollbar += " ↓";
    
    return vbox({
      text("Use Up/Down arrow keys to scroll"),
      vbox(elements) | border,
      text(scrollbar) | center
    });
  });
  
  // Add event handling
  component = CatchEvent(component, [&](Event event) {
    if (event == Event::ArrowUp && top_line > 0) {
      top_line--;
      return true;
    }
    if (event == Event::ArrowDown && top_line < (int)lines.size() - visible_lines) {
      top_line++;
      return true;
    }
    if (event == Event::PageUp) {
      top_line = std::max(0, top_line - visible_lines);
      return true;
    }
    if (event == Event::PageDown) {
      top_line = std::min((int)lines.size() - visible_lines, top_line + visible_lines);
      return true;
    }
    if (event == Event::Home) {
      top_line = 0;
      return true;
    }
    if (event == Event::End) {
      top_line = std::max(0, (int)lines.size() - visible_lines);
      return true;
    }
    return false;
  });
  
  auto screen = ScreenInteractive::TerminalOutput();
  screen.Loop(component);
  
  return 0;
}
