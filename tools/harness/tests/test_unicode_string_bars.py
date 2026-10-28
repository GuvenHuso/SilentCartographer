"""The game's wide strings' '|': "|n" ends a line, any other '|' is a character, and none reads past a string's end."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure


def generated(fault=None):
    source = read('source/text/draw_string.c')
    definitions = enum_with(source, '_parsed_end_of_string = 0') + '\n' + structure(source, 'parse_string_state') + '\n'
    code = function(source, 'parse_unicode_string')
    if fault:
        code = mutated(code, *fault)
    return (('definitions.inc', definitions), ('under_test.inc', code))


CASES = ['bar-at-the-end', 'bar-alone', 'bar-n-ends-a-line', 'bar-is-a-character']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('unicode_string_bars', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    # (the original's: the character after every '|' taken before looking at it)
    (("if (string[state->string_index] == 'n')", "if (string[state->string_index++] == 'n')"), 'bar-at-the-end'),
    (("if (string[state->string_index] == 'n')", "if (string[state->string_index++] == 'n')"), 'bar-is-a-character'),
    # (a '|' that keeps the result before it)
    (('state->result = _parsed_character;\n\t\t}\n\t\tbreak;', '}\n\t\tbreak;'), 'bar-alone'),
    # (the n of "|n" left to be drawn)
    (("state->string_index++;\n\t\t\tstate->character = '\\r';", "state->character = '\\r';"), 'bar-n-ends-a-line'),
])
def test_negative_control(fault, case):
    status, output = run(build('unicode_string_bars', generated(fault)), case)
    assert status == CHECK_FAILED, output
