/*
TAG_VALIDATE_WALKS.C

The validator (port/linux/game/tag_validate.c), taken whole, walking made-up
schemas over tags laid out in a buffer: which blocks' elements its passes
enter, which elements it takes through the extents' pass, which elements of
enums and indices it enters when it checks their values a block at a time,
and that skipping the others changes nothing (corrections, messages, checks,
refusals).
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
static char harness_messages[8192];

void tag_validate_report(
	char const *message)
{
	size_t length = strlen(harness_messages);

	snprintf(harness_last_message, sizeof(harness_last_message), "%s", message);
	snprintf(harness_messages + length, sizeof(harness_messages) - length, "%s\n", message);
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

/* elements of enums and indices: into the root's block (none allowed), and
into their parent's (none not allowed, and an array of two with none
allowed), whose counts differ from parent to parent */
struct indexed_element { short kind; short reference; short target; short pair[2]; };
static struct tag_schema_field const indexed_fields[] = {
	TAG_SCHEMA_ENUM(struct indexed_element, kind, 3, 0),
	TAG_SCHEMA_BLOCK_INDEX(struct indexed_element, reference, TAG_SCHEMA_ROOT, 0, FLAG(_tag_schema_none_bit)),
	TAG_SCHEMA_BLOCK_INDEX(struct indexed_element, target, 1, 12, 0),
	TAG_SCHEMA_BLOCK_INDEX_ARRAY(struct indexed_element, pair, 1, 12, FLAG(_tag_schema_none_bit)),
	TAG_SCHEMA_END };
static struct tag_schema_definition const indexed_definition = TAG_SCHEMA_DEFINITION(indexed,
	struct indexed_element, indexed_fields);
struct parent_element { struct tag_block children; struct tag_block targets; };
static struct tag_schema_field const parent_fields[] = {
	TAG_SCHEMA_BLOCK(struct parent_element, children, indexed_definition, 0),
	TAG_SCHEMA_BLOCK(struct parent_element, targets, empty_definition, 0),
	TAG_SCHEMA_END };
static struct tag_schema_definition const parent_definition = TAG_SCHEMA_DEFINITION(parent, struct parent_element,
	parent_fields);
/* (the root's blocks the other way round from a parent's, so that a level
off by one reads another count) */
struct indexed_root { struct tag_block references; struct tag_block parents; };
static struct tag_schema_field const indexed_root_fields[] = {
	TAG_SCHEMA_BLOCK(struct indexed_root, references, empty_definition, 0),
	TAG_SCHEMA_BLOCK(struct indexed_root, parents, parent_definition, 0),
	TAG_SCHEMA_END };
static struct tag_schema_definition const indexed_root = TAG_SCHEMA_DEFINITION(indexed_root, struct indexed_root,
	indexed_root_fields);

/* elements with an index into a block of their own (its block not one of
their fields), whose counts differ from element to element: named by their
structure, or by their level (0) */
struct own_element { struct tag_block own; short index; short pad; };
static struct tag_schema_field const own_structure_fields[] = {
	TAG_SCHEMA_BLOCK_INDEX(struct own_element, index, TAG_SCHEMA_STRUCTURE, 0, 0), TAG_SCHEMA_END };
static struct tag_schema_definition const own_structure_definition = TAG_SCHEMA_DEFINITION(own_structure,
	struct own_element, own_structure_fields);
static struct tag_schema_field const own_level_fields[] = {
	TAG_SCHEMA_BLOCK_INDEX(struct own_element, index, 0, 0, 0), TAG_SCHEMA_END };
static struct tag_schema_definition const own_level_definition = TAG_SCHEMA_DEFINITION(own_level,
	struct own_element, own_level_fields);
struct own_root { struct tag_block structure_elements; struct tag_block level_elements; };
static struct tag_schema_field const own_root_fields[] = {
	TAG_SCHEMA_BLOCK(struct own_root, structure_elements, own_structure_definition, 0),
	TAG_SCHEMA_BLOCK(struct own_root, level_elements, own_level_definition, 0),
	TAG_SCHEMA_END };
