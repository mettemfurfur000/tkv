#include "tkv_internal.h"
#include <temlib/data_io.h>

u8 tkv_write_to_stream(tkv_object object, void *vs)
{
    stream_t *s = (stream_t *)vs;
    u16 n = tkv_object_get_length(object);
    if (stream_write((const u8 *)&n, sizeof(n), s) != 0)
        return FAIL;

    for (u16 i = 0; i < n; i++)
    {
        tkv_key key = tkv_object_get_key(object, i);
        char key_name[TKV_KEY_LEN_MAX + 1];
        tkv_key_to_str(key, key_name);
        key_name[key.size] = '\0';

        tkv_value val = tkv_get_value(object, key_name);
        u8 type = val.meta.tkv_value_type;
        u8 state = val.meta.tkv_value_state;

        if (stream_write((const u8 *)&key, sizeof(key), s) != 0)
            return FAIL;
        if (stream_write(&type, sizeof(type), s) != 0)
            return FAIL;
        if (stream_write(&state, sizeof(state), s) != 0)
            return FAIL;

        switch (type)
        {
        case TKV_VALUE_BOOL:
        {
            u8 b = *(bool *)val.ptr;
            if (stream_write(&b, sizeof(b), s) != 0)
                return FAIL;
            break;
        }
        case TKV_VALUE_I8:
        case TKV_VALUE_U8:
            if (stream_write((const u8 *)val.ptr, 1, s) != 0)
                return FAIL;
            break;
        case TKV_VALUE_I16:
        case TKV_VALUE_U16:
            if (stream_write((const u8 *)val.ptr, sizeof(u16), s) != 0)
                return FAIL;
            break;
        case TKV_VALUE_I32:
        case TKV_VALUE_U32:
        case TKV_VALUE_F32:
            if (stream_write((const u8 *)val.ptr, sizeof(u32), s) != 0)
                return FAIL;
            break;
        case TKV_VALUE_I64:
        case TKV_VALUE_U64:
        case TKV_VALUE_F64:
            if (stream_write((const u8 *)val.ptr, sizeof(u64), s) != 0)
                return FAIL;
            break;
        case TKV_VALUE_STR:
        {
            char *str = (char *)val.ptr;
            u32 len = strlen(str) + 1;
            if (stream_write((const u8 *)&len, sizeof(len), s) != 0)
                return FAIL;
            if (stream_write((const u8 *)str, len, s) != 0)
                return FAIL;
            break;
        }
        case TKV_VALUE_ARR:
        {
            u16 element_size = *(u16 *)val.ptr;
            u16 array_length = *(u16 *)((u8 *)val.ptr + sizeof(u16));
            u32 data_bytes = element_size * array_length;
            if (stream_write((const u8 *)&element_size, sizeof(element_size), s) != 0)
                return FAIL;
            if (stream_write((const u8 *)&array_length, sizeof(array_length), s) != 0)
                return FAIL;
            if (data_bytes > 0 && stream_write((const u8 *)((u8 *)val.ptr + 2 * sizeof(u16)), data_bytes, s) != 0)
                return FAIL;
            break;
        }
        case TKV_VALUE_VEC3:
        {
            f32 *components = (f32 *)val.ptr;
            for (i32 j = 0; j < 3; j++)
                if (stream_write((const u8 *)&components[j], sizeof(f32), s) != 0)
                    return FAIL;
            break;
        }
        case TKV_VALUE_QUAT:
        {
            f32 *components = (f32 *)val.ptr;
            for (i32 j = 0; j < 4; j++)
                if (stream_write((const u8 *)&components[j], sizeof(f32), s) != 0)
                    return FAIL;
            break;
        }
        case TKV_VALUE_TKV:
        {
            tkv_object child = *(tkv_object *)val.ptr;
            if (tkv_write_to_stream(child, s) != SUCCESS)
                return FAIL;
            break;
        }
        default:
            return FAIL;
        }
    }

    return SUCCESS;
}

