#include "tkv.h"

#include <temlib/data_io.h>
#include <temlib/error.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                                                               \
	do                                                                                                                 \
	{                                                                                                                  \
		if (!(condition))                                                                                              \
		{                                                                                                              \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);                              \
			return 0;                                                                                                  \
		}                                                                                                              \
	} while (0)

static int nearly_equal(f64 actual, f64 expected)
{
	return fabs(actual - expected) < 0.000001;
}

static int check_fixture(tkv_object object)
{
	tkv_value value;
	tkv_array array;
	vec3 position;
	quaternion orientation;

	CHECK(object != NULL);
	value = tkv_get_value(object, "enabled");
	CHECK(value.meta.tkv_value_type == TKV_VALUE_BOOL);
	CHECK(tkv_value_to_bool(value));

	CHECK(tkv_value_to_i8(tkv_get_value(object, "small")) == -12);
	CHECK(tkv_value_to_i16(tkv_get_value(object, "medium")) == -1234);
	CHECK(tkv_value_to_i32(tkv_get_value(object, "count")) == -123456);
	CHECK(tkv_value_to_i64(tkv_get_value(object, "big")) == -123456789012LL);
	CHECK(tkv_value_to_u8(tkv_get_value(object, "byte")) == 250);
	CHECK(tkv_value_to_u16(tkv_get_value(object, "word")) == 60000);
	CHECK(tkv_value_to_u32(tkv_get_value(object, "dword")) == 1234567890U);
	CHECK(tkv_value_to_u64(tkv_get_value(object, "qword")) == 1234567890123ULL);
	CHECK(nearly_equal(tkv_value_to_f32(tkv_get_value(object, "ratio")), 1.25));
	CHECK(nearly_equal(tkv_value_to_f64(tkv_get_value(object, "precise")), 3.141592653589793));
	CHECK(strcmp(tkv_value_to_str(tkv_get_value(object, "name")), "goblin") == 0);

	array = tkv_value_to_arr(tkv_get_value(object, "data"));
	CHECK(array.element_size == 1);
	CHECK(array.array_length == 4);
	CHECK(array.bytes[0] == 0 && array.bytes[1] == 1 && array.bytes[2] == 127 && array.bytes[3] == 255);

	position = tkv_value_to_vec3(tkv_get_value(object, "position"));
	CHECK(nearly_equal(position.x, -1.5));
	CHECK(nearly_equal(position.y, 2.0));
	CHECK(nearly_equal(position.z, 3.25));

	orientation = tkv_value_to_quat(tkv_get_value(object, "orient"));
	CHECK(nearly_equal(orientation.x, 0.0));
	CHECK(nearly_equal(orientation.y, 0.5));
	CHECK(nearly_equal(orientation.z, -0.5));
	CHECK(nearly_equal(orientation.w, 1.0));

	value = tkv_traverse_get_value(object, "child.score");
	CHECK(value.meta.tkv_value_type == TKV_VALUE_I32);
	CHECK(tkv_value_to_i32(value) == 42);

	return 1;
}

static int test_keys(void)
{
	const char *key_text = "A0_bZ9xY2q";
	tkv_key key = tkv_make_key(key_text);
	char decoded[TKV_KEY_LEN_MAX + 1] = {0};

	CHECK(key.whole != 0);
	CHECK(key.size == strlen(key_text));
	tkv_key_to_str(key, decoded);
	decoded[key.size] = '\0';
	CHECK(strcmp(decoded, key_text) == 0);
	CHECK(tkv_make_key("").whole == 0);
	CHECK(tkv_make_key("key-too-long").whole == 0);
	CHECK(tkv_make_key("bad-key").whole == 0);
	return 1;
}

static int test_text_parse_and_serialize(void)
{
	static const char source_text[] =
		"{"
		" bool enabled = true;"
		" i8 small = -12;"
		" i16 medium = -1234;"
		" i32 count = -123456;"
		" i64 big = -123456789012;"
		" u8 byte = 250;"
		" u16 word = 60000;"
		" u32 dword = 1234567890;"
		" u64 qword = 1234567890123;"
		" f32 ratio = 1.25;"
		" f64 precise = 3.141592653589793;"
		" str name = \"goblin\";"
		" arr data = [ 0 1 127 255 ];"
		" vec3 position = (-1.5, 2, 3.25);"
		" quat orient = (0, 0.5, -0.5, 1);"
		" tkv child = { i32 score = 42; };"
		"}";
	arena *object_arena = arena_create(65536);
	char *serialized;
	const char *source = source_text;
	const char *serialized_source;
	tkv_object original;
	tkv_object reparsed;
	int original_ok;
	int reparsed_ok;

	CHECK(object_arena != NULL);
	original = tkv_parse_object_once(&source, object_arena);
	CHECK(original != NULL);
	original_ok = check_fixture(original);

	serialized = tkv_serialize_object(original, object_arena);
	CHECK(serialized != NULL);
	serialized_source = serialized;
	reparsed = tkv_parse_object_once(&serialized_source, object_arena);
	CHECK(reparsed != NULL);
	reparsed_ok = check_fixture(reparsed);

	arena_destroy(object_arena);
	return original_ok && reparsed_ok;
}

