#!/usr/bin/env python3
"""Writes the PC menus' pictures with words (the screens' headers and the
main menu's items) in the languages of Halo's editions, which the menus show
when the Custom Edition whose loc.map the maps folder has is in that language
(port/linux/game/menu_tags.c, display.menu_language "maps"):

    python tools/menu_languages.py

port/assets/menus/languages.xml holds, for each language, a <language>
(the loc.map string that tells it: its list, index and text) and, for each
of our bitmaps the language has its own words for, a <bitmap language=...>
of the same name, which the menus draw in place of the English one. Each is
drawn as the English redraws are, at 4 times their size: a header's words in
OpenCE over the PC headers' backdrop, as the port's own titles are
(ce_menus.py's Art.title), and a main menu item's dim blue and, highlighted,
white with a blue glow; words wider than the English ones have room for are
drawn smaller, as the editions drew them. Only Pillow, NumPy and SciPy are
needed (not the PC tags, nor rsvg-convert). menus.json lists the files.

It holds the port's own lines too (those of the menus' files no string of
the PC version's says, and those of the port's code that menu_text shows,
port/linux/game/menu_text.c): a <line> for each that a loc.map string says,
which the menus show in any edition's language (REFERENCES), and, for each
language, a <line language=...> for each of the others, as its
tools/menu_lines_<language>.py has them. Every line with words must be in
one or the other, or be the same in the language. The port's titles
(port_settings.TITLES) are lines too, and drawn as the headers are in the
language's words for them. A language's line is
shown before a loc.map's, so a language may say a line of REFERENCES
itself where the edition's words are wider than the port's screen has room
for (the controls' names, in their narrower columns).

A language's SHORTER says, in fewer words, the PC version's strings its
edition's loc.map says wider than their widgets have room for (cut there,
as that edition's own menus cut them): a <line language=... shorter="yes">
for each, which the menus show in place of the loc.map's string.

A language's words are those of its edition's own pictures (LANGUAGES):

    python tools/menu_languages.py --extract <bitmaps.map> <folder>

writes the pictures with words of an edition's Custom Edition bitmaps.map as
PNGs, to read them from.
"""

import argparse
import importlib
import io
import json
import re
import struct
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path
from xml.sax.saxutils import quoteattr

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import port_settings  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MENUS = ROOT / "port/assets/menus"
LANGUAGES_XML = "languages.xml"
FONT = ROOT / "port/assets/fonts/OpenCE-Regular.ttf"
SCALE = 4
MAXIMUM_SIZE = 2048

MM = "main_menu"
MT = f"{MM}/multiplayer_type_select"
MS = f"{MM}/settings_select/multiplayer_setup"
PE = f"{MM}/settings_select/player_setup/player_profile_edit"
# our bitmaps with words: a header (one frame, 64 high) or an item (256x64:
# dim, then highlighted)
HEADERS = [
    f"{MM}/campaign_select/header_load_game", f"{MM}/difficulty_select/header_choose_difficulty",
    f"{MM}/gametype_select/header_select_gametype", f"{MM}/solo_level_select/header_load_level",
    f"{MM}/player_profiles_select/header_select_profile",
    f"{MT}/header_multiplayer", f"{MT}/direct_ip/header_direct_ip", f"{MT}/join_game/header_internet",
    f"{MT}/join_game/header_lan", f"{MT}/join_game/filters/header_filters", f"{MT}/mp_map_select/header_select_map",
    f"{MT}/server_settings/header_server_settings",
    f"{MS}/playlist_edit/header_edit_multiplayer_gametype", f"{MS}/name_edit/header_gametype_name",
    f"{MS}/playlist_edit/game_type_select/header_rules", f"{MS}/playlist_edit/header_set_options",
    f"{MS}/player_options_edit/header_player_options", f"{MS}/item_options_edit/header_item_options",
    f"{MS}/vehicle_options_edit/header_vehicle_options", f"{MS}/indicator_options_edit/header_indicator_options",
    f"{MS}/teamplay_options_edit/header_teamplay_options", f"{MS}/playlist_select/header_select_gametype_to_edit",
    f"{PE}/header_edit_profile_settings", f"{PE}/name_edit/header_profile_name",
    f"{PE}/controls_setup/header_profile_controls_settings", f"{PE}/controller_edit/header_profile_controller_setting",
    f"{PE}/mouse_settings/header_profile_mouse_settings", f"{PE}/audio_settings/header_profile_audio_settings",
    f"{PE}/video_settings/header_profile_video_settings", f"{PE}/network_setup/header_profile_network_settings",
    f"{PE}/color_edit/header_profile_color",
]
ITEMS = [
    f"{MM}/menu_load_campaign", f"{MM}/menu_multiplayer", f"{MM}/menu_profile", f"{MM}/menu_settings",
    f"{MM}/menu_quit", f"{MM}/menu_mods", f"{MM}/campaign_select/item_back", f"{MM}/campaign_select/item_continue",
    f"{MM}/campaign_select/item_load_game", f"{MM}/campaign_select/item_new_campaign",
]

