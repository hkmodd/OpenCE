"""The weapon in a headset's player's other hand as a first-person weapon of its own: its index, the unit's messages
of it, taken up and let go of with the hand, and effects on it its local player's
(source/interface/first_person_weapons.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run  # noqa: E402

CASES = ['index', 'get', 'owner', 'take', 'undrawable', 'let_go', 'unit']
CONTROLS = [
    ('under_test.inc', 'return (short)(local_player_index+FIRST_PERSON_WEAPON_OTHER_HAND);',
     'return local_player_index;', 'index'),
    ('under_test.inc', 'if (local_player_index>=FIRST_PERSON_WEAPON_OTHER_HAND)',
     'if (local_player_index>FIRST_PERSON_WEAPON_OTHER_HAND)', 'get'),
    ('under_test.inc', 'local_player_index<NUMBER_OF_FIRST_PERSON_WEAPONS; local_player_index++',
     'local_player_index<MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++', 'get'),
    ('header.inc', 'FIRST_PERSON_WEAPON_LOCAL_PLAYER(index)==(local_player_index)', '(index)==(local_player_index)',
     'owner'),
    ('header.inc', '((index)>=0 && ', '(', 'owner'),
    ('under_test.inc', 'if (weapon_index!=NONE)\n\t\t{\n\t\t\tfirst_person_weapon_message(index, '
     '_first_person_weapon_message_ready);', 'if (FALSE)\n\t\t{\n\t\t\tfirst_person_weapon_message(index, '
     '_first_person_weapon_message_ready);', 'take'),
    ('under_test.inc', ' &&\n\t\tfirst_person_weapon_other_hand_taken[local_player_index]!=weapon_index)', ')',
     'undrawable'),
    ('under_test.inc', '\t\tfirst_person_weapon->unit_index= NONE;\n\t\treturn;', '\t\treturn;', 'let_go'),
    ('under_test.inc', 'if (first_person_weapon->unit_index!=unit_index)\n\t{\n\t\tfirst_person_weapon_other_hand_taken',
     'if (FALSE)\n\t{\n\t\tfirst_person_weapon_other_hand_taken', 'unit'),
]


def generated(fault=None):
    header = read('source/interface/first_person_weapons.h')
    source = read('source/interface/first_person_weapons.c').replace('\r\n', '\n')
    config = '\n'.join([
        'enum { MAXIMUM_NUMBER_OF_LOCAL_PLAYERS = %d };' % constant(read('source/networking/network_connection.h'),
                                                                    'MAXIMUM_NUMBER_OF_LOCAL_PLAYERS'),
        enum_with(read('source/items/weapons.h'), '_first_person_weapon_message_ready'),
    ]) + '\n'
    macros = header[header.index('#define FIRST_PERSON_WEAPON_LOCAL_PLAYER'):]
    macros = macros[:macros.index('(local_player_index))\n') + len('(local_player_index))\n')]
    header_code = enum_with(header, 'FIRST_PERSON_WEAPON_OTHER_HAND= 4') + '\n' + macros
    statics = source[source.index('static struct first_person_weapon first_person_weapon_other_hands'):]
    statics = statics[:statics.index('? 1 : -1];') + len('? 1 : -1];')]
    code = '\n'.join([
        statics,
        function(source[source.index('/* ---------- private code */'):], 'first_person_weapon_get'),
        function(source, 'first_person_weapon_new_unit'),
        function(source[source.index('/* ---------- private code */'):], 'first_person_weapon_index_from_weapon_index'),
        function(source, 'first_person_weapon_get_local_index'),
        function(source[source.index('/* ---------- public code */'):], 'first_person_weapon_other_hand_update'),
    ])
    files = {'header.inc': header_code, 'under_test.inc': code}
    if fault:
        files[fault[0]] = mutated(files[fault[0]], fault[1], fault[2])
    return (('config.inc', config), ('header.inc', files['header.inc']), ('under_test.inc', files['under_test.inc']))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('first_person_other_hand', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('file,before,after,case', CONTROLS)
def test_negative_control(file, before, after, case):
    status, output = run(build('first_person_other_hand', generated((file, before, after))), case)
    assert status == CHECK_FAILED, output


def test_messages_and_draw_take_either_hand():
    # The unit's messages, the effects and the drawing go through the index: every first-person weapon is posed,
    # drawn and updated, the other hand's after the weapon in hand's.
    source = read('source/interface/first_person_weapons.c')
    draw = function(source, 'first_person_weapon_draw')
    assert draw.index('first_person_weapon_draw_weapon(render.local_player_index)') < \
        draw.index('FIRST_PERSON_WEAPON_OTHER_HAND')
    update = function(source, 'first_person_weapons_update')
    assert update.index('first_person_weapon_update(local_player_index);') < \
        update.index('first_person_weapon_other_hand_update(local_player_index, unit_index);')
    particles = read('source/render/render_particles.c')
    assert 'FIRST_PERSON_WEAPON_OF_LOCAL_PLAYER(particle->local_player_index, local_player_index)' in particles
