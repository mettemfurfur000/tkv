#include "tkv.h"

#include <temlib/data_io.h>

#include <cjson/cJSON.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} text_buffer;

#define MAX_FIELDS 256U

static int append_format(text_buffer *buffer, const char *format, ...)
{
    va_list args;
    va_list copy;
    int formatted_length;
    size_t required;

    va_start(args, format);
    va_copy(copy, args);
    formatted_length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (formatted_length < 0)
    {
        va_end(args);
        return 0;
    }

    required = buffer->length + (size_t)formatted_length + 1;
    if (required > buffer->capacity)
    {
        size_t capacity = buffer->capacity ? buffer->capacity : 256;
        char *data;

        while (capacity < required)
            capacity *= 2;
        data = (char *)realloc(buffer->data, capacity);
        if (!data)
        {
            va_end(args);
            return 0;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }

    vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, format, args);
    va_end(args);
    buffer->length += (size_t)formatted_length;
    return 1;
}

static u32 random_next(u32 *state)
{
    u32 value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static int generate_fixtures(u32 fields, u32 seed, text_buffer *tkv, text_buffer *json)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    if (!append_format(tkv, "{") || !append_format(json, "{"))
        return 0;

    for (u32 i = 0; i < fields; i++)
    {
        char key[10];
        u32 value = random_next(&seed);

        (void)snprintf(key, sizeof(key), "f%04u", i);
        if (i && (!append_format(tkv, " ") || !append_format(json, ",")))
            return 0;

        switch (i % 5)
        {
        case 0:
        {
            const char *boolean = (value & 1) ? "true" : "false";
            if (!append_format(tkv, "bool %s = %s;", key, boolean) ||
                !append_format(json, "\"%s\":%s", key, boolean))
                return 0;
            break;
        }
        case 1:
        {
            i32 integer = (i32)(value % 2000001U) - 1000000;
            if (!append_format(tkv, "i32 %s = %d;", key, integer) ||
                !append_format(json, "\"%s\":%d", key, integer))
                return 0;
            break;
        }
        case 2:
        {
            f64 number = (f64)(value % 100000000U) / 10000.0;
            if (!append_format(tkv, "f64 %s = %.4f;", key, number) ||
                !append_format(json, "\"%s\":%.4f", key, number))
                return 0;
            break;
        }
        case 3:
        {
            char string[21];
            u32 length = 8 + random_next(&seed) % 13;
            for (u32 j = 0; j < length; j++)
                string[j] = alphabet[random_next(&seed) % (sizeof(alphabet) - 1)];
            string[length] = '\0';
            if (!append_format(tkv, "str %s = \"%s\";", key, string) ||
                !append_format(json, "\"%s\":\"%s\"", key, string))
                return 0;
            break;
        }
        default:
        {
            u32 bytes[4];
            for (u32 j = 0; j < 4; j++)
                bytes[j] = random_next(&seed) & 0xff;
            if (!append_format(tkv, "arr %s = [ %u %u %u %u ];", key, bytes[0], bytes[1], bytes[2], bytes[3]) ||
                !append_format(json, "\"%s\":[%u,%u,%u,%u]", key, bytes[0], bytes[1], bytes[2], bytes[3]))
                return 0;
            break;
        }
        }
    }

    return append_format(tkv, "}") && append_format(json, "}");
}

static double elapsed_seconds(clock_t start, clock_t finish)
{
    double elapsed = (double)(finish - start) / CLOCKS_PER_SEC;
    return elapsed > 0.0 ? elapsed : 1.0 / CLOCKS_PER_SEC;
}

static int parse_argument(const char *text, u32 maximum, u32 *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > maximum)
        return 0;

    *value = (u32)parsed;
    return 1;
}