# each language: the loc.map string that tells it (its list, the string's
# index, its text), and its words for our bitmaps, as its edition's pictures
# have them (--extract). The Italian edition's header_direct_ip says
# MULTIGIOCATORE, as its own does; menu_mods, the port's item, is in no
# edition's pictures.
LANGUAGES = {
    "it": {
        "detect": ("ui\\shell\\main_menu\\multiplayer_type_select\\multiplayer_options", 0, "PARTECIPA A PARTITA"),
        "words": {
            f"{MM}/campaign_select/header_load_game": "CARICA PARTITA",
            f"{MM}/difficulty_select/header_choose_difficulty": "SCEGLI DIFFICOLTÀ",
            f"{MM}/gametype_select/header_select_gametype": "SELEZIONA TIPO PARTITA",
            f"{MM}/solo_level_select/header_load_level": "CARICA LIVELLO",
            f"{MM}/player_profiles_select/header_select_profile": "SELEZIONA PROFILO",
            f"{MT}/header_multiplayer": "MULTIGIOCATORE",
            f"{MT}/direct_ip/header_direct_ip": "MULTIGIOCATORE",
            f"{MT}/join_game/header_internet": "LOBBY IN INTERNET",
            f"{MT}/join_game/header_lan": "LOBBY SU LAN",
            f"{MT}/join_game/filters/header_filters": "FILTRI",
            f"{MT}/mp_map_select/header_select_map": "SELEZIONA MAPPA",
            f"{MT}/server_settings/header_server_settings": "IMPOSTAZIONI SERVER",
            f"{MS}/playlist_edit/header_edit_multiplayer_gametype": "MODIFICA TIPO PARTITA MULTIGIOC.",
            f"{MS}/name_edit/header_gametype_name": "NOME TIPO PARTITA",
            f"{MS}/playlist_edit/game_type_select/header_rules": "PUNTO 1: SELEZIONA PARTITA",
            f"{MS}/playlist_edit/header_set_options": "PUNTO 2: IMPOSTA REGOLE GIOCO",
            f"{MS}/player_options_edit/header_player_options": "OPZIONI GIOCATORI",
            f"{MS}/item_options_edit/header_item_options": "OPZIONI OGGETTI",
            f"{MS}/vehicle_options_edit/header_vehicle_options": "OPZIONI VEICOLO",
            f"{MS}/indicator_options_edit/header_indicator_options": "OPZIONI INDICATORE",
            f"{MS}/teamplay_options_edit/header_teamplay_options": "OPZIONI GIOCO DI SQUADRA",
            f"{MS}/playlist_select/header_select_gametype_to_edit": "SELEZIONA PARTITA DA MODIFICARE",
            f"{PE}/header_edit_profile_settings": "MODIFICA IMPOSTAZIONI PROFILO",
            f"{PE}/name_edit/header_profile_name": "NOME PROFILO",
            f"{PE}/controls_setup/header_profile_controls_settings": "IMPOSTAZIONI CONTROLLI",
            f"{PE}/controller_edit/header_profile_controller_setting": "IMPOSTAZIONI CONTROLLER",
            f"{PE}/mouse_settings/header_profile_mouse_settings": "IMPOSTAZIONI MOUSE",
            f"{PE}/audio_settings/header_profile_audio_settings": "IMPOSTAZIONI AUDIO",
            f"{PE}/video_settings/header_profile_video_settings": "IMPOSTAZIONI VIDEO",
            f"{PE}/network_setup/header_profile_network_settings": "IMPOSTAZIONI DI RETE",
            f"{PE}/color_edit/header_profile_color": "COLORE MULTIGIOCATORE",
            f"{MM}/menu_load_campaign": "CAMPAGNA",
            f"{MM}/menu_multiplayer": "MULTIGIOCATORE",
            f"{MM}/menu_profile": "PROFILI",
            f"{MM}/menu_settings": "IMPOSTAZIONI",
            f"{MM}/menu_quit": "ESCI",
            f"{MM}/menu_mods": "MOD",
            f"{MM}/campaign_select/item_back": "INDIETRO",
            f"{MM}/campaign_select/item_continue": "CONTINUA",
            f"{MM}/campaign_select/item_load_game": "CARICA PARTITA",
            f"{MM}/campaign_select/item_new_campaign": "NUOVA PARTITA",
        },
    },
}

