#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>

using namespace ftxui;

// Function to split text into lines based on current width
std::vector<std::string> split_text_to_lines(const std::string& text, int width) {
  std::vector<std::string> lines;
  
  std::istringstream stream(text);
  std::string word;
  std::string current_line;
  
  while (stream >> word) {
    // If adding this word exceeds the line width, start a new line
    if (current_line.length() + word.length() + 1 > width) {
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
  
  return lines;
}

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
    "vel illum qui dolorem eum fugiat quo voluptas nulla pariatur?"
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

  // Track scroll position
  int top_line = 0;
  int visible_lines = 10;  // Initial value, will be updated based on terminal size
  int track_height = visible_lines - 2;  // Initially set, will be updated
  
  // Dynamically calculated values
  std::vector<std::string> lines;
  int total_lines = 0;
  int current_width = 0;
  
  // Mouse state tracking
  bool dragging = false;
  int drag_start_y = 0;
  int drag_start_top_line = 0;
  
  // Box coordinates for mouse interaction
  Box up_arrow_box;
  Box down_arrow_box;
  Box thumb_box;
  std::vector<Box> track_boxes(track_height);
  
  // Create a component
  auto component = Renderer([&](bool focused) -> Element {
    // Get dimensions from the terminal
    auto screen_size = Terminal::Size();
    int available_width = screen_size.dimx - 7;  // Account for borders and scrollbar
    int available_height = screen_size.dimy - 2;  // Account for borders
    
    // Update visible lines based on available height
    visible_lines = available_height;
    track_height = std::max(1, visible_lines - 2);  // Minus arrows
    
    // Check if we need to re-split text (when width changes)
    if (current_width != available_width) {
      current_width = available_width;
      lines = split_text_to_lines(long_text, current_width);
      total_lines = lines.size();
      
      // Reset scroll position if needed
      if (top_line > total_lines - visible_lines) {
        top_line = std::max(0, total_lines - visible_lines);
      }
    }
    
    // Adjust track_boxes size
    track_boxes.resize(track_height);
    
    // Calculate scrollbar position
    float scroll_ratio = total_lines <= visible_lines ? 
                         0.0f : 
                         (float)top_line / (total_lines - visible_lines);
    float scroll_thumb_size = std::min(1.0f, (float)visible_lines / std::max(1, total_lines));
    
    // Create visible text elements
    std::vector<Element> text_elements;
    for (int i = top_line; i < std::min(top_line + visible_lines, total_lines); i++) {
      text_elements.push_back(text(lines[i]));
    }
    
    // Fill remaining space with empty lines if needed
    for (int i = text_elements.size(); i < visible_lines; i++) {
      text_elements.push_back(text(""));
    }
    
    // Create scrollbar elements
    std::vector<Element> scrollbar_elements;
    
    // Add top arrow
    auto up_arrow = text("▲") | reflect(up_arrow_box);
    scrollbar_elements.push_back(up_arrow);
    
    // Scrollbar track with thumb
    int thumb_height = std::max(1, (int)(track_height * scroll_thumb_size));
    int thumb_position = (int)(scroll_ratio * (track_height - thumb_height));
    
    // Build the scrollbar track with thumb
    for (int i = 0; i < track_height; i++) {
      if (i >= thumb_position && i < thumb_position + thumb_height) {
        // Thumb section
        if (i == thumb_position) {
          // Only capture the top of the thumb for dragging
          scrollbar_elements.push_back(text("█") | reflect(thumb_box));
        } else {
          scrollbar_elements.push_back(text("█") | reflect(track_boxes[i]));
        }
      } else {
        // Track sections
        scrollbar_elements.push_back(text("│") | reflect(track_boxes[i]));
      }
    }
    
    // Add bottom arrow
    auto down_arrow = text("▼") | reflect(down_arrow_box);
    scrollbar_elements.push_back(down_arrow);
    
    // Return the combined elements with flex to handle resizing
    return hbox({
               vbox(text_elements) | flex,
               vbox(scrollbar_elements)
             }) | border;
  });
  
  // Add event handling
  component = CatchEvent(component, [&](Event event) {
    // Handle keyboard events
    if (event == Event::ArrowUp && top_line > 0) {
      top_line--;
      return true;
    }
    if (event == Event::ArrowDown && top_line < total_lines - visible_lines) {
      top_line++;
      return true;
    }
    if (event == Event::PageUp) {
      top_line = std::max(0, top_line - visible_lines);
      return true;
    }
    if (event == Event::PageDown) {
      top_line = std::min(total_lines - visible_lines, top_line + visible_lines);
      return true;
    }
    if (event == Event::Home) {
      top_line = 0;
      return true;
    }
    if (event == Event::End) {
      top_line = std::max(0, total_lines - visible_lines);
      return true;
    }
    
    // Handle mouse events
    if (event.is_mouse()) {
      // Get mouse position
      int mouse_x = event.mouse().x;
      int mouse_y = event.mouse().y;
      
      // Handle mouse down events
      if (event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Pressed) {
        // Click on up arrow
        if (up_arrow_box.Contain(mouse_x, mouse_y)) {
          if (top_line > 0) top_line--;
          return true;
        }
        
        // Click on down arrow
        if (down_arrow_box.Contain(mouse_x, mouse_y)) {
          if (top_line < total_lines - visible_lines) top_line++;
          return true;
        }
        
        // Click on thumb to start dragging
        if (thumb_box.Contain(mouse_x, mouse_y)) {
          dragging = true;
          drag_start_y = mouse_y;
          drag_start_top_line = top_line;
          return true;
        }
        
        // Click on track to page up/down
        for (size_t i = 0; i < track_boxes.size(); i++) {
          if (track_boxes[i].Contain(mouse_x, mouse_y)) {
            // Calculate scrollbar position
            float scroll_ratio = (float)top_line / std::max(1, total_lines - visible_lines);
            float scroll_thumb_size = std::min(1.0f, (float)visible_lines / total_lines);
            int thumb_height = std::max(1, (int)(track_height * scroll_thumb_size));
            int thumb_position = (int)(scroll_ratio * (track_height - thumb_height));
            
            // Page up if clicking above thumb
            if ((int)i < thumb_position) {
              top_line = std::max(0, top_line - visible_lines);
              return true;
            }
            
            // Page down if clicking below thumb
            if ((int)i >= thumb_position + thumb_height) {
              top_line = std::min(total_lines - visible_lines, top_line + visible_lines);
              return true;
            }
            
            break;
          }
        }
      }
      
      // Handle mouse move events when dragging
      if (dragging && event.mouse().motion == Mouse::Moved) {
        int delta_y = mouse_y - drag_start_y;
        float delta_ratio = (float)delta_y / track_height;
        int new_top_line = drag_start_top_line + delta_ratio * (total_lines - visible_lines);
        
        // Clamp the new top line value
        top_line = std::max(0, std::min(total_lines - visible_lines, new_top_line));
        return true;
      }
      
      // Handle mouse up events to end dragging
      if (event.mouse().button == Mouse::Left && event.mouse().motion == Mouse::Released) {
        dragging = false;
        return true;
      }
    }
    
    return false;
  });
  
  auto screen = ScreenInteractive::TerminalOutput();
  
  // Add resize handler to adjust text and layout
  component |= CatchEvent([&](Event event) {
    if (event == Event::Custom) {
      // Terminal size might have changed, force redrawing
      return true;
    }
    return false;
  });
  
  screen.Loop(component);
  
  return 0;
}
