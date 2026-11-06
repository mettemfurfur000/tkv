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