static int test_stream_round_trip(void)
{
	static const char source_text[] =
		"{ bool enabled = false; i32 count = 17; str name = \"stream\";"
		" arr data = [ 2 4 8 ]; tkv child = { f64 value = 9.5; }; }";
	arena *object_arena = arena_create(65536);
	const char *source = source_text;
	tkv_object original;
	tkv_object restored;
	stream_t stream = {0};

	CHECK(object_arena != NULL);
	original = tkv_parse_object_once(&source, object_arena);
	CHECK(original != NULL);

	stream.mode = STREAM_BUF;
	vec_init(&stream.handle.raw.bytes);
	CHECK(tkv_write_to_stream(original, &stream) == SUCCESS);

	stream.handle.raw.read_idx = 0;
	restored = tkv_read_from_stream_once(&stream, object_arena);
	CHECK(restored != NULL);
	CHECK(tkv_value_to_bool(tkv_get_value(restored, "enabled")) == false);
	CHECK(tkv_value_to_i32(tkv_get_value(restored, "count")) == 17);
	CHECK(strcmp(tkv_value_to_str(tkv_get_value(restored, "name")), "stream") == 0);
	CHECK(tkv_value_to_arr(tkv_get_value(restored, "data")).array_length == 3);
	CHECK(nearly_equal(tkv_value_to_f64(tkv_traverse_get_value(restored, "child.value")), 9.5));

	vec_deinit(&stream.handle.raw.bytes);
	arena_destroy(object_arena);
	return 1;
}

static int test_negative_float_parsing(void)
{
	static const char source_text[] = "{ f32 single = -1.25; f64 double = -3.5; }";
	const char *source = source_text;
	arena *object_arena = arena_create(4096);
	tkv_object object;
	int passed = 1;

	CHECK(object_arena != NULL);
	object = tkv_parse_object_once(&source, object_arena);
	CHECK(object != NULL);

	if (!nearly_equal(tkv_value_to_f32(tkv_get_value(object, "single")), -1.25))
	{
		fprintf(stderr, "%s:%d: negative f32 value was not preserved\n", __FILE__, __LINE__);
		passed = 0;
	}
	if (!nearly_equal(tkv_value_to_f64(tkv_get_value(object, "double")), -3.5))
	{
		fprintf(stderr, "%s:%d: negative f64 value was not preserved\n", __FILE__, __LINE__);
		passed = 0;
	}

	arena_destroy(object_arena);
	return passed;
}

static int test_add_field_and_setter(void)
{
	arena *object_arena = arena_create(4096);
	tkv_object empty;
	tkv_object extended;
	i32 original_value = 10;

	CHECK(object_arena != NULL);
	empty = tkv_object_create_empty(object_arena);
	CHECK(empty != NULL);
	extended = tkv_object_add_field(empty, "value", TKV_VALUE_I32, TKV_STATE_VOLATILE, &original_value,
									object_arena);
	CHECK(extended != NULL);
	CHECK(extended != empty);
	tkv_value_set_i32(tkv_get_value(extended, "value"), 25);
	CHECK(tkv_value_to_i32(tkv_get_value(extended, "value")) == 25);

	arena_destroy(object_arena);
	return 1;
}

static int test_invalid_input(void)
{
	static const char source_text[] = "not-a-tree";
	const char *source = source_text;
	arena *object_arena = arena_create(4096);

	CHECK(object_arena != NULL);
	CHECK(tkv_parse_object_once(&source, object_arena) == NULL);
	arena_destroy(object_arena);
	return 1;
}

static int test_parse_errors_are_collected_and_reset(void)
{
	static const char invalid_text[] = "{ vec3 position = (1, invalid, 3); }";
	static const char missing_semicolon_text[] = "{ i32 value = 1 }";
	static const char valid_text[] = "{}";
	const char *source = invalid_text;
	arena *object_arena = arena_create(4096);
	tkv_object object;

	CHECK(object_arena != NULL);
	error_clear();

	object = tkv_parse_object_once(&source, object_arena);
	CHECK(object == NULL);
	CHECK(error_amount() == 1);
	CHECK(strcmp(error_cache[0].function_name, "parse_float_component") == 0);

	source = missing_semicolon_text;
	object = tkv_parse_object_once(&source, object_arena);
	CHECK(object == NULL);
	CHECK(error_amount() == 2);
	CHECK(strcmp(error_cache[0].function_name, "tkv_parse_object") == 0);
	CHECK(strcmp(error_cache[1].function_name, "tkv_parse_object") == 0);

	source = valid_text;
	object = tkv_parse_object_once(&source, object_arena);
	CHECK(object != NULL);
	CHECK(error_amount() == 0);

	arena_destroy(object_arena);
	return 1;
}

