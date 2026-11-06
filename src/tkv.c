#include "tkv_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

bool util_is_valid_char(char val)
{
    return (val >= 'a' && val <= 'z') || // lowercase letter
           (val >= 'A' && val <= 'Z') || // uppercase letter
           (val >= '0' && val <= '9') || // digit
           (val == '_') ||               // underscore
           (val == 0);                   // null terminator
}

/*==============================================================================
  CHARACTER COMPRESSION

  Compresses a single ASCII character to 6-bit value for key storage.
  Maps characters as follows:
    0      -> '\0' (end-of-string marker)
    1-26   -> 'a'-'z' (lowercase letters)
    27-52  -> 'A'-'Z' (uppercase letters)
    53-62  -> '0'-'9' (digits)
    63     -> '_' (underscore)
    -1     -> unknown character (error)
===============================================================================*/

i8 util_compress_char(char val)
{
    const u8 alpha_count = ('z' - 'a') + 1;

    if (val == 0)
        return 0; // null terminator

    if (val >= 'a' && val <= 'z')
        return 1 + val - 'a'; // 1-26 for lowercase

    if (val >= 'A' && val <= 'Z')
        return 1 + alpha_count + val - 'A'; // 27-52 for uppercase

    if (val >= '0' && val <= '9')
        return 1 + 2 * alpha_count + val - '0'; // 53-62 for digits

    if (val == '_')
        return 63; // 63 for underscore

    return -1;
}

/*==============================================================================
  CHARACTER DECOMPRESSION

  Reverses util_compress_char - converts 6-bit value back to ASCII character.
  Inverse mapping of the compression function above.
===============================================================================*/

char util_decompress_char(i8 compressed_val)
{
    if (compressed_val == 0)
        return '\0'; // null terminator

    // 1-26 map to 'a'-'z'
    if (compressed_val >= 1 && compressed_val <= 26)
        return 'a' + compressed_val - 1;

    // 27-52 map to 'A'-'Z'
    if (compressed_val >= 27 && compressed_val <= 52)
        return 'A' + compressed_val - 27;

    // 53-62 map to '0'-'9'
    if (compressed_val >= 53 && compressed_val <= 62)
        return '0' + compressed_val - 53;

    // 63 maps to '_'
    if (compressed_val == 63)
        return '_';

    assert(0 && "Invalid compressed character value (0-63 expected)");
    return -1;
}

// Validate that a string is a valid TKV variable name
bool tkv_is_valid_key(const char *input)
{
    u64 len = strlen(input);

    if (len > TKV_KEY_LEN_MAX || len == 0)
        return false;

    for (u32 i = 0; i < len; i++)
        if (!util_is_valid_char(input[i]))
            return false;

    return true;
}

/*==============================================================================
  KEY ENCODING

  Encodes a string into a compressed tkv_key structure using 6-bit encoding.
  Each character is compressed to 6 bits, and up to 10 characters fit in 60 bits.
  The 4-bit size field stores the actual string length.

  Returns TKV_INVALID_KEY if the string is invalid (empty, too long, or
  contains invalid characters).
===============================================================================*/
tkv_key tkv_make_key(const char *input)
{
    assert(input);

    u64 len = strlen(input);

    if (len > TKV_KEY_LEN_MAX || len == 0)
        return TKV_INVALID_KEY;

    u64 compressed_payload = 0;

    // Compress each character and pack into 6-bit fields
    for (u64 i = 0; i < len; i++)
    {
        i8 compressed_char = util_compress_char(input[i]);
        if (compressed_char < 0)
            return TKV_INVALID_KEY;
        compressed_payload = (compressed_payload << 6) | compressed_char;
    }

    return (tkv_key){.payload = compressed_payload, .size = len};
}

/*==============================================================================
  KEY DECODING

  Reverses tkv_make_key - converts a compressed tkv_key back to a string.
  Extracts the size from the key's size field, then decompresses each 6-bit
  character and writes them in reverse order (since the payload was left-shifted
  during compression).

  NOTE: The caller must ensure 'out' has at least TKV_KEY_LEN_MAX + 1 bytes.
===============================================================================*/
void tkv_key_to_str(const tkv_key key, char *out)
{
    u64 compressed_payload = key.payload;
    const u8 key_length = key.size;
    const u8 last_index = key_length - 1;

    // Characters were left-shifted during encoding, so extract in reverse
    for (u64 i = 0; i < key_length; i++)
    {
        u8 compressed_char = compressed_payload & 0x3f; // Extract lowest 6 bits
        out[last_index - i] = util_decompress_char(compressed_char);
        compressed_payload >>= 6; // Shift to next character
    }
}

