"""Which hosts a client joins by their network version: one of its own version, or a VR server of its version and VR
revision; what it tells the player of the others (halo_port_limits.h, network_client_manager.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run


def generated(fault=None):
    limits = read('port/linux/include/halo_port_limits.h').replace('\r\n', '\n')
    client = read('source/networking/network_client_manager.c').replace('\r\n', '\n')
    defines = re.findall(r'^#define HALO_PORT_(?:NETWORK_VERSION|NETWORK_VR_|ADVERTISED_)\w*(?:[^\n]*\\\n)*[^\n]*', limits,
                         re.M)
    versions = re.search(r'^static struct\n\{\n\tword version;.*?\} network_game_client_advertised_versions\['
                         r'MAXIMUM_NETWORK_ADVERTISED_GAMES\];', client, re.M | re.S).group(0)
    code = '\n'.join(defines + [versions, function(client, 'network_game_client_advertised_game_compatible'),
                                function(client, 'network_game_client_advertised_game_kind')])
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


CASES = ['standard', 'vr', 'vr_revision', 'others', 'lockstep', 'official']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('network_version', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('(revision) == HALO_PORT_NETWORK_VR_REVISION))', '1))'), 'vr_revision'),
    (('#define HALO_PORT_NETWORK_VR_SERVER_BIT 0x8000', '#define HALO_PORT_NETWORK_VR_SERVER_BIT 0x0000'), 'official'),
    (('((theirs) == HALO_PORT_NETWORK_VERSION ||', '(0 ||'), 'standard'),
    (('\ttheirs &= ~HALO_PORT_NETWORK_VR_SERVER_BIT;\n', ''), 'vr_revision'),
    (('HALO_PORT_NETWORK_VR_SERVER_BIT) ?\n\t\t\t\tHALO_PORT_ADVERTISED_KIND_VR : 0','HALO_PORT_NETWORK_VR_SERVER_BIT) ?\n\t\t\t\t0 : 0'),
     'vr'),
])
def test_negative_control(fault, case):
    status, output = run(build('network_version', generated(fault)), case)
    assert status == CHECK_FAILED, output