static struct tag_schema_definition const own_root = TAG_SCHEMA_DEFINITION(own_root, struct own_root,
	own_root_fields);

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
	/* (the elements of enums and indices that have a value to correct are
entered, and only those: their parents' counts, each its own, the root's,
none where it may be and where it may not) */
	CASE("indices")
	{
		static long const target_counts[3] = { 2, 5, 0 };
		struct indexed_root *root = tags_new(sizeof(*root));
		struct parent_element *parents;
		struct indexed_element *children[3];
		long parent_index;
		long index;

		block_new(&root->references, 4, &empty_definition);
		parents = block_new(&root->parents, 3, &parent_definition);
		for (parent_index = 0; parent_index < 3; parent_index++)
		{
			long count = target_counts[parent_index];

			children[parent_index] = block_new(&parents[parent_index].children, 50, &indexed_definition);
			block_new(&parents[parent_index].targets, count, &empty_definition);
			for (index = 0; index < 50; index++)
			{
				struct indexed_element *child = &children[parent_index][index];

				child->kind = (short)(index % 3);
				child->reference = (short)(index % 5 == 4 ? NONE : index % 4);
				child->target = (short)(count ? index % count : NONE);
				child->pair[0] = (short)(count ? (index + 1) % count : NONE);
				child->pair[1] = (short)(index % 2 || !count ? NONE : index % count);
			}
		}
		children[0][7].kind = 3;
		children[0][20].target = 2;
		children[1][0].reference = 4;
		children[1][30].pair[1] = 5;
		children[1][49].target = NONE;
		children[2][10].target = 3;
		CHECK(walk(root, &indexed_root, &corrections), "refused: %s", harness_last_message);
		CHECK(corrections == 6, "%ld corrections for 6 values", corrections);
		CHECK(children[0][7].kind == 0 && children[0][20].target == 0 && children[1][0].reference == NONE &&
			children[1][30].pair[1] == NONE && children[1][49].target == 0 && children[2][10].target == NONE,
			"corrected otherwise: %d %d %d %d %d %d", children[0][7].kind, children[0][20].target,
			children[1][0].reference, children[1][30].pair[1], children[1][49].target, children[2][10].target);
		/* (the root in each walk, the parents, and the six children) */
		CHECK(harness_entered == 2 + 3 + 6, "%ld elements entered", harness_entered);
		/* (and the messages, each naming its element, as when every child is
		entered) */
		CHECK(!strcmp(harness_messages,
			"the map 'harness' is corrected: tag '?' (?): parents[0].children[7].kind is 3, past its 3 values: 0\n"
			"the map 'harness' is corrected: tag '?' (?): parents[0].children[20].target is 2, past its block's 2: 0\n"
			"the map 'harness' is corrected: tag '?' (?): parents[1].children[0].reference is 4, past its block's 4: "
				"none\n"
			"the map 'harness' is corrected: tag '?' (?): parents[1].children[30].pair is 5, past its block's 5: none\n"
			"the map 'harness' is corrected: tag '?' (?): parents[1].children[49].target is -1, past its block's 5: 0\n"
			"the map 'harness' is corrected: tag '?' (?): parents[2].children[10].target is 3, past its block's 0: "
				"none\n"), "messages:\n%s", harness_messages);
		return 0;
	}
	/* (an index into a block of its element's own is checked in each element,
whose blocks are not the same for each) */
	CASE("own-indices")
	{
		static long const own_counts[4] = { 1, 0, 3, 3 };
		struct own_root *root = tags_new(sizeof(*root));
		struct own_element *blocks[2];
		long block_index;
		long index;

		blocks[0] = block_new(&root->structure_elements, 4, &own_structure_definition);
		blocks[1] = block_new(&root->level_elements, 4, &own_level_definition);
		/* (indices of 1 into blocks of 1, 0, 3 and 3) */
		for (block_index = 0; block_index < 2; block_index++)
		{
			for (index = 0; index < 4; index++)
			{
				blocks[block_index][index].own.count = own_counts[index];
				blocks[block_index][index].own.address = NULL;
				blocks[block_index][index].index = 1;
			}
		}
		CHECK(walk(root, &own_root, &corrections), "refused: %s", harness_last_message);
		CHECK(corrections == 4, "%ld corrections for 4 indices past their blocks", corrections);
		for (block_index = 0; block_index < 2; block_index++)
		{
			struct own_element *elements = blocks[block_index];

			CHECK(elements[0].index == 0 && elements[1].index == NONE && elements[2].index == 1 &&
				elements[3].index == 1, "block %ld corrected otherwise: %d %d %d %d", block_index,
				elements[0].index, elements[1].index, elements[2].index, elements[3].index);
		}
		/* (the root in each walk, and every element) */
		CHECK(harness_entered == 2 + 8, "%ld elements entered", harness_entered);
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
		/* (in the first walk: the root and the levels above the last, once) */
		CHECK(harness_entered == DEPTH_CHAIN_LENGTH, "%ld elements entered", harness_entered);
		return 0;
	}

	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