/*==============================================================================
  TKV OBJECT ACCESSORS

  These functions extract header information and elements from a serialized
  TKV object in binary format. The layout is:

  [Header: u16 length, u32 size]
  [Keys: tkv_key[length]]
  [Metadata: tkv_value_meta[length]]
  [Values: u8[...]]
===============================================================================*/

u16 tkv_object_get_length(tkv_object object)
{
    return *(u16 *)(object + 0);
}

u32 tkv_object_get_size(tkv_object object)
{
    return *(u32 *)(object + sizeof(u16));
}

tkv_key tkv_object_get_key(tkv_object object, u16 index)
{
    tkv_key *keys = (tkv_key *)(object + sizeof(u16) + sizeof(u32));
    return keys[index];
}

tkv_value tkv_get_value(tkv_object object, const char *key_str)
{
    tkv_key key = tkv_make_key(key_str);
    tkv_value result = {};

    if (!key.whole)
        return result;

    u16 keys_total = tkv_object_get_length(object);

    tkv_value_meta meta = {};
    i32 meta_index = -1;

    for (u16 i = 0; i < keys_total; i++)
    {
        tkv_key k = tkv_object_get_key(object, i);

        if (k.size != key.size)
            continue;

        if (k.payload != key.payload)
            continue;

        meta_index = i;
        break;
    }

    if (meta_index == -1)
        return (tkv_value){.meta.whole = UINT_MAX}; // Key not found

    tkv_value_meta *metas = (tkv_value_meta *)(object + sizeof(u16) + sizeof(u32) + sizeof(tkv_key) * keys_total);
    meta = metas[meta_index];

    result.meta = meta;
    result.tkv_meta_index = meta_index;
    result.ptr = (object + meta.tkv_value_offset);

    return result;
}

// This is a helper for getting nested values with a single function call, without manually traversing each level.
// It will be useful for the network sync system, where we want to get a value from a nested TKV with a single path
// string.
tkv_value tkv_traverse_get_value(tkv_object object, const char *path)
{
    tkv_value result = {};

    char path_copy[256];

    assert(strlen(path) < sizeof(path_copy)); // just to be safe, we can remove this later if needed

    strncpy(path_copy, path, sizeof(path_copy));
    path_copy[sizeof(path_copy) - 1] = '\0';

    char *token = strtok(path_copy, ".");
    tkv_object current_object = object;

    while (token)
    {
        result = tkv_get_value(current_object, token);
        if (result.meta.whole == UINT_MAX)
            return (tkv_value){.meta.whole = UINT_MAX}; // Key not found

        if (result.meta.tkv_value_type != TKV_VALUE_TKV)
            return result; // Found the value

        current_object = tkv_value_to_tkv(result);
        token = strtok(NULL, ".");
    }

    return result;
}

tkv_object tkv_value_get_root(tkv_value value)
{
    return value.ptr - value.meta.tkv_value_offset;
}

#define IMPL_TKV_TO_TYPE(type, enum_type)                                                                              \
    type tkv_value_to_##type(tkv_value value)                                                                          \
    {                                                                                                                  \
        assert(value.meta.tkv_value_type == TKV_VALUE_##enum_type);                                                    \
        return *(type *)value.ptr;                                                                                     \
    }

IMPL_TKV_TO_TYPE(bool, BOOL)

IMPL_TKV_TO_TYPE(i8, I8)
IMPL_TKV_TO_TYPE(i16, I16)
IMPL_TKV_TO_TYPE(i32, I32)
IMPL_TKV_TO_TYPE(i64, I64)

IMPL_TKV_TO_TYPE(u8, U8)
IMPL_TKV_TO_TYPE(u16, U16)
IMPL_TKV_TO_TYPE(u32, U32)
IMPL_TKV_TO_TYPE(u64, U64)

IMPL_TKV_TO_TYPE(f32, F32)
IMPL_TKV_TO_TYPE(f64, F64)

char *tkv_value_to_str(tkv_value value)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_STR);
    return value.ptr;
}

