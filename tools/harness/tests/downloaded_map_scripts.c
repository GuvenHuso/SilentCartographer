/*
DOWNLOADED_MAP_SCRIPTS.C (test)

The real re-linking of a Custom Edition map's scripts
(port/linux/game/custom_edition_scripts.c, function_call_convert) on calls
and sets built here, in a map map sharing downloaded and in one of the
player's own: a downloaded map's call of player0_look_invert_pitch and its
set of cheat_deathless_player do nothing, its set of the far clip distance
and its other calls are made as any map's are, and a map of the player's
own may make them all. test_downloaded_map_scripts.py takes the
definitions (definitions.inc) and the file's code to function_call_convert
(under_test.inc).
*/

#include "harness.h"

#include <stdarg.h>
#include <string.h>
#include <strings.h>

#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define csstrcasecmp strcasecmp

enum { _error_silent = 0 };

#include "definitions.inc"

#define hs_type_valid(type) ((type) >= _hs_type_void && (type) < NUMBER_OF_HS_TYPES)

/* the engine globals' designators, with their bit 15 (engine globals') */
#define FAR_CLIP_DESIGNATOR ((short)(0x8000 | 1))
#define DEATHLESS_DESIGNATOR ((short)(0x8000 | 5))

static void error(int kind, char const *format, ...)
{
	va_list arguments;

	(void)kind;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

/* this build's tables: their indices are not the map's */
static char const *const functions[] = { "begin", "set", "ai_place", "player0_look_invert_pitch" };

static short hs_find_function_by_name(char const *name)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(functions); index++)
	{
		if (!strcasecmp(functions[index], name))
			return index;
	}
	return NONE;
}

static short hs_find_global_by_name(char const *name)
{
	if (!strcasecmp(name, "rasterizer_far_clip_distance"))
		return FAR_CLIP_DESIGNATOR;
	if (!strcasecmp(name, "cheat_deathless_player"))
		return DEATHLESS_DESIGNATOR;
	return NONE;
}

#include "under_test.inc"

/* the scripts' strings, each ending in a 0, the last the empty string */
static char const strings[] =
	"set\0player0_look_invert_pitch\0PLAYER0_LOOK_INVERT_PITCH\0ai_place\0"
	"cheat_deathless_player\0rasterizer_far_clip_distance\0";

static long string_offset(char const *text)
{
	long offset = 0;

	while (offset < (long)sizeof(strings))
	{
		if (!strcmp(strings + offset, text))
			return offset;
		offset += (long)strlen(strings + offset) + 1;
	}
	CHECK(FALSE, "no string %s", text);
	return 0;
}

static struct hs_syntax_node nodes[4];
static struct scripts_conversion conversion;

static void world(boolean downloaded)
{
	memset(nodes, 0, sizeof(nodes));
	memset(&conversion, 0, sizeof(conversion));
	conversion.nodes = nodes;
	conversion.node_count = NUMBEROF(nodes);
	conversion.strings = strings;
	conversion.string_bytes = sizeof(strings);
	conversion.empty_string = 0x1000 + (long)sizeof(strings) - 1;
	conversion.downloaded = downloaded;
}

/* a call of `name` (by the map's own index, 40) with a constant argument */
static void call(char const *name, short type)
{
	nodes[0].datum_header = 1;
	nodes[0].function_index = 40;
	nodes[0].type = type;
	nodes[0].next_node_index = NONE;
	nodes[0].data = 1;
	nodes[1].datum_header = 1;
	nodes[1].function_index = 40;
	nodes[1].type = _hs_function_name;
	nodes[1].string_offset = string_offset(name);
	nodes[1].next_node_index = 2;
	nodes[2].datum_header = 1;
	nodes[2].type = _hs_type_boolean;
	nodes[2].flags = FLAG(_hs_syntax_node_primitive_bit) | FLAG(_hs_syntax_node_permanent_bit);
	nodes[2].next_node_index = NONE;
	nodes[0].flags = FLAG(_hs_syntax_node_permanent_bit);
	nodes[1].flags = FLAG(_hs_syntax_node_primitive_bit) | FLAG(_hs_syntax_node_permanent_bit);
}

/* a set of the engine global `name` to a constant */
static void set(char const *name, short designator, short type)
{
	call("set", type);
	nodes[1].next_node_index = 2;
	nodes[2].type = type;
	nodes[2].flags = FLAG(_hs_syntax_node_primitive_bit) | FLAG(_hs_syntax_node_global_bit) |
		FLAG(_hs_syntax_node_permanent_bit);
	nodes[2].string_offset = string_offset(name);
	nodes[2].data = 0;
	nodes[2].short_value = designator;
	nodes[2].next_node_index = 3;
	nodes[3].datum_header = 1;
	nodes[3].type = type;
	nodes[3].flags = FLAG(_hs_syntax_node_primitive_bit) | FLAG(_hs_syntax_node_permanent_bit);
	nodes[3].next_node_index = NONE;
}

static boolean inert(void)
{
	return TEST_FLAG(nodes[0].flags, _hs_syntax_node_primitive_bit) && nodes[0].constant_type == nodes[0].type &&
		nodes[0].data == 0 && TEST_FLAG(nodes[0].flags, _hs_syntax_node_permanent_bit);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("downloaded-invert")
	{
		world(TRUE);
		call("player0_look_invert_pitch", _hs_type_void);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the call is laid out as one");
		CHECK(inert(), "a downloaded map's call of player0_look_invert_pitch does nothing");
		CHECK(conversion.refused == 1 && conversion.missing_count == 0, "it is counted refused, not missing");
	}
	else CASE("downloaded-invert-any-case")
	{
		world(TRUE);
		call("PLAYER0_LOOK_INVERT_PITCH", _hs_type_void);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the call is laid out as one");
		CHECK(inert(), "the name is refused in any case, as the scripts' names are found");
	}
	else CASE("own-invert")
	{
		world(FALSE);
		call("player0_look_invert_pitch", _hs_type_void);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the call is laid out as one");
		CHECK(!inert() && nodes[0].function_index == 3, "a map of the player's own calls it, by this build's index");
		CHECK(conversion.refused == 0, "nothing is refused");
	}
	else CASE("downloaded-other-call")
	{
		world(TRUE);
		call("ai_place", _hs_type_void);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the call is laid out as one");
		CHECK(!inert() && nodes[0].function_index == 2, "a downloaded map's other calls are made");
	}
	else CASE("downloaded-cheat")
	{
		world(TRUE);
		set("cheat_deathless_player", DEATHLESS_DESIGNATOR, _hs_type_boolean);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the set is laid out as a call");
		CHECK(inert(), "a downloaded map's set of cheat_deathless_player writes nothing");
		CHECK(conversion.refused == 1, "it is counted refused");
	}
	else CASE("own-cheat")
	{
		world(FALSE);
		set("cheat_deathless_player", DEATHLESS_DESIGNATOR, _hs_type_boolean);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the set is laid out as a call");
		CHECK(!inert() && nodes[0].function_index == 1, "a map of the player's own sets it");
	}
	else CASE("downloaded-clip")
	{
		world(TRUE);
		set("rasterizer_far_clip_distance", FAR_CLIP_DESIGNATOR, _hs_type_real);
		CHECK(function_call_convert(&conversion, &nodes[0]), "the set is laid out as a call");
		CHECK(!inert() && nodes[0].function_index == 1, "a downloaded map still sets the far clip distance");
		CHECK(conversion.refused == 0, "nothing is refused");
	}
	else
	{
		return 2;
	}
	return 0;
}
