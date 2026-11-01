"""The server browser's intake (p2p_lobby.c's listing_wanted): which listings take a queue place, and a broker's burst of kept slots leaving every live game one."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

SIZES = ('P2P_KEY_SIZE', 'P2P_KEY_HASH_SIZE', 'P2P_TOKEN_SIZE', 'P2P_SEALED_TOKEN_SIZE', 'P2P_SIGNATURE_SIZE',
         'P2P_LISTING_NAME_SIZE', 'P2P_LISTING_MAP_SIZE', 'P2P_LISTING_GAMETYPE_SIZE')


def size(source, name):
    """an enum constant set to a small sum of numbers (P2P_SEALED_TOKEN_SIZE = 24 + 16 + 16)"""
    value = re.search(r"\b" + name + r"\s*=\s*([\d +]+)[,\n]", source).group(1)
    return sum(int(part) for part in value.split("+"))


def generated(fault=None):
    source = read('port/linux/src/p2p_lobby.c')
    headers = read('port/linux/src/p2p_internal.h') + read('port/linux/src/p2p.h')
    limits = read('port/linux/include/halo_port_limits.h')
    version = re.search(r"#define HALO_PORT_NETWORK_VERSION (\d+)", limits)
    config = ''.join(f'#define {name} {size(headers, name)}\n' for name in SIZES)
    config += f'#define HALO_PORT_NETWORK_VERSION {version.group(1)}\n'
    # (a VR server's version and revision, and which of them this build joins)
    config += re.search(r"^#define HALO_PORT_NETWORK_VR_SERVER_BIT .*$", limits, re.M).group(0) + '\n'
    config += re.search(r"^#define HALO_PORT_NETWORK_VR_REVISION .*$", limits, re.M).group(0) + '\n'
    config += re.search(r"^#define HALO_PORT_NETWORK_VERSION_JOINABLE(?:.*\\\r?\n)*.*$", limits, re.M).group(0) + '\n'
    # (the file's constants: the listing's format and sizes, the windows and the queue's places)
    config += re.search(r"^enum\n\{\n\tLISTING_FORMAT.*?^\};\n", source, re.M | re.S).group(0)
    code = '\n'.join([structure(source, 'listing')] +
                     [function(source, name) for name in ('sanitize', 'get_long', 'get_text', 'listing_read',
                                                          'listing_current', 'listing_wanted')])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', ['filter', 'burst'])
def test_case(case):
    status, output = run(build('lobby_intake', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('case,fault', [
    # every listing of this version let in, however old
    ('burst', ('return listing_current(&listing, retained);', 'return 1;')),
    # every version let in
    ('burst', ('if (!HALO_PORT_NETWORK_VERSION_JOINABLE(listing.version, listing.vr_revision))', 'if (0)')),
    # a VR server's listing refused as another version's
    ('filter', ('if (!HALO_PORT_NETWORK_VERSION_JOINABLE(listing.version, listing.vr_revision))',
                'if (listing.version != HALO_PORT_NETWORK_VERSION)')),
    # a newer VR revision not told
    ('filter', ('listing.vr_revision > HALO_PORT_NETWORK_VR_REVISION', '0')),
])
def test_negative_control(case, fault):
    status, output = run(build('lobby_intake', generated(fault)), case)
    assert status == CHECK_FAILED, output