static int run_benchmark(u32 iterations, u32 fields, u32 seed)
{
    arena *tkv_arena = arena_create(4096);
    arena *parse_arena = arena_create(4096);
    arena *scratch_arena = arena_create(4096);
    arena *output_arena = arena_create(4096);
    arena *stream_arena = arena_create(4096);
    stream_t binary = {0};
    text_buffer tkv_fixture = {0};
    text_buffer json_fixture = {0};
    const char *source;
    tkv_object object;
    cJSON *json = NULL;
    char *json_output = NULL;
    char *tkv_output;
    size_t json_size;
    size_t tkv_text_size;
    size_t tkv_binary_size;
    clock_t start;
    clock_t finish;
    double json_parse_seconds;
    double json_serialize_seconds;
    double tkv_text_parse_seconds;
    double tkv_text_serialize_seconds;
    double tkv_binary_read_seconds;
    double tkv_binary_write_seconds;

    if (!tkv_arena || !parse_arena || !scratch_arena || !output_arena || !stream_arena)
    {
        fprintf(stderr, "benchmark: failed to create arenas\n");
        if (tkv_arena)
            arena_destroy(tkv_arena);
        if (parse_arena)
            arena_destroy(parse_arena);
        if (scratch_arena)
            arena_destroy(scratch_arena);
        if (output_arena)
            arena_destroy(output_arena);
        if (stream_arena)
            arena_destroy(stream_arena);
        return 0;
    }

    binary.mode = STREAM_BUF;
    vec_init(&binary.handle.raw.bytes);

    if (!generate_fixtures(fields, seed, &tkv_fixture, &json_fixture))
    {
        fprintf(stderr, "benchmark: failed to generate fixtures\n");
        goto fail;
    }

    source = tkv_fixture.data;
    object = tkv_parse_object(&source, scratch_arena, tkv_arena);
    json = cJSON_Parse(json_fixture.data);
    if (!object || !json)
    {
        fprintf(stderr, "benchmark: failed to parse benchmark fixture\n");
        goto fail;
    }

    tkv_output = tkv_serialize_object(object, output_arena);
    json_output = cJSON_PrintUnformatted(json);
    if (!tkv_output || !json_output || tkv_write_to_stream(object, &binary) != SUCCESS)
    {
        fprintf(stderr, "benchmark: failed to serialize benchmark fixture\n");
        free(json_output);
        json_output = NULL;
        goto fail;
    }

    tkv_text_size = strlen(tkv_output);
    json_size = strlen(json_output);
    tkv_binary_size = (size_t)binary.handle.raw.bytes.length;
    free(json_output);
    json_output = NULL;

    if (tkv_text_size >= 31999 || cJSON_GetArraySize(json) != (int)fields)
    {
        fprintf(stderr, "benchmark: generated fixture exceeds TKV text limit or field counts differ\n");
        goto fail;
    }

    start = clock();
    for (u32 i = 0; i < iterations; i++)
    {
        cJSON *parsed = cJSON_Parse(json_fixture.data);
        if (!parsed)
        {
            fprintf(stderr, "benchmark: JSON parse failed\n");
            goto fail;
        }
        cJSON_Delete(parsed);
    }
    finish = clock();
    json_parse_seconds = elapsed_seconds(start, finish);

    start = clock();
    for (u32 i = 0; i < iterations; i++)
    {
        char *serialized = cJSON_PrintUnformatted(json);
        if (!serialized)
        {
            fprintf(stderr, "benchmark: JSON serialization failed\n");
            goto fail;
        }
        free(serialized);
    }
    finish = clock();
    json_serialize_seconds = elapsed_seconds(start, finish);

    start = clock();
    for (u32 i = 0; i < iterations; i++)
    {
        const char *text = tkv_fixture.data;
        tkv_object parsed = tkv_parse_object(&text, scratch_arena, parse_arena);
        if (!parsed)
        {
            fprintf(stderr, "benchmark: TKV text parse failed\n");
            goto fail;
        }
        parse_arena->length = 0;
    }
    finish = clock();
    tkv_text_parse_seconds = elapsed_seconds(start, finish);

    start = clock();
    for (u32 i = 0; i < iterations; i++)
    {
        output_arena->length = 0;
        if (!tkv_serialize_object(object, output_arena))
        {
            fprintf(stderr, "benchmark: TKV text serialization failed\n");
            goto fail;
        }
    }
    finish = clock();
    tkv_text_serialize_seconds = elapsed_seconds(start, finish);

    start = clock();
    for (u32 i = 0; i < iterations; i++)
    {
        binary.handle.raw.read_idx = 0;
        tkv_object parsed = tkv_read_from_stream(&binary, scratch_arena, stream_arena);
        if (!parsed)
        {
            fprintf(stderr, "benchmark: TKV binary read failed\n");
            goto fail;
        }
        stream_arena->length = 0;
    }
    finish = clock();
    tkv_binary_read_seconds = elapsed_seconds(start, finish);

    start = clock();
    for (u32 i = 0; i < iterations; i++)
    {
        binary.handle.raw.bytes.length = 0;
        if (tkv_write_to_stream(object, &binary) != SUCCESS)
        {
            fprintf(stderr, "benchmark: TKV binary write failed\n");
            goto fail;
        }
    }
    finish = clock();
    tkv_binary_write_seconds = elapsed_seconds(start, finish);

    printf("Iterations: %u\n", iterations);
    printf("Generated fields: %u; seed: %u\n", fields, seed);
    printf("JSON library: cJSON %d.%d.%d (compact text)\n", CJSON_VERSION_MAJOR, CJSON_VERSION_MINOR,
           CJSON_VERSION_PATCH);
    printf("Timing: clock() CPU time; TKV arenas are reset between operations\n");
    printf("\n");
    printf("%-20s %12s %18s %20s\n", "Format", "Size (bytes)", "Parse/read ops/s", "Serialize/write ops/s");
    printf("%-20s %12zu %18.0f %20.0f\n", "JSON compact", json_size, iterations / json_parse_seconds,
           iterations / json_serialize_seconds);
    printf("%-20s %12zu %18.0f %20.0f\n", "TKV text", tkv_text_size, iterations / tkv_text_parse_seconds,
           iterations / tkv_text_serialize_seconds);
    printf("%-20s %12zu %18.0f %20.0f\n", "TKV binary stream", tkv_binary_size, iterations / tkv_binary_read_seconds,
           iterations / tkv_binary_write_seconds);

    cJSON_Delete(json);
    vec_deinit(&binary.handle.raw.bytes);
    free(tkv_fixture.data);
    free(json_fixture.data);
    arena_destroy(tkv_arena);
    arena_destroy(parse_arena);
    arena_destroy(scratch_arena);
    arena_destroy(output_arena);
    arena_destroy(stream_arena);
    return 1;

fail:
    free(json_output);
    cJSON_Delete(json);
    vec_deinit(&binary.handle.raw.bytes);
    free(tkv_fixture.data);
    free(json_fixture.data);
    arena_destroy(tkv_arena);
    arena_destroy(parse_arena);
    arena_destroy(scratch_arena);
    arena_destroy(output_arena);
    arena_destroy(stream_arena);
    return 0;
}

int main(int argc, char **argv)
{
    u32 iterations = 10000;
    u32 fields = 128;
    u32 seed = 1;

    if (argc > 4 || (argc > 1 && !parse_argument(argv[1], 100000000U, &iterations)) || iterations == 0 ||
        (argc > 2 && (!parse_argument(argv[2], MAX_FIELDS, &fields) || fields == 0)) ||
        (argc > 3 && !parse_argument(argv[3], 0xffffffffU, &seed)))
    {
        fprintf(stderr, "Usage: %s [iterations (1-100000000) [fields (1-%u) [seed (0-4294967295)]]]\n", argv[0],
                MAX_FIELDS);
        return EXIT_FAILURE;
    }

    if (seed == 0)
        seed = 0xA341316CU;

    return run_benchmark(iterations, fields, seed) ? EXIT_SUCCESS : EXIT_FAILURE;
}