tkv_array tkv_value_to_arr(tkv_value value)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_ARR);
    return (tkv_array){
        .element_size = *((u16 *)(value.ptr + 0)),           //
        .array_length = *((u16 *)(value.ptr + sizeof(u16))), //
        .bytes = value.ptr + 2 * sizeof(u16)                 //
    };
}

vec3 tkv_value_to_vec3(tkv_value value)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_VEC3);
    return *(vec3 *)value.ptr;
}

quaternion tkv_value_to_quat(tkv_value value)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_QUAT);
    return *(quaternion *)value.ptr;
}

tkv_object tkv_value_to_tkv(tkv_value value)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_TKV);
    return *(tkv_object *)value.ptr;
}

// setters for mutable types

#define IMPL_TKV_SETTER(type, enum_type)                                                                               \
    void tkv_value_set_##type(tkv_value value, type new_val)                                                           \
    {                                                                                                                  \
        assert(value.meta.tkv_value_type == TKV_VALUE_##enum_type);                                                    \
        assert(value.meta.tkv_value_state != TKV_STATE_CONST);                                                         \
        *(type *)value.ptr = new_val;                                                                                  \
    }

IMPL_TKV_SETTER(i8, I8)
IMPL_TKV_SETTER(i16, I16)
IMPL_TKV_SETTER(i32, I32)
IMPL_TKV_SETTER(i64, I64)

IMPL_TKV_SETTER(u8, U8)
IMPL_TKV_SETTER(u16, U16)
IMPL_TKV_SETTER(u32, U32)
IMPL_TKV_SETTER(u64, U64)

IMPL_TKV_SETTER(f32, F32)
IMPL_TKV_SETTER(f64, F64)

void tkv_value_set_vec3(tkv_value value, vec3 new_val)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_VEC3);
    assert(value.meta.tkv_value_state != TKV_STATE_CONST);
    *(vec3 *)value.ptr = new_val;
}

void tkv_value_set_quat(tkv_value value, quaternion new_val)
{
    assert(value.meta.tkv_value_type == TKV_VALUE_QUAT);
    assert(value.meta.tkv_value_state != TKV_STATE_CONST);
    *(quaternion *)value.ptr = new_val;
}

void tkv_value_set_changed(tkv_value value)
{
    if (value.meta.tkv_value_state != TKV_STATE_NETWORKABLE)
        return;

    // tkv_object object = tkv_value_get_root(value);

    // u16 keys_total = tkv_object_get_length(object);
    // tkv_value_meta *metas = (tkv_value_meta *)(object + sizeof(u16) + sizeof(u32) + sizeof(tkv_key) * keys_total);

    // (metas + value.tkv_meta_index)->tkv_value_state = TKV_STATE_CHANGED;

    // That would mean that id have to check every tkv tree for all the changed values and put them in a list after the
    // fact. Tahts why it would be cool to have an arena for changes instead.

    // TODO: have an arena for tkv value updates instead of a changed state of the value...
}

/*==============================================================================
  TYPE STRING MAPPING

  Maps string type names from text format to TKV_VALUE_TYPE enum values.
  This is called during parsing to determine the data type.

  EXTENSION POINT: When adding a new type (e.g., i32):
  1. Add new enum value to TKV_VALUE_TYPE (or use TKV_VALUE_UNUSED1)
  2. Add mapping here: if (strcmp(type, "i32") == 0) return TKV_VALUE_I32;
  3. Update parsing switch in tkv_parse_object()
  4. Update serialization in tkv_serialize_recursive()
  5. Add conversion functions (tkv_value_to_i32, tkv_value_set_i32)
===============================================================================*/
i8 tkv_string_to_tkv_type(char *type)
{
#define STRCMP_AND_RETURN(str, enum_val)                                                                               \
    if (strcmp(type, str) == 0)                                                                                        \
        return TKV_VALUE_##enum_val;

    STRCMP_AND_RETURN("bool", BOOL)

    STRCMP_AND_RETURN("i8", I8)
    STRCMP_AND_RETURN("i16", I16)
    STRCMP_AND_RETURN("i32", I32)
    STRCMP_AND_RETURN("i64", I64)

    STRCMP_AND_RETURN("u8", U8)
    STRCMP_AND_RETURN("u16", U16)
    STRCMP_AND_RETURN("u32", U32)
    STRCMP_AND_RETURN("u64", U64)

    STRCMP_AND_RETURN("f32", F32)
    STRCMP_AND_RETURN("f64", F64)

    STRCMP_AND_RETURN("str", STR)
    STRCMP_AND_RETURN("arr", ARR)
    STRCMP_AND_RETURN("vec3", VEC3)
    STRCMP_AND_RETURN("quat", QUAT)

    STRCMP_AND_RETURN("tkv", TKV)
#undef STRCMP_AND_RETURN

    return -1; // Unknown type
}