static int test_unterminated_object(void)
{
	static const char source_text[] = "{ i32 value = 1;";
	const char *source = source_text;
	arena *object_arena = arena_create(4096);
	tkv_object object;

	CHECK(object_arena != NULL);
	object = tkv_parse_object_once(&source, object_arena);
	arena_destroy(object_arena);
	CHECK(object == NULL);
	return 1;
}

static int test_scratch_arena_restored_after_error(void)
{
	static const char source_text[] = "{ i32 value = invalid; }";
	const char *source = source_text;
	arena *scratch_arena = arena_create(4096);
	arena *object_arena = arena_create(4096);
	u32 initial_length;
	u32 initial_object_length;
	tkv_object object;
	int passed;

	CHECK(scratch_arena != NULL);
	CHECK(object_arena != NULL);
	initial_length = scratch_arena->length;
	initial_object_length = object_arena->length;
	object = tkv_parse_object(&source, scratch_arena, object_arena);
	passed = object == NULL && scratch_arena->length == initial_length &&
			 object_arena->length == initial_object_length;

	arena_destroy(scratch_arena);
	arena_destroy(object_arena);
	CHECK(passed);
	return 1;
}

static int test_malformed_vectors_restore_arenas(void)
{
	static const char *source_texts[] = {
		"{ vec3 position = (1, invalid, 3); }",
		"{ quat orient = (1, 2, 3); }",
	};

	for (size_t i = 0; i < sizeof(source_texts) / sizeof(source_texts[0]); i++)
	{
		const char *source = source_texts[i];
		arena *scratch_arena = arena_create(4096);
		arena *object_arena = arena_create(4096);
		u32 initial_scratch_length;
		u32 initial_object_length;

		CHECK(scratch_arena != NULL);
		CHECK(object_arena != NULL);
		initial_scratch_length = scratch_arena->length;
		initial_object_length = object_arena->length;

		tkv_object object = tkv_parse_object(&source, scratch_arena, object_arena);
		int restored = object == NULL && scratch_arena->length == initial_scratch_length &&
					   object_arena->length == initial_object_length;

		arena_destroy(scratch_arena);
		arena_destroy(object_arena);
		CHECK(restored);
	}

	return 1;
}

static int test_malformed_nested_object_restores_arenas(void)
{
	static const char source_text[] = "{ tkv child = { i32 value = 1;";
	const char *source = source_text;
	arena *scratch_arena = arena_create(4096);
	arena *object_arena = arena_create(4096);
	u32 initial_scratch_length;
	u32 initial_object_length;
	tkv_object object;
	int passed;

	CHECK(scratch_arena != NULL);
	CHECK(object_arena != NULL);
	initial_scratch_length = scratch_arena->length;
	initial_object_length = object_arena->length;

	object = tkv_parse_object(&source, scratch_arena, object_arena);
	passed = object == NULL && scratch_arena->length == initial_scratch_length &&
			 object_arena->length == initial_object_length;

	arena_destroy(scratch_arena);
	arena_destroy(object_arena);
	CHECK(passed);
	return 1;
}

int main(void)
{
	static const struct
	{
		const char *name;
		int (*run)(void);
	} tests[] = {
		{"keys", test_keys},
		{"text parse and serialize", test_text_parse_and_serialize},
		{"stream round trip", test_stream_round_trip},
		{"negative float parsing", test_negative_float_parsing},
		{"add field and setter", test_add_field_and_setter},
		{"invalid input", test_invalid_input},
		{"parse errors are collected and reset", test_parse_errors_are_collected_and_reset},
		{"unterminated object", test_unterminated_object},
		{"scratch arena restored after error", test_scratch_arena_restored_after_error},
		{"malformed vectors restore arenas", test_malformed_vectors_restore_arenas},
		{"malformed nested object restores arenas", test_malformed_nested_object_restores_arenas},
	};
	int failures = 0;

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
	{
		if (!tests[i].run())
		{
			fprintf(stderr, "FAIL: %s\n", tests[i].name);
			failures++;
		}
		else
		{
			printf("PASS: %s\n", tests[i].name);
		}
	}

	printf("%zu tests, %d failed\n", sizeof(tests) / sizeof(tests[0]), failures);
	return failures ? 1 : 0;
}
