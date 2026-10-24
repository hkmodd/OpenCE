"""The other headsets' players' arms: the wrist's place reached with the arm's own bones, the elbow down, straight
beyond its reach, the hand turned to its controller's way (port/linux/game/network_vr_body.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402


def generated(fault=None):
    source = read('port/linux/game/network_vr_body.c')
    code = '\n'.join(re.findall(r'^#define NETWORK_VR_BODY_\w+ .*$', source, re.M) +
                     [function(source, name) for name in (
                         'network_vr_body_frame', 'network_vr_body_turn_frames', 'network_vr_body_turn_node',
                         'network_vr_body_least_turn', 'network_vr_body_way', 'network_vr_body_arm')])
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['reach', 'straight', 'hand'])
def test_case(case):
    status, output = run(build('network_vr_body', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('pole.k = outward->k * 0.5f - aim->forward.k * 0.3f - 1.0f;', 'pole.k = outward->k * 0.5f + 1.0f;'), 'reach'),
    (('point_from_line3d(grip, &controller.forward, -NETWORK_VR_BODY_WRIST_BACK, &wrist);',
      'wrist = *grip;'), 'reach'),
    (('\tnodes[forearm].position = elbow;\n', ''), 'reach'),
    (('if (distance > (upper + fore) * NETWORK_VR_BODY_STRAIGHT)\n\t\tdistance = (upper + fore) * NETWORK_VR_BODY_STRAIGHT;',
      ''), 'straight'),
    (('network_vr_body_turn_node(aim, &controller, &nodes[hand]);', ''), 'hand'),
    (('if (!network_vr_body_frame(grip_forward, grip_up, &controller))\n\t\treturn;', ''), 'hand'),
])
def test_negative_control(fault, case):
    status, output = run(build('network_vr_body', generated(fault)), case)
    assert status == CHECK_FAILED, output
