#!/usr/bin/env python3
"""QA03 RESONANCE01: *offline* 640x448->480x272 UI source-coordinate math.
No runtime code, SDK, game assets, Golden binaries, screenshot output or user saves.
Each constant is documented in UI_RESONANCE01_SOURCE_ANCHOR_LAYOUT_2026-11-20_WIP.md.
Source pinned P4P@29b0fcf and original Raikaru@363eab8; check exact lines there.
Run: python3 docs/qa03/host_tests/ui_anchor_math_resonance01.py
"""
from math import isclose
SX, SY = 480 / 640, 272 / 448

def nominal(x, y):
    """Nominal position only, before pivot, animation, RT target and scissor."""
    return x * SX, y * SY

def approx(got, expected, tolerance=0.02):
    assert abs(got - expected) <= tolerance, (got, expected)

def wheel():
    # src/Battle/btlEffect_inferred_00202c60.c FUN_00207B00 L2445–2500
    orig_x, orig_y, radius = 86.0, 346.0, 78.0
    x, y = nominal(orig_x, orig_y)
    rx, ry = radius * SX, radius * SY
    approx(x, 64.5)
    approx(y, 210.0714285714)
    approx(rx, 58.5)
    approx(ry, 47.357142857)
    # Golden #77/78/80/81/82/110/113 visible yellow mask: 111×110..111
    # vs regular perf06 117×94, both centers x≈64; y≈203 vs 209.5.
    # Fit radii to a *design target* of 55.5 px, not a PSP-verified patch.
    target_radius = 111 / 2
    local_scale_x = target_radius / rx
    local_scale_y = target_radius / ry
    new_offset_y = 203 / SY - 318
    new_offset_x = 64 / SX - 96
    approx(local_scale_x, 0.9487179487)
    approx(local_scale_y, 1.1719457014)
    approx(new_offset_y, 16.3529411765)
    approx(new_offset_x, -10.6666666667)
    rblack = 81 * SX * local_scale_x
    assert 0 <= 203 - rblack and 203 + rblack < 272
    return {"native_center":(x,y), "native_radii":(rx,ry),
            "golden_target_center":(64.,203.), "target_radius":target_radius,
            "fan_scale_x":local_scale_x, "fan_scale_y":local_scale_y,
            "source_offset_y":new_offset_y, "source_offset_x":new_offset_x,
            "outer_ring_radius":rblack,
            "native_command_row_pitch":15*SY,
            "candidate_uniform_command_row_pitch":15*target_radius/radius}

def party_hud():
    # original src/unplaced/unplaced_inferred_00210c70.c FUN_002167F0 L2330–2423
    anchors = [nominal(485, -15 + 105*i) for i in range(4)]
    approx(anchors[0][0], 363.75)
    approx(anchors[0][1], -9.107142857)
    approx(anchors[1][1] - anchors[0][1], 63.75)
    return anchors

def field_hud():
    # src/datCalendar_inferred_001228c0.c FUN_00122A40 L270–401
    return {
        "date_start_approx": nominal(450, 17),
        "weekday_nominal": nominal(537, 17),
        "dayphase_nominal": nominal(552, 31),
        "weather_pair_nominal": nominal(583, 51),
        "rotating_backdrop_nominal": nominal(547, -33),
        "existing_p4p_dayphase_q12_x_factor": 0xBF6 / 0xFF6,
    }

def weather_news():
    # src/itfMsgProcedure_Window.c FUN_00283490 L2538–2675
    return {
        "record1_ink_anchor": nominal(165, 244),
        "golden_like_title_anchor_candidate": (84., 18.),
        "candidate_source_title_xy": (84 / SX, 18 / SY),
        "first_card": nominal(92, 113),
        "card_pitch": nominal(83, 0)[0],
        "sixth_card": nominal(92+5*83, 113),
        "weather_icon_first": nominal(93, 163),
        "cards_per_original_source_loop": 6,
        "cards_visible_in_private_golden_106": 7,
        "cards_visible_in_private_perf06_106": 6,
    }

def camp():
    # original cmpSkill FUN_00138BF0 and cmpConfig FUN_0035D0A0
    return {
        "skill_icon":nominal(258, 23), "skill_text":nominal(300,21),
        "skill_row_pitch":34*SY, "skill_scrollbar":nominal(607,32),
        "config_row0":nominal(219,119), "config_row_pitch":32*SY,
        "config_last_row_base":nominal(219,119+32*6),
    }

def shop():
    # original fclShopLayoutPanels FUN_002DD230; actual centers animate
    return [nominal(*xy) for xy in [(311,-17),(564,-17),(-610,-620)]]

def social_portrait():
    # original datCommunityPortraitDraw FUN_0035AFF0, dynamic portrait->position.
    # datPortraitSetValue initial (71.5,106) is NOT final detail/list anchor.
    return {
        "initial_reset_only": nominal(71.5,106),
        "source_half_x_1x":50., "source_half_y_1x":64.,
        "native_half_x_1x":50*SX,"native_half_y_1x":64*SY,
        "ratio_to_make_x_match_native_y_pixels_at_same_local_unit":SY/SX,
    }

if __name__ == "__main__":
    w = wheel(); p=party_hud();f=field_hud();n=weather_news();c=camp();sh=shop();so=social_portrait()
    approx(n["record1_ink_anchor"][0], 123.75)
    approx(n["record1_ink_anchor"][1], 148.1428571429)
    approx(n["first_card"][0],69.)
    approx(n["card_pitch"], 62.25)
    assert n["cards_per_original_source_loop"]==n["cards_visible_in_private_perf06_106"]==6
    assert n["cards_visible_in_private_golden_106"]==7
    approx(f["weather_pair_nominal"][0],437.25)
    approx(f["weather_pair_nominal"][1],30.964285714)
    approx(c["skill_scrollbar"][0],455.25)
    approx(c["skill_row_pitch"],20.6428571429)
    approx(c["config_row_pitch"],19.4285714286)
    approx(so["native_half_x_1x"],37.5)
    approx(so["native_half_y_1x"],38.857142857)
    print("PASS: all nominal coordinates, wheel geometry, aspect, six-vs-seven cards and safe outer ring")
    print("P4P source sx %.6f sy %.6f; wheel native center (%.2f,%.2f), radius (%.2f,%.2f)" % (SX,SY,*w["native_center"],*w["native_radii"]))
    print("Golden-target wheel center (64,203), radius 55.5; local fan scale x %.6f y %.6f" % (w["fan_scale_x"],w["fan_scale_y"]))
    print("Offset original (-10,28), candidate (%.4f,%.4f) SOURCE units (not tested on PSP)" % (w["source_offset_x"],w["source_offset_y"]))
    print("Party HUD nominal roster anchors: %s" % (p,))
    print("Field weather icon/bg nominal anchor %s; dayphase existing Q12 X ratio %.6f" % (f["weather_pair_nominal"],f["existing_p4p_dayphase_q12_x_factor"]))
    print("WeatherNews record1 current nominal %s; candidate top-left source %s" % (n["record1_ink_anchor"],n["candidate_source_title_xy"]))
    print("6 original cards native x first %.2f pitch %.2f; Golden screenshot #106 has 7 different composition" % (n["first_card"][0],n["card_pitch"]))
    print("Skill row pitch %.4f; config row pitch %.4f; social portrait reset source is only initial, not final" % (c["skill_row_pitch"],c["config_row_pitch"]))
    print("Shop temporary panel target anchors: %s (animation + pivot not included)" % (sh,))
