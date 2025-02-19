import curses
import random
import time
import requests

menu = ['Tell Me a Dad Joke', 'Update Windows', 'Exit']

def fetch_joke():
   return requests.get('https://icanhazdadjoke.com/', headers={'Accept': 'text/plain'}).content.decode('U8')


def make_me_laugh(stdscr):
   stdscr.clear()
   while True:
       # i’m using try/except to get rid of exceptions from huge texts
       try:
           setup = joke = fetch_joke()
           punchline = ''
           if '?' in joke:
               setup, punchline = [str(line) for line in joke.split('?')]


           height, width = stdscr.getmaxyx()
           x_setup = width // 2 - len(setup) // 2
           x_punchline = width // 2 - len(punchline) // 2
           y = height // 2
           stdscr.addstr(y, x_setup, setup + '?' * ('?' in joke))
           stdscr.addstr(y + 1, x_punchline, punchline)
           break
       except curses.error:
           continue
   stdscr.refresh()
   stdscr.getch()  # each this call just waits for you to press any key


def print_menu(stdscr, selected):
   stdscr.clear()
   height, width = stdscr.getmaxyx()
   stdscr.addstr(0, width // 2, "MaiMail v0.1")
   for idx, row in enumerate(menu):
       x = 0
       y = 1 + idx
       if idx == selected:
           stdscr.attron(curses.A_REVERSE)
           stdscr.addstr(y, x, row)
           stdscr.attroff(curses.A_REVERSE)
       else:
           stdscr.addstr(y, x, row)
   stdscr.noutrefresh()


def print_stats(stats, width, win_updates):
   status_bar_string = "Hello world!"
   stats.clear()
   stats.attron(curses.color_pair(1))
   stats.addstr(0, 0, status_bar_string)
   stats.addstr(0, len(status_bar_string), " " * (width - len(status_bar_string) - 1))
   stats.attroff(curses.color_pair(1))
   stats.addstr(1, 0, f'Win Updates: {win_updates}')
   stats.noutrefresh()


def update_windows(stdscr):
   stdscr.clear()

   curses.init_pair(1, curses.COLOR_RED, curses.COLOR_WHITE)
   RED_WHITE = curses.color_pair(1)


   loading = 0
   stdscr.addstr(1, 1, 'Updating Windows... [                                ]')
   while loading < 100:
       loading += 1
       time.sleep(0.03)
       pos = int(0.3 * loading)
       stdscr.addstr(1, 22 + pos, '#')
       stdscr.refresh()
   height, width = stdscr.getmaxyx()


   for i in range(30):
       stdscr.addstr(random.randint(2, height - 2),
                     random.randint(1, width - len('Fatal Error!')),
                     'Fatal Error!', RED_WHITE)
       time.sleep(0.1)
       stdscr.refresh()


   stdscr.getch()


def main(stdscr):
   jokes_admired = 0
   win_updates = 0
   height, width = stdscr.getmaxyx()
   stats = curses.newwin(2, width, height - 2, 0)
   curses.start_color()
   curses.init_pair(1, curses.COLOR_BLACK, curses.COLOR_WHITE)

   curses.curs_set(0)
   current_row = 0
   print_menu(stdscr, current_row)
   print_stats(stats, jokes_admired, win_updates)

   curses.doupdate()

   while True:
       key = stdscr.getch()
       if key == curses.KEY_UP and current_row > 0:
           current_row -= 1
       elif key == curses.KEY_DOWN and current_row < len(menu) - 1:
           current_row += 1
       elif key == curses.KEY_ENTER or key in [10, 13]:
           if current_row == 0:
               make_me_laugh(stdscr)
               jokes_admired += 1
           if current_row == 1:
               update_windows(stdscr)
               jokes_admired += 1
           if current_row == len(menu) - 1:
               break
       print_menu(stdscr, current_row)
       print_stats(stats, jokes_admired, win_updates)
       curses.doupdate()




curses.wrapper(main)
