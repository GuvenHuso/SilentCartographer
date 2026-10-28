"""A downloaded map's scripts: what may change the player's profile or the game past the map does nothing."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, mutated, read, run, structure


def generated(fault=None):
    hs = read('source/hs/hs.h')
    definitions = enum_with(hs, '_hs_unparsed = 0') + '\n' + \
        structure(read('source/hs/hs_scenario_definitions.h'), 'hs_syntax_node') + '\n'
    source = read('port/linux/game/custom_edition_scripts.c')
    # (the file's constants, globals and structures, and its code to the conversion of a call)
    code = source[source.index('/* ---------- constants */'):
                  source.index("/* FALSE when the global's name is not in the string data */")]
    if fault:
        code = mutated(code, *fault)
    return (('definitions.inc', definitions), ('under_test.inc', code))


CASES = ['downloaded-invert', 'downloaded-invert-any-case', 'own-invert', 'downloaded-other-call', 'downloaded-cheat',
         'own-cheat', 'downloaded-clip']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('downloaded_map_scripts', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    # (no function refused)
    (('return conversion->downloaded &&\n\t\tname_listed(name, downloaded_map_functions_refused',
      'return FALSE &&\n\t\tname_listed(name, downloaded_map_functions_refused'), 'downloaded-invert'),
    # (refused in every map, not only downloaded ones)
    (('return conversion->downloaded &&\n\t\tname_listed(name, downloaded_map_functions_refused',
      'return name_listed(name, downloaded_map_functions_refused'), 'own-invert'),
    # (no engine global refused)
    (('return conversion->downloaded && name &&', 'return FALSE && name &&'), 'downloaded-cheat'),
    # (the visual globals refused too)
    (('"stun_enable",', '"stun_enable",\n\t"rasterizer_far_clip_distance",'), 'downloaded-clip'),
])
def test_negative_control(fault, case):
    status, output = run(build('downloaded_map_scripts', generated(fault)), case)
    assert status == CHECK_FAILED, output
