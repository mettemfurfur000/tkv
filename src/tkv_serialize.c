#include "tkv_internal.h"

#include <stdio.h>
#include <string.h>

/*==============================================================================
  TYPE NAME LOOKUP

  Returns the string name for a TKV_VALUE_TYPE enum value.
  Used during serialization to write type names in text output.

  EXTENSION POINT: When adding a new type, add a corresponding case here.
===============================================================================*/
static const char *tkv_type_name(u8 type)
{
#define CASE_RETURN_STR(enum_val, ret)                                                                                 \
    case TKV_VALUE_##enum_val:                                                                                         \
        return ret;
    switch (type)
    {
        CASE_RETURN_STR(BOOL, "bool")

        CASE_RETURN_STR(I8, "i8")
        CASE_RETURN_STR(I16, "i16")
        CASE_RETURN_STR(I32, "i32")
        CASE_RETURN_STR(I64, "i64")

        CASE_RETURN_STR(U8, "u8")
        CASE_RETURN_STR(U16, "u16")
        CASE_RETURN_STR(U32, "u32")
        CASE_RETURN_STR(U64, "u64")

        CASE_RETURN_STR(F32, "f32")
        CASE_RETURN_STR(F64, "f64")

        CASE_RETURN_STR(STR, "str")
        CASE_RETURN_STR(ARR, "arr")
        CASE_RETURN_STR(VEC3, "vec3")
        CASE_RETURN_STR(QUAT, "quat")

        CASE_RETURN_STR(TKV, "tkv")
    default:
        return "unknown";
    }
#undef CASE_RETURN_STR
}

// Forward declaration for recursive size calculation
static u32 tkv_calculate_size_recursive(tkv_object object, u32 indent_level);

// Calculate the size needed to serialize a complete TKV object
static u32 tkv_calculate_size_recursive(tkv_object object, u32 indent_level)
{
    char temp_buffer[2048] = {};

    u32 total_size = 0;
    u32 indent_spaces = indent_level * 4;

    // Open brace + newline
    total_size += 2;

    u16 num_keys = tkv_object_get_length(object);

    for (u16 i = 0; i < num_keys; i++)
    {
        // Indentation
        total_size += indent_spaces;

        tkv_key key = tkv_object_get_key(object, i);
        char key_name[TKV_KEY_LEN_MAX + 1];
        tkv_key_to_str(key, key_name);
        key_name[key.size] = '\0';

        tkv_value val = tkv_get_value(object, key_name);
        const char *type_str = tkv_type_name(val.meta.tkv_value_type);

        // "type key = "
        total_size += strlen(type_str) + 1 + strlen(key_name) + 3; // +3 for " = "

        // Handle nested tkv specially
        if (val.meta.tkv_value_type == TKV_VALUE_TKV)
        {
            tkv_object child = tkv_value_to_tkv(val);
            total_size += tkv_calculate_size_recursive(child, indent_level + 1);
        }
        else
        {
            // total_size += tkv_calculate_value_size(val);
            // instead of doing that we can serialize the value to a temp buffer and measure the written size, which
            // will be more accurate for things like floats and strings
            u32 written = tkv_serialize_value((u8 *)temp_buffer, sizeof(temp_buffer), val);
            total_size += written;
        }

        // ";\n"
        total_size += 2;
    }

    // Indentation for closing brace
    total_size += indent_spaces;

    // Closing brace
    total_size += 1;

    return total_size;
}

/*==============================================================================
  SERIALIZE SINGLE VALUE TO TEXT

  Converts a TKV value to its text representation in a buffer.
  Returns the number of bytes written (not including null terminator).

  NOTE: Buffer may overflow. Caller is responsible for pre-allocation.

  EXTENSION POINT: When adding a new type (e.g., i32):
  1. Add a case here to format the value appropriately
  2. Use snprintf with appropriate format string
  3. Update tkv_calculate_value_size() to estimate size for your type
===============================================================================*/
u32 tkv_serialize_value(u8 *buffer, u32 buffer_size, tkv_value value)
{
    u32 written = 0;
    u8 type = value.meta.tkv_value_type;

    i64 temp_i64;
    u64 temp_u64;

#define TKV_SWITCH_SERIALIZE(type_enum, type, temp_var)                                                                \
    case TKV_VALUE_##type_enum:                                                                                        \
        temp_var = *(type *)value.ptr;                                                                                 \
        written = snprintf((char *)buffer, buffer_size, "%lld", temp_var);                                             \
        break;

    switch (type)
    {
    case TKV_VALUE_BOOL:;
        bool b = *(bool *)value.ptr;
        written = snprintf((char *)buffer, buffer_size, "%s", b ? "true" : "false");
        break;

        TKV_SWITCH_SERIALIZE(I8, i8, temp_i64)
        TKV_SWITCH_SERIALIZE(U8, u8, temp_u64)

        TKV_SWITCH_SERIALIZE(I16, i16, temp_i64)
        TKV_SWITCH_SERIALIZE(U16, u16, temp_u64)

        TKV_SWITCH_SERIALIZE(I32, i32, temp_i64)
        TKV_SWITCH_SERIALIZE(U32, u32, temp_u64)

        TKV_SWITCH_SERIALIZE(I64, i64, temp_i64)
        TKV_SWITCH_SERIALIZE(U64, u64, temp_u64)

    case TKV_VALUE_F32:;
        f32 f1 = *(f32 *)value.ptr;
        written = snprintf((char *)buffer, buffer_size, "%.9g", f1);
        break;

    case TKV_VALUE_F64:;
        f64 f2 = *(f64 *)value.ptr;
        written = snprintf((char *)buffer, buffer_size, "%.17g", f2);
        break;

    case TKV_VALUE_STR:;
        char *str = (char *)value.ptr;
        written = snprintf((char *)buffer, buffer_size, "\"%s\"", str);
        break;

    case TKV_VALUE_ARR:;
        tkv_array arr = tkv_value_to_arr(value);
        u32 bytes_left = (buffer_size > written) ? (buffer_size - written) : 0;
        written += snprintf((char *)(buffer + written), bytes_left, "[ ");

        for (u16 i = 0; i < arr.array_length; i++)
        {
            u8 byte_val = arr.bytes[i];
            bytes_left = (buffer_size > written) ? (buffer_size - written) : 0;
            u32 this_write = snprintf((char *)(buffer + written), bytes_left, "0x%02x ", byte_val);
            written += this_write;
        }

        // Always write closing bracket, even if buffer overflowed
        bytes_left = (buffer_size > written) ? (buffer_size - written) : 0;
        u32 closing_write = snprintf((char *)(buffer + written), bytes_left, "]");
        written += closing_write;
        break;

    case TKV_VALUE_VEC3:;
        vec3 v = *(vec3 *)value.ptr;
        written = snprintf((char *)buffer, buffer_size, "(%.9g, %.9g, %.9g)", v.x, v.y, v.z);
        break;

    case TKV_VALUE_QUAT:;
        quaternion q = *(quaternion *)value.ptr;

        written = snprintf((char *)buffer, buffer_size, "(%.9g, %.9g, %.9g, %.9g)", q.x, q.y, q.z, q.w);
        break;

    case TKV_VALUE_TKV:;
        // For nested objects, we'll handle this differently
        // Just write a placeholder here
        written = snprintf((char *)buffer, buffer_size, "{...}");
        break;

    default:
        written = snprintf((char *)buffer, buffer_size, "???");
        break;
    }

    assert(written < buffer_size && "Buffer overflow in tkv_serialize_value, increase buffer size");

    return written;

#undef TKV_SWITCH_SERIALIZE
}