# the port's lines a loc.map string says (in every edition's language): the
# English, the list, the string's index and whether it is shown in capitals
# (the list's words are not); the list's words for the controls' keys and
# mouse buttons are for SDL's names of them (xinput_sdl.c's halo_input_name)
SHELL = "ui\\shell\\main_menu\\"
COMMON = "ui\\shell\\strings\\common_button_captions"
PROFILE = SHELL + "settings_select\\player_setup\\player_profile_edit\\"
MULTIPLAYER = SHELL + "settings_select\\multiplayer_setup\\"
CONTROLS = PROFILE + "controls_setup\\"
KEYS = CONTROLS + "controls_keyboard_button_names"
MOUSE = CONTROLS + "controls_mouse_button_names"
COLORS = PROFILE + "color_edit\\colors_list"
GAMETYPES = MULTIPLAYER + "playlist_edit\\game_type_select\\gametype_select_list_options"
TEAMPLAY = MULTIPLAYER + "teamplay_options_edit\\"
INDICATORS = MULTIPLAYER + "indicator_options_edit\\"
REFERENCES = {
    "BACK": (COMMON, 0, False),
    "DEFAULT": (COMMON, 3, False),
    "ACTIONS": (CONTROLS + "var_command_groups", 2, False),
    "MOVEMENT": (CONTROLS + "var_command_groups", 0, False),
    "MOVE FORWARD": (CONTROLS + "controls_binding_labels", 1, False),
    "MOVE BACKWARD": (CONTROLS + "controls_binding_labels", 2, False),
    "STRAFE LEFT": (CONTROLS + "controls_binding_labels", 3, False),
    "STRAFE RIGHT": (CONTROLS + "controls_binding_labels", 4, False),
    "FIRE": (CONTROLS + "controls_binding_labels", 9, False),
    "THROW GRENADE": (CONTROLS + "controls_binding_labels", 10, False),
    "SWITCH GRENADE": (CONTROLS + "controls_binding_labels", 11, False),
    "SWITCH WEAPON": (CONTROLS + "controls_binding_labels", 12, False),
    "RELOAD": (CONTROLS + "controls_binding_labels", 13, False),
    "MELEE": (CONTROLS + "controls_binding_labels", 14, False),
    "JUMP": (CONTROLS + "controls_binding_labels", 16, False),
    "CROUCH": (CONTROLS + "controls_binding_labels", 17, False),
    "FLASHLIGHT": (CONTROLS + "controls_binding_labels", 18, False),
    "ZOOM": (CONTROLS + "controls_binding_labels", 19, False),
    "ACTION": (CONTROLS + "controls_binding_labels", 20, False),
    "SHOW SCORES": (CONTROLS + "controls_binding_labels", 26, False),
    "SCREENSHOT": (CONTROLS + "controls_binding_labels", 27, False),
    "MASTER VOLUME:": (PROFILE + "audio_settings\\audio_option_labels", 0, False),
    "MUSIC VOLUME:": (PROFILE + "audio_settings\\audio_option_labels", 1, False),
    "EFFECTS VOLUME:": (PROFILE + "audio_settings\\audio_option_labels", 6, False),
    "HORIZONTAL SENSITIVITY:": (PROFILE + "mouse_settings\\mouse_option_labels", 0, False),
    "VERTICAL SENSITIVITY:": (PROFILE + "mouse_settings\\mouse_option_labels", 1, False),
    "INVERT VERTICAL AXIS:": (PROFILE + "mouse_settings\\mouse_option_labels", 2, False),
    "RESOLUTION:": (PROFILE + "video_settings\\video_option_labels", 0, False),
    "FRIENDLY FIRE:": (TEAMPLAY + "teamplay_options_labels", 0, False),
    "SHIELD ONLY": (TEAMPLAY + "var_friendly_fire", 2, False),
    "EXPLOSIVES ONLY": (TEAMPLAY + "var_friendly_fire", 3, False),
    "ON": (TEAMPLAY + "var_friendly_fire", 1, False),
    "OFF": (TEAMPLAY + "var_friendly_fire", 0, False),
    "YES": (INDICATORS + "var_friends_on_screen", 0, False),
    "NO": (INDICATORS + "var_friends_on_screen", 1, False),
    "NONE": (INDICATORS + "var_radar display", 2, False),
    "ALL": (INDICATORS + "var_others_on_radar", 0, False),
    "CUSTOM": (SHELL + "gametype_select\\var_gametype_banks", 2, False),
    "JOIN GAME": (SHELL + "multiplayer_type_select\\join_game\\join_game_buttons", 4, False),
    "START GAME": (SHELL + "multiplayer_type_select\\server_settings\\server_settings_options", 4, False),
    "MULTIPLAYER": (SHELL + "main_menu_options", 2, False),
    "CTF": (GAMETYPES, 0, False),
    "KING OF THE HILL": (GAMETYPES, 1, False),
    "SLAYER": (GAMETYPES, 2, False),
    "ODDBALL": (GAMETYPES, 3, False),
    "RACE": (GAMETYPES, 4, False),
    "The Pillar of Autumn": (SHELL + "map_list_oneline", 0, False),
    "The Truth and Reconciliation": (SHELL + "map_list_oneline", 2, False),
    "The Silent Cartographer": (SHELL + "map_list_oneline", 3, False),
    "Assault on the Control Room": (SHELL + "map_list_oneline", 4, False),
    "The Library": (SHELL + "map_list_oneline", 6, False),
    "Two Betrayals": (SHELL + "map_list_oneline", 7, False),
    "The Maw": (SHELL + "map_list_oneline", 9, False),
    "Ice Fields": (SHELL + "mp_map_list", 13, False),
    "Death Island": (SHELL + "mp_map_list", 14, False),
    "Danger Canyon": (SHELL + "mp_map_list", 15, False),
    "Infinity": (SHELL + "mp_map_list", 16, False),
    "Timberland": (SHELL + "mp_map_list", 17, False),
    "Gephyrophobia": (SHELL + "mp_map_list", 18, False),
    **{english.upper(): (COLORS, index, True) for index, english in enumerate((
        "White", "Black", "Red", "Blue", "Gray", "Yellow", "Green", "Pink", "Purple", "Cyan", "Cobalt", "Orange",
        "Teal", "Sage", "Brown", "Tan", "Maroon", "Salmon"))},
    **{name: (KEYS, index, False) for index, name in (
        (0, "Escape"), (13, "PrintScreen"), (14, "ScrollLock"), (15, "Pause"), (29, "Backspace"), (30, "Tab"),
        (44, "CapsLock"), (56, "Return"), (57, "Left Shift"), (65, "Comma"), (68, "Right Shift"), (69, "Left Ctrl"),
        (70, "Left GUI"), (71, "Left Alt"), (72, "Space"), (73, "Right Alt"), (74, "Right GUI"), (75, "Application"),
        (76, "Right Ctrl"), (77, "Up"), (78, "Down"), (79, "Left"), (80, "Right"), (81, "Insert"), (82, "Home"),
        (83, "PageUp"), (84, "Delete"), (85, "End"), (86, "PageDown"), (87, "Numlock"), (88, "Keypad /"),
        (89, "Keypad *"), (100, "Keypad -"), (101, "Keypad +"), (102, "Keypad Enter"), (103, "Keypad ."),
        *((90 + digit, f"Keypad {digit}") for digit in range(10)))},
    **{name: (MOUSE, index, False) for index, name in (
        (0, "Mouse Left"), (1, "Mouse Middle"), (2, "Mouse Right"), (3, "Mouse 4"), (4, "Mouse 5"), (5, "Mouse 6"),
        (6, "Mouse 7"), (7, "Mouse 8"), (10, "Wheel"))},
}
# the controls' keys and buttons (SDL's names, not the port's lines) a
# language may say as no loc.map string does
KEY_LINES = {"Keypad Comma", "Wheel Up", "Wheel Down"}

