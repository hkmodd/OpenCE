/*
TAG_VALIDATE_WALKS.C

The validator (port/linux/game/tag_validate.c), taken whole, walking made-up
schemas over tags laid out in a buffer: which blocks' elements its passes
enter, which elements it takes through the extents' pass, and that skipping
the others changes nothing (corrections, checks, refusals).
*/

#define boolean harness_boolean
#include "harness.h"
#undef boolean
typedef byte boolean;
#include <stdarg.h>
#include <stddef.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define UNSIGNED_SHORT_MAX 0xFFFF

#include "game_types.inc"

/* the elements the validator enters (validate_element), those it takes
through the extents' pass, the checks it calls, and its messages */
static long harness_entered;
static long harness_extents;
static long harness_checks;
static char harness_last_message[512];

void tag_validate_report(
	char const *message)
{
	snprintf(harness_last_message, sizeof(harness_last_message), "%s", message);
}

#include "tag_schema.h"
#include "under_test.inc"

struct tag_schema_group const tag_schema_custom_edition_groups[] = { { 0 } };
static struct tag_schema_group const harness_no_groups[] = { { 0 } };
struct tag_schema_group const *const tag_schema_group_lists[] = { harness_no_groups, NULL };

static boolean count_check(
	struct tag_validation *validation,
	void *base)
{
	(void)validation;
	(void)base;
	harness_checks++;
	return TRUE;
}

/* ---------- the made-up schemas */

/* an element that does nothing in any pass */
struct empty_element { long value; };
static struct tag_schema_field const empty_fields[] = { TAG_SCHEMA_END };
static struct tag_schema_definition const empty_definition = TAG_SCHEMA_DEFINITION(empty, struct empty_element,
	empty_fields);

/* an element with a value (an enum of 3) */
struct enum_element { short kind; short pad; };
static struct tag_schema_field const enum_fields[] = { TAG_SCHEMA_ENUM(struct enum_element, kind, 3, 0), TAG_SCHEMA_END };
static struct tag_schema_definition const enum_definition = TAG_SCHEMA_DEFINITION(enum, struct enum_element,
	enum_fields);

/* an element with a check only */
static struct tag_schema_field const checked_fields[] = { TAG_SCHEMA_CHECK(count_check), TAG_SCHEMA_END };
static struct tag_schema_definition const checked_definition = TAG_SCHEMA_DEFINITION(checked, struct empty_element,
	checked_fields);

/* an element whose block's elements have a check */
struct outer_element { struct tag_block inner; };
static struct tag_schema_field const outer_fields[] = {
	TAG_SCHEMA_BLOCK(struct outer_element, inner, checked_definition, 0), TAG_SCHEMA_END };
static struct tag_schema_definition const outer_definition = TAG_SCHEMA_DEFINITION(outer, struct outer_element,
	outer_fields);

/* an element whose structure has a check */
struct holder_element { struct empty_element held; };
static struct tag_schema_field const holder_fields[] = {
	TAG_SCHEMA_STRUCT(struct holder_element, held, checked_definition), TAG_SCHEMA_END };
static struct tag_schema_definition const holder_definition = TAG_SCHEMA_DEFINITION(holder, struct holder_element,
	holder_fields);

/* an element with data (of at most 16 bytes) */
struct data_element { struct tag_data data; };
static struct tag_schema_field const data_fields[] = { TAG_SCHEMA_DATA(struct data_element, data, 16), TAG_SCHEMA_END };
static struct tag_schema_definition const data_definition = TAG_SCHEMA_DEFINITION(data, struct data_element,
	data_fields);

/* an element whose structure has data */
struct data_holder_element { struct data_element held; };
static struct tag_schema_field const data_holder_fields[] = {
	TAG_SCHEMA_STRUCT(struct data_holder_element, held, data_definition), TAG_SCHEMA_END };
static struct tag_schema_definition const data_holder_definition = TAG_SCHEMA_DEFINITION(data_holder,
	struct data_holder_element, data_holder_fields);

