import curses
import time

def main(stdscr):
    # Set up the screen
    curses.curs_set(1)  # Show the cursor
    stdscr.clear()

    # Get screen dimensions
    height, width = stdscr.getmaxyx()

    # Define height for status bar and input box (at the bottom)
    status_bar_height = 1
    input_box_height = 1  # Keep input box at height 1 character
    total_bottom_height = status_bar_height + input_box_height

    # Create windows for status bar and input box
    status_win = curses.newwin(status_bar_height, width, height - total_bottom_height, 0)
    input_box_win = curses.newwin(input_box_height, width, height - input_box_height, 0)

    # Set color pair for the status bar (inverted colors)
    curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_WHITE)  # Black text on white background
    status_win.bkgd(' ', curses.color_pair(1))  # Apply inverted colors
    status_win.addstr(0, 0, "Status Bar | Press ESC to quit")

    # Create a window for the title and menu
    menu_win = curses.newwin(height - total_bottom_height - 3, width, 0, 0)

    # Title and separator
    title = "Main Application"
    separator = "-" * width

    # Menu items
    menu_items = ["Home", "Settings", "About", "Exit"]
    current_menu_item = 0

    # Display the title and separator on the menu window
    menu_win.addstr(0, 0, title, curses.A_BOLD)
    menu_win.addstr(1, 0, separator)

    # Display menu items on the menu window
    for i, item in enumerate(menu_items):
        if i == current_menu_item:
            menu_win.addstr(i + 2, 0, f"> {item}", curses.A_REVERSE)  # Highlight selected item
        else:
            menu_win.addstr(i + 2, 0, f"  {item}")

    # Initial screen refresh to show everything
    stdscr.refresh()
    menu_win.refresh()
    status_win.refresh()
    input_box_win.refresh()

    # Initial empty input string
    input_str = ""
    input_box_active = True  # Allow input in the input box
    scroll_pos = 0  # Track the current scroll position (horizontal scroll)

    # Main loop
    while True:
        # Display input box with current input string
        input_box_win.clear()

        # Handle horizontal scrolling if the input text overflows the box
        if len(input_str) > width:
            scroll_pos = len(input_str) - width
            input_box_win.addstr(0, 0, input_str[scroll_pos:], curses.A_BOLD)  # Scroll horizontally
        else:
            input_box_win.addstr(0, 0, input_str, curses.A_BOLD)  # No horizontal scroll
        
        input_box_win.refresh()

        # Wait for user input
        key = stdscr.getch()

        # Exit on Esc key (ASCII 27)
        if key == 27:  # ESC key
            break

        # Handle arrow keys for menu navigation
        elif key == curses.KEY_DOWN:  # Down arrow
            if current_menu_item < len(menu_items) - 1:  # Prevent going beyond the menu
                current_menu_item += 1

        elif key == curses.KEY_UP:  # Up arrow
            if current_menu_item > 0:
                current_menu_item -= 1

        # Handle Enter key to select a menu item
        elif key == 10:  # Enter key
            if menu_items[current_menu_item] == "Exit":
                break
            else:
                menu_win.clear()
                menu_win.addstr(0, 0, title, curses.A_BOLD)
                menu_win.addstr(1, 0, separator)
                menu_win.addstr(2, 0, f"You selected: {menu_items[current_menu_item]}")
                menu_win.refresh()
                time.sleep(2)  # Display selection message briefly
                menu_win.clear()
                menu_win.addstr(0, 0, title, curses.A_BOLD)
                menu_win.addstr(1, 0, separator)
                for i, item in enumerate(menu_items):
                    if i == current_menu_item:
                        menu_win.addstr(i + 2, 0, f"> {item}", curses.A_REVERSE)  # Highlight selected item
                    else:
                        menu_win.addstr(i + 2, 0, f"  {item}")
                menu_win.refresh()

        # Handle Backspace (KEY_BACKSPACE) for deleting characters in input box
        elif key == curses.KEY_BACKSPACE:
            if input_box_active:  # Delete the last character
                input_str = input_str[:-1]

        # Handle Delete (KEY_DC) for deleting characters in input box
        elif key == curses.KEY_DC:  # Delete key
            if input_box_active:
                input_str = input_str[:-1]

        # Add other characters to the input string if input box is active
        elif key != curses.ERR and input_box_active:
            input_str += chr(key)

        # Refresh menu after updating
        menu_win.clear()
        menu_win.addstr(0, 0, title, curses.A_BOLD)
        menu_win.addstr(1, 0, separator)
        for i, item in enumerate(menu_items):
            if i == current_menu_item:
                menu_win.addstr(i + 2, 0, f"> {item}", curses.A_REVERSE)  # Highlight selected item
            else:
                menu_win.addstr(i + 2, 0, f"  {item}")
        menu_win.refresh()

        # Refresh status bar with time
        status_win.clear()
        status_win.addstr(0, 0, f"Status Bar | Time: {time.strftime('%H:%M:%S')}")
        status_win.refresh()

# Initialize curses wrapper
curses.wrapper(main)
