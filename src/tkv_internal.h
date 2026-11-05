#ifndef TKV_INTERNAL_H
#define TKV_INTERNAL_H

#include "tkv.h"

bool tkv_is_valid_key(const char *input);
i8 tkv_string_to_tkv_type(char *type);
u8 tkv_type_length_flat(i8 type);

u16 tkv_object_get_length(tkv_object object);
u32 tkv_object_get_size(tkv_object object);
tkv_key tkv_object_get_key(tkv_object object, u16 index);

#endif
