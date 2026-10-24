"""The headsets' players' poses in a VR server's game: the wire, the poses fit to take, the host taking only the
sending machine's own player's and sending it on, a client keeping the others' (port/linux/game/network_vr.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402


def generated(fault=None):
    header = read('port/linux/game/network_vr.h')
    distributed_header = read('port/linux/game/network_distributed.h')
    distributed = read('port/linux/game/network_distributed.c')
    enums = '\n'.join([enum_with(read('source/game/game.h'), '_game_connection_local = 0'),
                       enum_with(distributed_header, '_distributed_message_vr_pose_up'),
                       enum_with(distributed_header, '_distributed_to_host,'),
                       enum_with(read('source/networking/network_game_manager.h'),
                                 '_network_game_port_vr_rules_bit')]) + '\n'
    source = read('port/linux/game/network_vr.c')
    code = '\n'.join([enum_with(header, '_network_vr_pose_left_hand_bit'), structure(header, 'network_vr_pose')] +
                     re.findall(r'^#define NETWORK_VR_\w+ .*$', header, re.M) +
                     [structure(distributed_header, 'distributed_vector'),
                      structure(distributed_header, 'distributed_message_header'),
                      function(distributed, 'distributed_axes_make_valid'),
                      function(distributed, 'distributed_vector_pack'),
                      function(distributed, 'distributed_vector_unpack'),
                      function(distributed, 'distributed_unit_vector_unpack'),
                      source[source.index('#define NETWORK_VR_PLACE_SCALE'):]])
    if fault:
        code = mutated(code, *fault)
    return (('enums.inc', enums), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['wire', 'valid', 'host', 'send', 'client'])
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
    (('if (!TEST_FLAG(pose->flags, _network_vr_pose_left_hand_bit + hand))\n\t\t\tcontinue;', ''), 'wire'),
    (('time == network_vr_sent_time ||', ''), 'send'),
    (('distributed_player_is_local(distributed_player_from_byte(entry.player_index))', 'FALSE'), 'client'),
    (('age <= NETWORK_VR_POSE_TICKS ?', 'TRUE ?'), 'client'),
])
def test_negative_control(fault, case):
    status, output = run(build('network_vr', generated(fault)), case)
    assert status == CHECK_FAILED, output
