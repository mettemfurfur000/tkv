# TKV

TKV is a typed, hierarchical key-value format with text parsing, compact binary
storage, nested objects, and stream I/O.

## Build

```sh
make -C /path/to/temlib install
make
make test
make install
```

This builds `build/libtkv.a`. TKV uses temlib's common types, arenas, tokenizer,
and stream utilities from its installed headers. By default, TKV looks for
temlib under the same prefix as TKV (`/usr/local`). Override `TEMLIB_PREFIX`
when temlib is installed elsewhere:

```sh
make -C /path/to/temlib install PREFIX=/path/to/temlib/prefix
make TEMLIB_PREFIX=/path/to/temlib/prefix
```

## Source layout

- `src/tkv.c` contains key encoding, value accessors, and shared value helpers.
- `src/tkv_parse.c` parses text into TKV objects.
- `src/tkv_serialize.c` serializes objects to text.
- `src/tkv_object.c` creates objects and adds fields.
- `src/tkv_stream.c` reads and writes stream data.
- `src/tkv_internal.h` contains declarations shared only by implementation files.

Consumers need both TKV and temlib headers and libraries. For example:

```c
#include <tkv/tkv.h>
#include <temlib/error.h>

const char *source = "{ i64 hp = 100; str name = \"goblin\"; }";
tkv_object object = tkv_parse_object(&source, scratch_arena, value_arena);
i64 hp = tkv_value_to_i64(tkv_get_value(object, "hp"));
```

## Error handling

Text parsing returns `NULL` on invalid input and records details in temlib's
shared error cache. Check the cache immediately after a failed parse, before
starting another parse: `tkv_parse_object` clears it at the beginning of each
call. Each entry contains the function name and message, and
`error_amount()` gives the number of entries.

```c
#include <stdio.h>

tkv_object object = tkv_parse_object(&source, scratch_arena, value_arena);
if (object == NULL)
{
    for (u32 i = 0; i < error_amount(); i++)
        fprintf(stderr, "%s reported a parse error\n", error_cache[i].function_name);
}
```

Call `error_clear()` when you want to discard the current errors explicitly.
Parsing errors are shared through temlib's global cache; copy or handle them
before the next parse if they need to be retained.

## Benchmark: TKV and JSON

Run the benchmark from the repository root with cJSON and temlib installed.
By default it generates a deterministic, 128-field configuration with mixed
booleans, integers, floats, strings, and byte arrays:

```sh
make benchmark
make benchmark BENCH_ITERATIONS=10000 BENCH_FIELDS=256 BENCH_SEED=42
```

The same generated values are encoded as TKV and JSON. The benchmark reports
encoded byte size and operations per second for parsing/reading and
serializing/writing through TKV's text format, TKV's binary stream format, and
compact JSON using cJSON. Set `BENCH_ITERATIONS`, `BENCH_FIELDS` (1-256), and
`BENCH_SEED` to change the workload. The seed makes generated configurations
reproducible. The executable also accepts positional arguments:
`build/bench_tkv [iterations [fields [seed]]]`.

Results depend on your compiler, build flags, machine, and cJSON version. The
TKV text serializer emits formatted text while JSON is compact; binary-stream
size excludes compression and does not include any transport framing. Use the
results as a workload-specific comparison, not a universal ranking. Parsing
and serialization include each library's ordinary allocation strategy (TKV
arenas versus cJSON allocations), so they are not allocator-isolated timings.

| Area | TKV | JSON |
| --- | --- | --- |
| Measured size and speed | Text and compact binary stream; benchmark results are machine-specific | Compact text; benchmark results use cJSON |
| Type information | Explicit integer widths, float widths, byte arrays, `vec3`, `quat`, and per-field state | Objects, arrays, strings, numbers, booleans, and `null`; richer types need application conventions |
| Key constraints | 1-10 ASCII alphanumeric/underscore characters | Arbitrary strings |
| Interoperability | Custom parser and stream format | Broad language and tooling support |