// Recursive helper to serialize with proper indentation
static u32 tkv_serialize_recursive(tkv_object object, u8 *buffer, u32 buffer_size, u32 indent_level)
{
    u32 written = 0;
    u32 indent_spaces = indent_level * 4;

    // Open brace
    if (written < buffer_size)
        written += snprintf((char *)(buffer + written), buffer_size - written, "{\n");

    u16 num_keys = tkv_object_get_length(object);

    for (u16 i = 0; i < num_keys; i++)
    {
        // Indentation
        if (written < buffer_size)
        {
            for (u32 j = 0; j < indent_spaces && written < buffer_size; j++)
                buffer[written++] = ' ';
        }

        tkv_key key = tkv_object_get_key(object, i);
        char key_name[TKV_KEY_LEN_MAX + 1];
        tkv_key_to_str(key, key_name);
        key_name[key.size] = '\0';

        tkv_value val = tkv_get_value(object, key_name);
        const char *type_str = tkv_type_name(val.meta.tkv_value_type);

        // Write "type name = "
        if (written < buffer_size)
            written += snprintf((char *)(buffer + written), buffer_size - written, "%s %s = ", type_str, key_name);

        // Handle nested tkv specially
        if (val.meta.tkv_value_type == TKV_VALUE_TKV)
        {
            tkv_object child = tkv_value_to_tkv(val);
            u32 child_written =
                tkv_serialize_recursive(child, buffer + written, buffer_size - written, indent_level + 1);
            written += child_written;
        }
        else
        {
            // Serialize the value
            u8 temp_buffer[8 * 1024];
            u32 value_size = tkv_serialize_value(temp_buffer, sizeof(temp_buffer), val);

            if (written + value_size < buffer_size)
            {
                memcpy(buffer + written, temp_buffer, value_size);
            }
            // Always increment written to track total size needed
            written += value_size;
        }

        snprintf((char *)(buffer + written), buffer_size - written, ";");
        written += 1;

        // Newline
        if (written < buffer_size)
            written += snprintf((char *)(buffer + written), buffer_size - written, "\n");
    }

    // Indentation for closing brace
    if (written < buffer_size)
    {
        for (u32 j = 0; j < indent_spaces && written < buffer_size; j++)
            buffer[written++] = ' ';
    }

    // Close brace
    if (written < buffer_size)
        written += snprintf((char *)(buffer + written), buffer_size - written, "}");

    return written;
}

char *tkv_serialize_object(tkv_object object, arena *output_arena)
{
    if (!object)
        return NULL;

    // Calculate exact size needed for serialization
    u32 needed_size = tkv_calculate_size_recursive(object, 0);

    // Add padding for array/string size variability
    // Use 5x the calculated size + 32KB base as a good balance
    u32 alloc_size = needed_size * 5 + 32768;

    // Cap to prevent excessive allocations that exceed Windows command-line limits
    if (alloc_size > 32000) // Stay under ~32KB command-line limit
        alloc_size = 32000;

    u8 *buffer = arena_alloc(output_arena, alloc_size);
    if (!buffer)
        return NULL;

    u32 written = tkv_serialize_recursive(object, buffer, alloc_size, 0);

    // Null terminate
    if (written < alloc_size)
        buffer[written] = '\0';
    else
        buffer[alloc_size - 1] = '\0';

    return (char *)buffer;
}

u8 *tkv_serialize_for_network(tkv_object object, arena *output_arena, u32 *out_len)
{
    if (!object)
        return NULL;

    char *s = tkv_serialize_object(object, output_arena);
    if (!s)
        return NULL;

    if (out_len)
        *out_len = (u32)strlen(s);

    return (u8 *)s;
}
