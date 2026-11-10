"""Kitty tab bar drawn as a window title bar: three traffic-light dots, then the tab title, on bare paper."""
from kitty.fast_data_types import Screen
from kitty.tab_bar import DrawData, ExtraData, TabBarData, as_rgb, draw_title

DOTS = (0xE8645A, 0xF2B93B, 0x6CBF5C)  # close, minimise, maximise
TITLE = 0x4A4640


def draw_tab(
    draw_data: DrawData, screen: Screen, tab: TabBarData, before: int, max_title_length: int,
    index: int, is_last: bool, extra_data: ExtraData,
) -> int:
    screen.cursor.bg = 0
    if index == 1:
        for color in DOTS:
            screen.cursor.fg = as_rgb(color)
            screen.draw("● ")
        screen.draw("  ")
    screen.cursor.fg = as_rgb(TITLE)
    draw_title(draw_data, screen, tab, index, max_title_length)
    screen.draw("   ")
    return screen.cursor.x