/* a root of one block */
struct root { struct tag_block block; };
#define ROOT_DEFINITION(name, element_definition) \
	static struct tag_schema_field const name##_fields[] = { \
		TAG_SCHEMA_BLOCK(struct root, block, element_definition, 0), TAG_SCHEMA_END }; \
	static struct tag_schema_definition const name = TAG_SCHEMA_DEFINITION(name, struct root, name##_fields)
ROOT_DEFINITION(empty_root, empty_definition);
ROOT_DEFINITION(enum_root, enum_definition);
ROOT_DEFINITION(checked_root, checked_definition);
ROOT_DEFINITION(outer_root, outer_definition);
ROOT_DEFINITION(holder_root, holder_definition);
ROOT_DEFINITION(data_root, data_definition);
ROOT_DEFINITION(data_holder_root, data_holder_definition);

/* blocks nested as deep as the validator goes: the root's block and each
element's are a chain of DEPTH_CHAIN_LENGTH (chain.inc), the last of empty
elements */
#include "chain.inc"

/* ---------- the tags */

static long harness_tags_words[0x40000];
static byte *const harness_tags = (byte *)harness_tags_words;
static unsigned long harness_used;

static void *tags_new(
	unsigned long size)
{
	void *address = harness_tags + harness_used;

	harness_used += (size + 3) & ~3UL;
	CHECK(harness_used <= sizeof(harness_tags_words), "the tags are full");
	return address;
}

static void *block_new(
	struct tag_block *block,
	long count,
	struct tag_schema_definition const *definition)
{
	block->count = count;
	block->address = tags_new((unsigned long)(count * definition->size));
	return block->address;
}

/* the root through both walks, as tag_validate_tags takes each tag: TRUE
if it was not refused */
static boolean walk(
	void *root,
	struct tag_schema_definition const *definition,
	long *corrections)
{
	struct tag_validation validation;

	validation_begin(&validation, harness_tags, (long)sizeof(harness_tags_words), sizeof(harness_tags_words), FALSE,
		"harness");
	CHECK(definition_fits(definition, 0), "the made-up schema does not fit");
	claim(root, (unsigned long)definition->size);
	validate_tag(&validation, NONE, root, definition, _pass_values);
	if (!validation.refused)
		validate_tag(&validation, NONE, root, definition, _pass_checks);
	*corrections = validation.corrections;
	return !validation.refused;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	long corrections;

	/* (zeros where a tags' header would be, which messages read) */
	tags_new(64);

	/* (the elements of a block that do nothing are not entered: the root,
	once in each walk) */
	CASE("idle-elements-skipped")
	{
		struct root *root = tags_new(sizeof(*root));

		block_new(&root->block, 1000, &empty_definition);
		CHECK(walk(root, &empty_root, &corrections), "refused: %s", harness_last_message);
		CHECK(corrections == 0, "%ld corrections", corrections);
		CHECK(harness_entered == 2, "%ld elements entered for 1000 that do nothing", harness_entered);
		return 0;
	}
	/* (data of too many bytes, cut in every element) */
	CASE("data-extents")
	{
		struct root *root = tags_new(sizeof(*root));
		struct data_element *elements = block_new(&root->block, 3, &data_definition);
		long index;

		for (index = 0; index < 3; index++)
		{
			elements[index].data.size = 100;
			elements[index].data.address = tags_new(100);
		}
		CHECK(walk(root, &data_root, &corrections), "refused: %s", harness_last_message);
		CHECK(corrections == 3, "%ld corrections for 3 data of 100 bytes", corrections);
		for (index = 0; index < 3; index++)
			CHECK(elements[index].data.size == 16, "element %ld has %ld bytes", index, elements[index].data.size);
		CHECK(harness_extents == 1 + 3, "%ld elements through the extents' pass", harness_extents);
		return 0;
	}
	/* (data in an element's structure) */
	CASE("structure-data-extents")
	{
		struct root *root = tags_new(sizeof(*root));
		struct data_holder_element *elements = block_new(&root->block, 3, &data_holder_definition);
		long index;

		for (index = 0; index < 3; index++)
		{
			elements[index].held.data.size = 100;
			elements[index].held.data.address = tags_new(100);
		}
		CHECK(walk(root, &data_holder_root, &corrections), "refused: %s", harness_last_message);
		CHECK(corrections == 3, "%ld corrections for 3 data of 100 bytes", corrections);
		CHECK(harness_extents == 1 + 3, "%ld elements through the extents' pass", harness_extents);
		return 0;
	}
	/* (values are corrected in every element) */
	CASE("values")
	{
		struct root *root = tags_new(sizeof(*root));
		struct enum_element *elements = block_new(&root->block, 5, &enum_definition);
		long index;

		for (index = 0; index < 5; index++)
			elements[index].kind = (short)(3 + index);
		CHECK(walk(root, &enum_root, &corrections), "refused: %s", harness_last_message);
		CHECK(corrections == 5, "%ld corrections for 5 enums past their values", corrections);
		for (index = 0; index < 5; index++)
			CHECK(elements[index].kind == 0, "element %ld is %d", index, elements[index].kind);
		/* (and not entered in the checks' walk, which has nothing to do in them,
		nor taken through the extents' pass, which has nothing to do in them
		either) */
		CHECK(harness_entered == 1 + 5 + 1, "%ld elements entered", harness_entered);
		CHECK(harness_extents == 1, "%ld elements through the extents' pass", harness_extents);
		return 0;
	}
	/* (checks are called on every element, which the first walk need not enter) */
	CASE("checks")
	{
		struct root *root = tags_new(sizeof(*root));

		block_new(&root->block, 4, &checked_definition);
		CHECK(walk(root, &checked_root, &corrections), "refused: %s", harness_last_message);
		CHECK(harness_checks == 4, "%ld checks for 4 elements", harness_checks);
		CHECK(harness_entered == 1 + 1 + 4, "%ld elements entered", harness_entered);
		return 0;
	}
	/* (a check in a block's elements' block) */
	CASE("nested-checks")
	{
		struct root *root = tags_new(sizeof(*root));
		struct outer_element *outer = block_new(&root->block, 3, &outer_definition);
		long index;

		for (index = 0; index < 3; index++)
			block_new(&outer[index].inner, 2, &checked_definition);
		CHECK(walk(root, &outer_root, &corrections), "refused: %s", harness_last_message);
		CHECK(harness_checks == 6, "%ld checks for 6 elements", harness_checks);
		/* (the root and the elements with a block: those of the blocks, with
		none, are not taken through the extents' pass) */
		CHECK(harness_extents == 1 + 3, "%ld elements through the extents' pass", harness_extents);
		return 0;
	}
	/* (a check in an element's structure) */
	CASE("structure-checks")
	{
		struct root *root = tags_new(sizeof(*root));

		block_new(&root->block, 3, &holder_definition);
		CHECK(walk(root, &holder_root, &corrections), "refused: %s", harness_last_message);
		CHECK(harness_checks == 3, "%ld checks for 3 elements", harness_checks);
		return 0;
	}
	/* (elements too deep are refused, though they would do nothing) */
	CASE("too-deep")
	{
		struct root *root = tags_new(sizeof(*root));
		struct tag_block *block = &root->block;
		long level;

		for (level = 0; level < DEPTH_CHAIN_LENGTH; level++)
		{
			struct root *element = block_new(block, 1, chain_definitions[level]);

			block = &element->block;
		}
		CHECK(!walk(root, &chain_root, &corrections), "elements %d deep were not refused", DEPTH_CHAIN_LENGTH);
		CHECK(strstr(harness_last_message, "is too deep") != NULL, "refused otherwise: %s", harness_last_message);
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