tkv_object tkv_read_from_stream(void *vs, arena *scratchpad_arena, arena *tkv_arena)
{
    stream_t *s = (stream_t *)vs;
    u16 node_count;
    if (stream_read(&node_count, sizeof(node_count), s) != 0)
        return NULL;

    tkv_object obj = tkv_object_create_empty(tkv_arena);

    for (u16 i = 0; i < node_count; i++)
    {
        tkv_key key;
        u8 type, state;

        if (stream_read(&key, sizeof(key), s) != 0)
            return NULL;
        if (stream_read(&type, sizeof(type), s) != 0)
            return NULL;
        if (stream_read(&state, sizeof(state), s) != 0)
            return NULL;

        char key_name[TKV_KEY_LEN_MAX + 1];
        tkv_key_to_str(key, key_name);
        key_name[key.size] = '\0';

        switch (type)
        {
        case TKV_VALUE_BOOL:
        {
            u8 b;
            stream_read(&b, sizeof(b), s);
            bool val = b != 0;
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_I8:
        {
            i8 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_I16:
        {
            i16 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_I32:
        {
            i32 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_I64:
        {
            i64 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_U8:
        {
            u8 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_U16:
        {
            u16 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_U32:
        {
            u32 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_U64:
        {
            u64 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_F32:
        {
            f32 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_F64:
        {
            f64 val;
            stream_read((u8 *)&val, sizeof(val), s);
            obj = tkv_object_add_field(obj, key_name, type, state, &val, tkv_arena);
            break;
        }
        case TKV_VALUE_STR:
        {
            u32 len;
            stream_read((u8 *)&len, sizeof(len), s);
            char *str = arena_alloc(scratchpad_arena, len);
            stream_read((u8 *)str, len, s);
            obj = tkv_object_add_field(obj, key_name, type, state, str, tkv_arena);
            break;
        }
        case TKV_VALUE_ARR:
        {
            u16 element_size;
            u16 array_length;
            stream_read((u8 *)&element_size, sizeof(element_size), s);
            stream_read((u8 *)&array_length, sizeof(array_length), s);
            u32 data_bytes = element_size * array_length;
            u8 *arr_data = data_bytes > 0 ? arena_alloc(scratchpad_arena, data_bytes) : NULL;
            if (data_bytes > 0)
                stream_read(arr_data, data_bytes, s);
            tkv_array arr = {.element_size = element_size, .array_length = array_length, .bytes = arr_data};
            obj = tkv_object_add_field(obj, key_name, type, state, &arr, tkv_arena);
            break;
        }
        case TKV_VALUE_VEC3:
        {
            f32 components[3];
            for (i32 j = 0; j < 3; j++)
                stream_read((u8 *)&components[j], sizeof(f32), s);
            vec3 v = {components[0], components[1], components[2]};
            obj = tkv_object_add_field(obj, key_name, type, state, &v, tkv_arena);
            break;
        }
        case TKV_VALUE_QUAT:
        {
            f32 components[4];
            for (i32 j = 0; j < 4; j++)
                stream_read((u8 *)&components[j], sizeof(f32), s);
            quaternion q = {components[0], components[1], components[2], components[3]};
            obj = tkv_object_add_field(obj, key_name, type, state, &q, tkv_arena);
            break;
        }
        case TKV_VALUE_TKV:
        {
            tkv_object child = tkv_read_from_stream(s, scratchpad_arena, tkv_arena);
            if (!child)
                return NULL;
            obj = tkv_object_add_field(obj, key_name, type, state, child, tkv_arena);
            break;
        }
        default:
            return NULL;
        }
    }

    return obj;
}

tkv_object tkv_read_from_stream_once(void *s, arena *tkv_arena)
{
    arena *scratch = arena_create(4096);
    if (!scratch)
        return NULL;

    tkv_object obj = tkv_read_from_stream(s, scratch, tkv_arena);

    arena_destroy(scratch);
    return obj;
}