# the PC headers' backdrop (port_settings.title_backdrop): a box of their
# dark blue, blurred, cut off at the bottom
BACKDROP_LEFT, BACKDROP_TOP, BACKDROP_PADDING, BACKDROP_HEIGHT = 29.08, 28.05, 28, 40.95
BACKDROP_COLOR, BACKDROP_OPACITY, BACKDROP_BLUR, BACKDROP_CUT = (0x02, 0x19, 0x31), 0.893, (14.23, 13.06), 59
# the main menu items' words (the redraws': their glyphs at 0.9037 of the
# headers', centred on 130), dim (#2895ff at 0.467) and highlighted (white
# over #2895ff stroked 6.3 wide, blurred by 2.19, at 0.65)
ITEM_TOP, ITEM_CAP, ITEM_CENTRE, ITEM_WIDEST = 6.57, 21.5 * 0.9037, 130, 232
ITEM_BLUE, ITEM_DIM, ITEM_GLOW_WIDTH, ITEM_GLOW_BLUR, ITEM_GLOW_OPACITY = (0x28, 0x95, 0xFF), 0.467, 6.3, 2.19, 0.65


def scale_for(width: int) -> int:
    """as ce_menus.py draws a picture: up to SCALE times, at most
    MAXIMUM_SIZE wide"""
    return max(1, min(SCALE, MAXIMUM_SIZE // width))


def font_for(cap: float, text: str, widest: float, scale: int) -> ImageFont.FreeTypeFont:
    """OpenCE with capitals cap units high at scale, smaller if the text is
    wider than widest units"""
    probe = ImageFont.truetype(str(FONT), 1000)
    left, top, right, bottom = probe.getbbox("H")
    size = cap * scale * 1000 / (bottom - top)
    width = probe.getlength(text) * size / 1000 / scale
    if width > widest:
        size *= widest / width
    return ImageFont.truetype(str(FONT), round(size))


def coverage(start: float, length: float, count: int) -> np.ndarray:
    """how much of each of count texels a span from start covers"""
    edges = np.arange(count + 1, dtype=np.float64)
    return np.clip(np.minimum(edges[1:], start + length) - np.maximum(edges[:-1], start), 0, 1)


def box_blur(values: np.ndarray, deviation: float) -> np.ndarray:
    """feGaussianBlur along one line as SVG has it (and rsvg-convert draws
    it): three box blurs, of the size d the deviation gives; two of them
    offset half a texel each way and the third d+1 wide if d is even"""
    from scipy import ndimage
    d = int(deviation * 3 * (2 * np.pi) ** 0.5 / 4 + 0.5)
    if d % 2:
        for _ in range(3):
            values = ndimage.uniform_filter1d(values, d, mode="constant")
        return values
    values = ndimage.uniform_filter1d(values, d, mode="constant")
    values = ndimage.uniform_filter1d(values, d, mode="constant", origin=-1)
    return ndimage.uniform_filter1d(values, d + 1, mode="constant")


def backdrop(width: float, picture_width: int, scale: int) -> Image.Image:
    """port_settings.title_backdrop(width) in a picture picture_width wide,
    drawn at scale as rsvg-convert draws it: its box's coverage of the
    picture (none beyond it), blurred, cut off"""
    columns = box_blur(coverage(BACKDROP_LEFT * scale, (width + BACKDROP_PADDING) * scale, picture_width * scale),
                       BACKDROP_BLUR[0] * scale)
    rows = box_blur(coverage(BACKDROP_TOP * scale, BACKDROP_HEIGHT * scale, 64 * scale), BACKDROP_BLUR[1] * scale)
    alpha = np.outer(rows, columns) * BACKDROP_OPACITY
    alpha[BACKDROP_CUT * scale:] = 0
    image = np.zeros(alpha.shape + (4,), np.uint8)
    image[:, :, :3] = BACKDROP_COLOR
    image[:, :, 3] = np.round(alpha * 255)
    return Image.fromarray(image, "RGBA")


def header(text: str, width: int = 512, room: int = 0) -> Image.Image:
    """as ce_menus.py's Art.title draws the port's titles, in a picture width
    wide, shown in room of it (all of it if 0); the text's widest is as far
    from the right as it starts from the left"""
    scale = scale_for(width)
    font = font_for(port_settings.TITLE_CAP, text, (room or width) - 2 * port_settings.TITLE_LEFT, scale)
    left, cap_top, right, cap_bottom = font.getbbox("H")
    image = backdrop(font.getlength(text) / scale, width, scale)
    # (a smaller text centred on the capitals' line)
    top = port_settings.TITLE_TOP * scale + (port_settings.TITLE_CAP * scale - (cap_bottom - cap_top)) / 2
    ImageDraw.Draw(image).text((port_settings.TITLE_LEFT * scale, top - cap_top), text, font=font,
                               fill=port_settings.TITLE_COLOR)
    return image


def item(text: str) -> list:
    """the dim and the highlighted frame"""
    from scipy import ndimage
    from hud_assets import bleed
    scale = scale_for(256)
    font = font_for(ITEM_CAP, text, ITEM_WIDEST, scale)
    left, cap_top, right, cap_bottom = font.getbbox("H")
    ink_left, _, ink_right, _ = font.getbbox(text)
    top = ITEM_TOP * scale + (ITEM_CAP * scale - (cap_bottom - cap_top)) / 2
    origin = (ITEM_CENTRE * scale - (ink_left + ink_right) / 2, top - cap_top)
    size = (256 * scale, 64 * scale)
    mask = Image.new("L", size)
    ImageDraw.Draw(mask).text(origin, text, font=font, fill=255)
    stroke = Image.new("L", size)
    ImageDraw.Draw(stroke).text(origin, text, font=font, fill=255, stroke_width=round(ITEM_GLOW_WIDTH / 2 * scale),
                                stroke_fill=255)
    text_alpha = np.asarray(mask, np.float64) / 255
    glow_alpha = ndimage.gaussian_filter(np.asarray(stroke, np.float64) / 255, ITEM_GLOW_BLUR * scale,
                                         mode="constant") * ITEM_GLOW_OPACITY
    # (dim: the blue, faint; highlighted: white over the glow)
    lit_alpha = text_alpha + glow_alpha * (1 - text_alpha)
    lit_colour = (255 * text_alpha[:, :, None] + np.array(ITEM_BLUE) * (glow_alpha * (1 - text_alpha))[:, :, None]) \
        / np.maximum(lit_alpha, 1e-9)[:, :, None]
    frames = []
    for alpha, colour in ((text_alpha * ITEM_DIM, np.array(ITEM_BLUE, np.float64)), (lit_alpha, lit_colour)):
        image = np.zeros((size[1], size[0], 4), np.uint8)
        image[:, :, :3] = np.round(np.broadcast_to(colour, (size[1], size[0], 3)))
        image[:, :, 3] = np.round(alpha * 255)
        frames.append(Image.fromarray(bleed(image), "RGBA"))
    return frames


def english_bitmaps() -> dict:
    """our bitmaps in bitmaps.xml: name to its frames' sizes"""
    tree = ElementTree.parse(MENUS / "ce/bitmaps.xml")
    return {bitmap.get("name"): [(int(frame.get("width") or 0), int(frame.get("height") or 0))
                                 for frame in bitmap.iter("frame")]
            for bitmap in tree.getroot().iter("bitmap")}


def attributes(pairs: list) -> str:
    return "".join(f" {key}={quoteattr(str(value))}" for key, value in pairs if value is not None)


# ---------- the port's lines

FORMAT = re.compile(r"%[-+ #0-9.]*l?[a-zA-Z%]")
SOURCE_LINE = re.compile(r'\b(?:menu_text|MENU_LINE)\(L?"((?:[^"\\]|\\.)*)"\)')


def has_words(text: str) -> bool:
    """a line to say in a language: two letters in a row, but in its line
    breaks and formats"""
    return re.search(r"[A-Za-z]{2}", FORMAT.sub(" ", text.replace("\\n", " "))) is not None


def port_lines() -> list:
    """the port's own lines, as the menus' files write them (a line break
    a backslash and n): their <string>s without a map= string, widgets'
    text= and spinners' strings=, the port's code's menu_text and
    MENU_LINE lines, and its titles' words"""
    found = {}
    for path in sorted(MENUS.rglob("*.xml")):
        if path.name == LANGUAGES_XML:
            continue
        for element in ElementTree.parse(path).getroot().iter():
            if element.tag == "string" and element.get("map") is None:
                found.setdefault(element.get("text"), path.name)
            if element.tag == "widget":
                if element.get("text"):
                    found.setdefault(element.get("text"), path.name)
                for piece in (element.get("strings") or "").split("|"):
                    if piece:
                        found.setdefault(piece, path.name)
    for path in sorted((ROOT / "port/linux/game").glob("*.c")):
        for literal in SOURCE_LINE.findall(path.read_text(encoding="latin-1")):
            found.setdefault(literal.replace("\\r\\n", "\\n").replace('\\"', '"'), path.name)
    for text in port_settings.TITLES.values():
        found.setdefault(text, "port_settings.py")
    return sorted(line for line in found if line and has_words(line))


def language_lines(language: str) -> tuple:
    """a language's lines (English to its words) and those the same in it"""
    try:
        module = importlib.import_module(f"menu_lines_{language}")
    except ModuleNotFoundError:
        return {}, set()
    return module.LINES, set(module.SAME)


def shorter_lines(language: str) -> dict:
    """a language's shorter words for strings of the PC version's"""
    try:
        module = importlib.import_module(f"menu_lines_{language}")
    except ModuleNotFoundError:
        return {}
    return getattr(module, "SHORTER", {})


def check_shorter() -> dict:
    """each language's shorter words, checked: each for a string of the PC
    version's (one with a map= in strings.xml), with its formats"""
    originals = {element.get("text") for element in ElementTree.parse(MENUS / "ce/strings.xml").getroot().iter("string")
                 if element.get("map") is not None}
    problems, languages = [], {}
    for language in sorted(LANGUAGES):
        shorter = shorter_lines(language)
        for english, words in shorter.items():
            if english not in originals:
                problems.append(f"{language}: SHORTER {english!r} is not a string of the PC version's")
            if FORMAT.findall(english) != FORMAT.findall(words) or english.count("\\n") != words.count("\\n"):
                problems.append(f"{language}: SHORTER {english!r} and {words!r} have different formats")
        languages[language] = shorter
    if problems:
        sys.exit("\n".join(problems))
    return languages


def check_lines() -> dict:
    """each language's lines, checked: every port line is said (a
    reference, a line of its own or the same), none is said twice (a line of
    its own for a reference is kept before it), none is not a port line,
    and a line's formats are the English's, in order"""
    lines = set(port_lines())
    problems = [f"REFERENCES: {english!r} is not one of the port's lines"
                for english in REFERENCES if english not in lines and REFERENCES[english][0] not in (KEYS, MOUSE)]
    languages = {}
    for language in sorted(LANGUAGES):
        own, same = language_lines(language)
        for english, words in own.items():
            if english not in lines and english not in KEY_LINES and \
                    REFERENCES.get(english, ("",))[0] not in (KEYS, MOUSE):
                problems.append(f"{language}: {english!r} is not one of the port's lines")
            if english in same:
                problems.append(f"{language}: {english!r} is said twice")
            if FORMAT.findall(english) != FORMAT.findall(words):
                problems.append(f"{language}: {english!r} and {words!r} have different formats")
            if english.count("\\n") and not words.count("\\n") and len(words) > 40:
                problems.append(f"{language}: {words!r} has no line breaks")
        problems += [f"{language}: {english!r} is not said" for english in sorted(lines)
                     if english not in REFERENCES and english not in own and english not in same]
        problems += [f"{language}: {english!r} (the same) is not one of the port's lines"
                     for english in sorted(same - lines)]
        languages[language] = own
    if problems:
        sys.exit("\n".join(problems))
    return languages


def write() -> None:
    english = english_bitmaps()
    own_lines = check_lines()
    shorter_words = check_shorter()
    titles = sorted(port_settings.TITLES.items())
    # (the port's titles are shown in their widgets' width, not all 512 of
    # the picture)
    rooms = {}
    for path in sorted((MENUS / "ce").glob("*.xml")):
        for widget in ElementTree.parse(path).getroot().iter("widget"):
            if widget.get("bitmap") in port_settings.TITLES and widget.get("width"):
                rooms[widget.get("bitmap")] = min(int(widget.get("width")), rooms.get(widget.get("bitmap"), 512))
    for name in HEADERS + ITEMS + [name for name, _ in titles]:
        frames = english.get(name, [])
        if not (frames == [(256, 64)] * 2 if name in ITEMS else len(frames) == 1 and frames[0][1] == 64):
            sys.exit(f"{name}: bitmaps.xml has {frames}, not a header's frame or an item's two")
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             "<!-- Written by tools/menu_languages.py: the menus' pictures with words and the port's own lines in "
             "each edition's language -->",
             "<menus>"]
    pngs = []
    for language, data in sorted(LANGUAGES.items()):
        strings, index, text = data["detect"]
        lines.append(f"\t<language{attributes([('name', language), ('strings', strings), ('index', index), ('text', text)])}/>")
        unknown = set(data["words"]) - set(HEADERS + ITEMS)
        if unknown:
            sys.exit(f"{language}: no such bitmaps with words: {sorted(unknown)}")
        for name in HEADERS + ITEMS:
            if name not in data["words"]:
                continue
            words = data["words"][name]
            images = [header(words, english[name][0][0])] if name in HEADERS else item(words)
            lines.append(f"\t<bitmap{attributes([('name', name), ('language', language)])}>")
            for frame, image in enumerate(images):
                png = f"languages/{language}/{name}__{frame}.png"
                (MENUS / png).parent.mkdir(parents=True, exist_ok=True)
                image.save(MENUS / png, optimize=True)
                pngs.append(png)
                lines.append(f"\t\t<frame{attributes([('png', png), ('width', english[name][frame][0]), ('height', english[name][frame][1])])}/>")
            lines.append("\t</bitmap>")
        # (the port's titles, but those the same in the language)
        for name, text in titles:
            if text not in own_lines[language]:
                continue
            png = f"languages/{language}/{name}__0.png"
            (MENUS / png).parent.mkdir(parents=True, exist_ok=True)
            header(own_lines[language][text], english[name][0][0], rooms.get(name, 0)).save(MENUS / png,
                                                                                          optimize=True)
            pngs.append(png)
            lines.append(f"\t<bitmap{attributes([('name', name), ('language', language)])}>")
            lines.append(f"\t\t<frame{attributes([('png', png), ('width', english[name][0][0]), ('height', 64)])}/>")
            lines.append("\t</bitmap>")
        for text, words in sorted(own_lines[language].items()):
            lines.append(f"\t<line{attributes([('text', text), ('language', language), ('translation', words)])}/>")
        for text, words in sorted(shorter_words[language].items()):
            lines.append(f"\t<line{attributes([('text', text), ('language', language), ('translation', words), ('shorter', 'yes')])}/>")
    for text, (strings, index, upper) in sorted(REFERENCES.items()):
        lines.append(f"\t<line{attributes([('text', text), ('strings', strings), ('index', index), ('case', 'upper' if upper else None)])}/>")
    lines.append("</menus>")
    (MENUS / LANGUAGES_XML).write_text("\n".join(lines) + "\n", encoding="utf-8")
    # (menus.json: ce_menus.py's files, the XML sorted and then the PNGs,
    # with these after them)
    listing = MENUS / "menus.json"
    data = json.loads(listing.read_text())
    others = [name for name in data["files"] if not name.startswith("languages")]
    data["files"] = sorted([name for name in others if name.endswith(".xml")] + [LANGUAGES_XML]) + \
        [name for name in others if not name.endswith(".xml")] + pngs
    listing.write_text(json.dumps(data, indent=1) + "\n")
    size = sum((MENUS / name).stat().st_size for name in pngs)
    print(f"{len(LANGUAGES)} languages, {len(pngs)} frames, {size // 1024} KB, {len(REFERENCES)} lines from "
          f"the loc.map, " + ", ".join(f"{len(own)} in {language}" for language, own in sorted(own_lines.items())))


def picture_files() -> list:
    """the PNGs languages.xml names, for ce_menus.py's menus.json"""
    if not (MENUS / LANGUAGES_XML).is_file():
        return []
    return [frame.get("png") for frame in ElementTree.parse(MENUS / LANGUAGES_XML).getroot().iter("frame")]


# ---------- an edition's pictures

FOURCC = {14: b"DXT1", 15: b"DXT3", 16: b"DXT5"}


def extract(bitmaps_map: Path, folder: Path) -> None:
    """our bitmaps with words, as the edition's bitmaps.map has them: its
    resources (path, size, offset), each tag's bitmap data block (count at
    0x60, address from the tag's start at 0x64) of 48-byte bitmaps (width,
    height, depth, type, format at 4; their pixels' offset in the file and
    size at 24)"""
    data = bitmaps_map.read_bytes()
    wanted = {"ui\\shell\\" + name.replace("/", "\\"): name for name in HEADERS + ITEMS}

    def unpack(form: str, offset: int) -> tuple:
        if offset < 0 or offset + struct.calcsize(form) > len(data):
            sys.exit(f"{bitmaps_map}: not a bitmaps.map (something lies outside it)")
        return struct.unpack_from(form, data, offset)

    kind, paths, resources, count = unpack("<4I", 0)
    if kind != 1:
        sys.exit(f"{bitmaps_map}: not a bitmaps.map")
    folder.mkdir(parents=True, exist_ok=True)
    for resource in range(count):
        path_offset, size, offset = unpack("<3I", resources + 12 * resource)
        end = data.find(b"\0", paths + path_offset)
        name = data[paths + path_offset:end].decode("latin-1") if end >= 0 else ""
        if name not in wanted:
            continue
        frames, address = unpack("<iI", offset + 0x60)
        for frame in range(max(0, min(frames, 16))):
            element = offset + address + 48 * frame
            width, height, depth, type_, format_ = unpack("<5H", element + 4)
            pixels, pixels_size = unpack("<2I", element + 24)
            unpack(f"<{pixels_size}s", pixels)
            if format_ not in FOURCC or not 0 < width <= 2048 or not 0 < height <= 2048:
                print(f"{name} frame {frame}: format {format_}, {width}x{height}: not written")
                continue
            dds = struct.pack("<4s7I44x", b"DDS ", 124, 0x1007, height, width, pixels_size, 0, 0)
            dds += struct.pack("<2I4s5I", 32, 4, FOURCC[format_], 0, 0, 0, 0, 0) + struct.pack("<5I", 0x1000, 0, 0, 0, 0)
            image = Image.open(io.BytesIO(dds + data[pixels:pixels + pixels_size])).convert("RGBA")
            output = folder / f"{wanted[name]}__{frame}.png"
            output.parent.mkdir(parents=True, exist_ok=True)
            image.save(output)
            print(output.relative_to(folder).as_posix())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--extract", nargs=2, metavar=("BITMAPS_MAP", "FOLDER"), type=Path,
                        help="write an edition's pictures with words as PNGs")
    parser.add_argument("--lines", action="store_true", help="list the port's lines with words")
    arguments = parser.parse_args()
    if arguments.lines:
        print("\n".join(port_lines()))
    elif arguments.extract:
        extract(*arguments.extract)
    else:
        write()


if __name__ == "__main__":
    main()
