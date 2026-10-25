"""The headsets' players' poses in a VR server's game: the wire, the poses fit to take, the host taking only the
sending machine's own player's and sending it on, a client keeping the others' (port/linux/game/network_vr.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, inline, mutated, read, run, structure  # noqa: E402


def generated(fault=None):
    header = read('port/linux/game/network_vr.h')
    distributed_header = read('port/linux/game/network_distributed.h')
    distributed = read('port/linux/game/network_distributed.c')
    enums = '\n'.join([enum_with(read('source/game/game.h'), '_game_connection_local = 0'),
                       enum_with(distributed_header, '_distributed_message_vr_pose_up'),
                       enum_with(distributed_header, '_distributed_to_host,'),
                       enum_with(read('source/networking/network_game_manager.h'),
                                 '_network_game_port_vr_rules_bit'),
                       enum_with(read('source/units/units.h'), 'MAXIMUM_WEAPONS_PER_UNIT = 4'),
                       enum_with(read('source/physics/collisions.h'),
                                 '_collision_test_front_facing_surfaces_bit = 0')]) + '\n'
    source = read('port/linux/game/network_vr.c')
    code = '\n'.join([enum_with(header, '_network_vr_pose_left_hand_bit'), structure(header, 'network_vr_pose')] +
                     re.findall(r'^#define NETWORK_VR_\w+ .*$', header, re.M) +
                     [structure(distributed_header, 'distributed_vector'),
                      structure(distributed_header, 'distributed_message_header'),
                      function(distributed, 'distributed_axes_make_valid'),
                      function(distributed, 'distributed_vector_pack'),
                      function(distributed, 'distributed_vector_unpack'),
                      function(distributed, 'distributed_unit_vector_unpack'),
                      inline(read('source/math/real_math.h'), 'subtract_vectors3d'),
                      inline(read('source/math/real_math.h'), 'dot_product3d'),
                      source[source.index('#define NETWORK_VR_PLACE_SCALE'):]])
    if fault:
        code = mutated(code, *fault)
    return (('enums.inc', enums), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['wire', 'valid', 'host', 'send', 'client', 'drop', 'four', 'throw', 'melee'])
def test_case(case):
    status, output = run(build('network_vr', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('!distributed_machine_has_player(machine_index, player_index)', 'FALSE'), 'host'),
    (('if (count != 1 || !network_vr_server())', 'if (count != 1)'), 'host'),
    (('machines[index] != from_machine_index && ', ''), 'host'),
    (('time <= network_vr_poses[player_index].time', 'time < network_vr_poses[player_index].time'), 'host'),
    (('> NETWORK_VR_HEAD_REACH * NETWORK_VR_HEAD_REACH', '> 4.0f * NETWORK_VR_HEAD_REACH * NETWORK_VR_HEAD_REACH'),
     'valid'),
    (('NETWORK_VR_HAND_REACH * NETWORK_VR_HAND_REACH ||', '4.0f * NETWORK_VR_HAND_REACH * NETWORK_VR_HAND_REACH ||'),
     'valid'),
    (('pose->flags >= FLAG(NUMBER_OF_NETWORK_VR_POSE_BITS) ||', ''), 'valid'),
    (('pose->off_slot >= MAXIMUM_WEAPONS_PER_UNIT ||', 'pose->off_slot > MAXIMUM_WEAPONS_PER_UNIT ||'), 'valid'),
    (('pose->off_slot < NONE ||', ''), 'valid'),
    (('!(pose->off_trigger >= 0.0f && pose->off_trigger <= 1.0f)', 'pose->off_trigger > 1.0f'), 'valid'),
    (('PIN(pose->off_trigger, 0.0f, 1.0f) * 255.0f', 'pose->off_trigger * 255.0f'), 'wire'),
    (('\tpose->off_slot = entry->off_slot;\n', ''), 'wire'),
    (('if (!TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand))\n\t\t\tcontinue;', ''), 'wire'),
    (('time == network_vr_sent_time ||', ''), 'send'),
    (('distributed_player_is_local(distributed_player_from_byte(entry.player_index))', 'FALSE'), 'client'),
    (('age <= NETWORK_VR_POSE_TICKS ?', 'TRUE ?'), 'client'),
    (('.taken_time = game_time_get();', '.taken_time = time;'), 'client'),
    (('if (count != 1 || !stream || !network_vr_server())', 'if (count != 1 || !network_vr_server())'), 'drop'),
    (('!distributed_machine_has_player(machine_index, entry.player_index))\n\t{\n\t\treturn;\n\t}\n'
      '\tdistributed_vector_unpack(&entry.velocity, NETWORK_VR_VELOCITY_SCALE, &velocity);\n\tif (network_vr_drop_weapon',
      'FALSE)\n\t{\n\t\treturn;\n\t}\n'
      '\tdistributed_vector_unpack(&entry.velocity, NETWORK_VR_VELOCITY_SCALE, &velocity);\n\tif (network_vr_drop_weapon'),
     'drop'),
    (('if (count != 1 || !stream || !network_vr_server())', 'if (count != 1 || !stream)'), 'drop'),
    (('|| slot == unit->unit.current_weapon_index)\n\t\t\treturn FALSE;', ')\n\t\t\treturn FALSE;'), 'drop'),
    (('message.entry.slot = (byte)(slot + 1);', 'message.entry.slot = (byte)slot;'), 'drop'),
    (('(network_vr_server() || vr_rules_active())))\n\t{\n\t\tnetwork_vr_drop_weapon',
      'network_vr_server()))\n\t{\n\t\tnetwork_vr_drop_weapon'), 'drop'),
    (('(network_vr_server() || vr_rules_active())))\n\t{\n\t\tnetwork_vr_drop_weapon',
      'TRUE))\n\t{\n\t\tnetwork_vr_drop_weapon'), 'drop'),
    (('(network_vr_server() || vr_rules_active())))\n\t{\n\t\tnetwork_vr_throw_take',
      'network_vr_server()))\n\t{\n\t\tnetwork_vr_throw_take'), 'throw'),
    (('return network_vr_server() && network_vr_pose_get(unit->unit.player_index) != NULL;',
      'return network_vr_server();'), 'four'),
    (('\tif (distributed_player_is_local(unit->unit.player_index))\n\t\treturn vr_render_carries_four(unit_index);\n', ''),
     'four'),
    (('if (!unit || unit->object.parent_object_index != NONE ||', 'if (!unit ||'), 'drop'),
    (('now - network_vr_drop_times[player_index] < NETWORK_VR_DROP_TICKS', 'FALSE'), 'drop'),
    (('if (!(speed <= NETWORK_VR_DROP_SPEED))', 'if (0)'), 'drop'),
    (('_game_connection_network_client && network_vr_server())\n\t{\n\t\tcsmemset(&message, 0, sizeof(message));\n'
      '\t\tmessage.entry.player_index = (byte)player_index;\n\t\tmessage.entry.slot',
      '_game_connection_network_client)\n\t{\n\t\tcsmemset(&message, 0, sizeof(message));\n'
      '\t\tmessage.entry.player_index = (byte)player_index;\n\t\tmessage.entry.slot'), 'drop'),
    (('_distributed_message_vr_drop, 1, sizeof(message), _distributed_to_host_reliably);',
      '_distributed_message_vr_drop, 1, sizeof(message), _distributed_to_host);'), 'drop'),
    (('network_vr_throws[player_index].time = NONE;\n\tgame_speed', 'game_speed'), 'throw'),
    (('age > NETWORK_VR_THROW_TICKS ||', 'age > 4 * NETWORK_VR_THROW_TICKS ||'), 'throw'),
    (('if (struck == NONE || ahead > farthest)', 'if (struck == NONE || ahead < farthest)'), 'melee'),
    (('point_from_line3d(&unit->object.position, &pose->hands[struck], 1.0f, point);',
      'point_from_line3d(&unit->object.position, &pose->hands[1], 1.0f, point);'), 'melee'),
    (('if (!TEST_FLAG(pose->flags, hand ? _network_vr_pose_right_hand_bit : _network_vr_pose_left_hand_bit))\n'
      '\t\t\tcontinue;', ''), 'melee'),
    (('NETWORK_VR_THROW_LEAST, NETWORK_VR_THROW_MOST)', '0.0f, NETWORK_VR_THROW_MOST)'), 'throw'),
    (('NETWORK_VR_THROW_LEAST, NETWORK_VR_THROW_MOST)', 'NETWORK_VR_THROW_LEAST, 1e3f)'), 'throw'),
    (('PIN(speed / NETWORK_VR_THROW_FULL,', 'PIN(speed * 1.4f,'), 'throw'),
    (('if (reach > NETWORK_VR_HAND_REACH)', 'if (0)'), 'throw'),
    (('!(speed <= 1e3f) || !(reach <= 1e3f)', 'speed > 1e3f || reach > 1e3f'), 'throw'),
    (('&eyes, &offset, unit_index, &collision))', '&eyes, &offset, unit_index, &collision) && 0)'), 'throw'),
    (('if (!stream || count != 1 || !network_vr_server())', 'if (count != 1 || !network_vr_server())'), 'throw'),
    (('if (!stream || count != 1 || !network_vr_server())', 'if (!stream || !network_vr_server())'), 'throw'),
    (('if (!stream || count != 1 || !network_vr_server())', 'if (!stream || count != 1)'), 'throw'),
    (('!distributed_machine_has_player(machine_index, entry.player_index))\n\t{\n\t\treturn;\n\t}\n'
      '\tdistributed_vector_unpack(&entry.velocity, NETWORK_VR_VELOCITY_SCALE, &velocity);\n'
      '\tdistributed_vector_unpack(&entry.offset',
      'FALSE)\n\t{\n\t\treturn;\n\t}\n'
      '\tdistributed_vector_unpack(&entry.velocity, NETWORK_VR_VELOCITY_SCALE, &velocity);\n'
      '\tdistributed_vector_unpack(&entry.offset'), 'throw'),
    (('\t\t/* (kept here too, for a throw this machine predicts) */\n\t\tnetwork_vr_throw_take(player_index, velocity, offset);\n',
      ''), 'throw'),
    (('_distributed_message_vr_throw, 1, sizeof(message), _distributed_to_host_reliably);',
      '_distributed_message_vr_throw, 1, sizeof(message), _distributed_to_host);'), 'throw'),
    (('\t\tnetwork_vr_throw_take(player_index, velocity, offset);\n\t}\n\telse if (game_connection() == '
      '_game_connection_network_client && network_vr_server())',
      '\t\tnetwork_vr_throw_take(player_index, velocity, offset);\n\t}\n\telse if (game_connection() == '
      '_game_connection_network_client)'), 'throw'),
])
def test_negative_control(fault, case):
    status, output = run(build('network_vr', generated(fault)), case)
    assert status == CHECK_FAILED, output
