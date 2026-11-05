# TKV

TKV is a typed, hierarchical key-value format with text parsing, compact binary
storage, nested objects, and stream I/O.

## Build

```sh
make -C /path/to/temlib install
make
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

Consumers need both TKV and temlib headers and libraries. For example:

```c
#include <tkv/tkv.h>

const char *source = "{ i64 hp = 100; str name = \"goblin\"; }";
tkv_object object = tkv_parse_object(&source, scratch_arena, value_arena);
i64 hp = tkv_value_to_i64(tkv_get_value(object, "hp"));
```