u8 tkv_type_length_flat(i8 type)
{
    switch (type)
    {
    case TKV_VALUE_BOOL:
        return sizeof(bool);
    case TKV_VALUE_I8:
    case TKV_VALUE_U8:
        return sizeof(i8);
    case TKV_VALUE_I16:
    case TKV_VALUE_U16:
        return sizeof(i16);
    case TKV_VALUE_I32:
    case TKV_VALUE_U32:
        return sizeof(i32);
    case TKV_VALUE_I64:
    case TKV_VALUE_U64:
        return sizeof(i64);
    case TKV_VALUE_F32:
        return sizeof(f32);
    case TKV_VALUE_F64:
        return sizeof(f64);
    case TKV_VALUE_VEC3:
        return sizeof(vec3);
    case TKV_VALUE_QUAT:
        return sizeof(quaternion);
    default:
        assert(0 && "Type does not have a fixed flat size");
        return 0;
    }
}

/*==============================================================================
  PORTABLE BINARY VALUE SERIALIZERS

  These functions serialize individual typed values to a buffer in TKV binary
  value format. They are independent of the parser and can be reused for
  object field addition, network sync, file I/O, etc.
===============================================================================*/

u32 tkv_write_value_bool(u8 *buffer, bool value)
{
    *(bool *)buffer = value;
    return sizeof(bool);
}

u32 tkv_write_value_i8(u8 *buffer, i8 value)
{
    *(i8 *)buffer = value;
    return sizeof(i8);
}

u32 tkv_write_value_i16(u8 *buffer, i16 value)
{
    *(i16 *)buffer = value;
    return sizeof(i16);
}

u32 tkv_write_value_i32(u8 *buffer, i32 value)
{
    *(i32 *)buffer = value;
    return sizeof(i32);
}

u32 tkv_write_value_i64(u8 *buffer, i64 value)
{
    *(i64 *)buffer = value;
    return sizeof(i64);
}

u32 tkv_write_value_u8(u8 *buffer, u8 value)
{
    *(u8 *)buffer = value;
    return sizeof(u8);
}

u32 tkv_write_value_u16(u8 *buffer, u16 value)
{
    *(u16 *)buffer = value;
    return sizeof(u16);
}

u32 tkv_write_value_u32(u8 *buffer, u32 value)
{
    *(u32 *)buffer = value;
    return sizeof(u32);
}

u32 tkv_write_value_u64(u8 *buffer, u64 value)
{
    *(u64 *)buffer = value;
    return sizeof(u64);
}

u32 tkv_write_value_f32(u8 *buffer, f32 value)
{
    *(f32 *)buffer = value;
    return sizeof(f32);
}

u32 tkv_write_value_f64(u8 *buffer, f64 value)
{
    *(f64 *)buffer = value;
    return sizeof(f64);
}

u32 tkv_write_value_str(u8 *buffer, const char *str)
{
    u32 len = strlen(str) + 1;
    memcpy(buffer, str, len);
    return len;
}

u32 tkv_write_value_arr(u8 *buffer, u16 element_size, u16 array_length, const u8 *bytes)
{
    *(u16 *)buffer = element_size;
    *(u16 *)(buffer + sizeof(u16)) = array_length;
    u32 data_bytes = element_size * array_length;
    memcpy(buffer + 2 * sizeof(u16), bytes, data_bytes);
    return 2 * sizeof(u16) + data_bytes;
}

u32 tkv_write_value_vec3(u8 *buffer, vec3 value)
{
    *(vec3 *)buffer = value;
    return sizeof(vec3);
}

u32 tkv_write_value_quat(u8 *buffer, quaternion value)
{
    *(quaternion *)buffer = value;
    return sizeof(quaternion);
}

u32 tkv_write_value_tkv(u8 *buffer, tkv_object value)
{
    *(tkv_object *)buffer = value;
    return sizeof(tkv_object);
}
