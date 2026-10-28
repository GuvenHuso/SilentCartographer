/*
UNICODE_STRING_BARS.C (test)

The real parse_unicode_string (source/text/draw_string.c), which reads the
wide strings the game draws (players' names, server messages, a map's
string lists) a character at a time, on strings with a '|' in them: "|n" is
the end of a line, any other '|' is a character, and a '|' at a string's
end does not take its terminator. Each string is followed, past its
terminator, by TRAP, which a parse that reads too far returns.
test_unicode_string_bars.py takes the definitions (definitions.inc) and the
code (under_test.inc).
*/

#include "harness.h"

#include <string.h>
#include <wchar.h>

typedef unsigned short word;
typedef unsigned long pixel32;
struct font_header;

#include "definitions.inc"
#include "under_test.inc"

#define TRAP 'X'
#define MAXIMUM_PARSES 16

/* parses text until its end (or MAXIMUM_PARSES), recording each result and
character; returns the number of parses */
static int parse_all(wchar_t const *text, short initial_result, short *results, word *characters)
{
	struct parse_string_state state;
	int count = 0;

	memset(&state, 0, sizeof(state));
	state.string = (byte *)text;
	state.result = initial_result;
	do
	{
		results[count] = parse_unicode_string(&state);
		characters[count] = state.character;
		count++;
	}
	while (results[count - 1] != _parsed_end_of_string && count < MAXIMUM_PARSES);
	return count;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	short results[MAXIMUM_PARSES];
	word characters[MAXIMUM_PARSES];
	int count;
	int index;

	CASE("bar-at-the-end")
	{
		static wchar_t const text[] = { 'a', '|', 0, TRAP, TRAP, 0 };

		count = parse_all(text, _parsed_character, results, characters);
		for (index = 0; index < count; index++)
			CHECK(characters[index] != TRAP, "parse %d read past the string's end", index);
		CHECK(count == 3, "%d parses, not a, | and the end", count);
		CHECK(results[1] == _parsed_character && characters[1] == '|', "the last '|' is a character");
		CHECK(results[2] == _parsed_end_of_string, "the string ends at its terminator");
	}
	else CASE("bar-alone")
	{
		static wchar_t const text[] = { '|', 0, TRAP, 0 };

		count = parse_all(text, _parsed_end_of_line, results, characters);
		CHECK(count == 2, "%d parses, not | and the end", count);
		CHECK(results[0] == _parsed_character && characters[0] == '|', "a string of only '|' draws it");
		CHECK(results[1] == _parsed_end_of_string, "and ends");
	}
	else CASE("bar-n-ends-a-line")
	{
		static wchar_t const text[] = { 'a', '|', 'n', 'b', 0, TRAP, 0 };

		count = parse_all(text, _parsed_character, results, characters);
		CHECK(count == 4, "%d parses, not a, the line's end, b and the end", count);
		CHECK(results[1] == _parsed_end_of_line && characters[1] == '\r', "|n ends the line");
		CHECK(results[2] == _parsed_character && characters[2] == 'b', "the n is not drawn");
		CHECK(results[3] == _parsed_end_of_string, "the string ends");
	}
	else CASE("bar-is-a-character")
	{
		static wchar_t const text[] = { 'a', '|', 'b', 0, TRAP, 0 };

		/* (the result before it is the line's end, which a '|' must not keep) */
		count = parse_all(text, _parsed_end_of_line, results, characters);
		CHECK(count == 4, "%d parses, not a, |, b and the end", count);
		CHECK(results[1] == _parsed_character && characters[1] == '|', "a '|' before b is drawn");
		CHECK(results[2] == _parsed_character && characters[2] == 'b', "and so is the b");
	}
	else
	{
		return 2;
	}
	return 0;
}
