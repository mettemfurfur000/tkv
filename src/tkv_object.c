#include "tkv_internal.h"

#include <limits.h>
#include <string.h>

tkv_object tkv_object_add_field(tkv_object object, const char *key_str, u8 type, u8 state, const void *value, arena *arena)
{
	if (!object || !key_str || !value)
		return NULL;

	tkv_key new_key = tkv_make_key(key_str);
	if (!new_key.whole)
		return NULL;

	tkv_value existing = tkv_get_value(object, key_str);
	if (existing.meta.whole != UINT_MAX)
		return NULL;

	u8 value_buf[8192];
	u32 value_size = 0;

	switch (type)
	{
	case TKV_VALUE_BOOL:
		value_size = tkv_write_value_bool(value_buf, *(const bool *)value);
		break;
	case TKV_VALUE_I8:
		value_size = tkv_write_value_i8(value_buf, *(const i8 *)value);
		break;
	case TKV_VALUE_I16:
		value_size = tkv_write_value_i16(value_buf, *(const i16 *)value);
		break;
	case TKV_VALUE_I32:
		value_size = tkv_write_value_i32(value_buf, *(const i32 *)value);
		break;
	case TKV_VALUE_I64:
		value_size = tkv_write_value_i64(value_buf, *(const i64 *)value);
		break;
	case TKV_VALUE_U8:
		value_size = tkv_write_value_u8(value_buf, *(const u8 *)value);
		break;
	case TKV_VALUE_U16:
		value_size = tkv_write_value_u16(value_buf, *(const u16 *)value);
		break;
	case TKV_VALUE_U32:
		value_size = tkv_write_value_u32(value_buf, *(const u32 *)value);
		break;
	case TKV_VALUE_U64:
		value_size = tkv_write_value_u64(value_buf, *(const u64 *)value);
		break;
	case TKV_VALUE_F32:
		value_size = tkv_write_value_f32(value_buf, *(const f32 *)value);
		break;
	case TKV_VALUE_F64:
		value_size = tkv_write_value_f64(value_buf, *(const f64 *)value);
		break;
	case TKV_VALUE_STR:
		value_size = tkv_write_value_str(value_buf, (const char *)value);
		break;
	case TKV_VALUE_ARR:
	{
		const tkv_array *arr = (const tkv_array *)value;
		value_size = tkv_write_value_arr(value_buf, arr->element_size, arr->array_length, arr->bytes);
		break;
	}
	case TKV_VALUE_VEC3:
		value_size = tkv_write_value_vec3(value_buf, *(const vec3 *)value);
		break;
	case TKV_VALUE_QUAT:
		value_size = tkv_write_value_quat(value_buf, *(const quaternion *)value);
		break;
	case TKV_VALUE_TKV:
		value_size = tkv_write_value_tkv(value_buf, (tkv_object)value);
		break;
	default:
		return NULL;
	}

	assert(value_size > 0 && value_size <= sizeof(value_buf));

	u16 old_n = tkv_object_get_length(object);
	u32 old_total = tkv_object_get_size(object);

	u32 old_kv_meta_size = old_n * (sizeof(tkv_key) + sizeof(tkv_value_meta));
	u32 old_values_start = sizeof(u16) + sizeof(u32) + old_kv_meta_size;
	u32 old_values_size = old_total - old_values_start;

	u16 new_n = old_n + 1;
	u32 new_kv_meta_size = new_n * (sizeof(tkv_key) + sizeof(tkv_value_meta));
	u32 new_values_start = sizeof(u16) + sizeof(u32) + new_kv_meta_size;
	u32 new_total = new_values_start + old_values_size + value_size;

	u8 *new_obj = arena_alloc(arena, new_total);

	*(u16 *)new_obj = new_n;
	*(u32 *)(new_obj + sizeof(u16)) = new_total;

	u8 *old_keys = (u8 *)object + sizeof(u16) + sizeof(u32);
	u8 *new_keys = new_obj + sizeof(u16) + sizeof(u32);
	memcpy(new_keys, old_keys, old_n * sizeof(tkv_key));

	*(tkv_key *)(new_keys + old_n * sizeof(tkv_key)) = new_key;

	u8 *old_metas = old_keys + old_n * sizeof(tkv_key);
	u8 *new_metas = new_keys + new_n * sizeof(tkv_key);
	memcpy(new_metas, old_metas, old_n * sizeof(tkv_value_meta));

	u32 offset_delta = sizeof(tkv_key) + sizeof(tkv_value_meta);
	for (u16 i = 0; i < old_n; i++)
	{
		tkv_value_meta *meta = (tkv_value_meta *)(new_metas + i * sizeof(tkv_value_meta));
		meta->tkv_value_offset += offset_delta;
	}

	tkv_value_meta new_meta = {
		.tkv_value_type = type,
		.tkv_value_state = state,
		.tkv_value_offset = new_values_start + old_values_size,
	};
	*(tkv_value_meta *)(new_metas + old_n * sizeof(tkv_value_meta)) = new_meta;

	u8 *old_values = (u8 *)object + old_values_start;
	u8 *new_values = new_obj + new_values_start;
	memcpy(new_values, old_values, old_values_size);

	memcpy(new_values + old_values_size, value_buf, value_size);

	return (tkv_object)new_obj;
}

tkv_object tkv_object_create_empty(arena *arena)
{
	u32 total_size = sizeof(u16) + sizeof(u32);
	u8 *obj = arena_alloc(arena, total_size);
	*(u16 *)obj = 0;
	*(u32 *)(obj + sizeof(u16)) = total_size;
	return (tkv_object)obj;
}


