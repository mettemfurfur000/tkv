#include "tkv_internal.h"

#include <temlib/error.h>
#include <temlib/tokenizer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*==============================================================================
  TEMPORARY LINKED LIST NODE

  Used during parsing to accumulate key-value pairs before writing the final
  binary format. We build a linked list during parsing, then write all keys
  and metadata in order to the final binary structure.
===============================================================================*/
typedef struct temp_tkv_ll_node tkv_ll_node;

typedef struct temp_tkv_ll_node
{
    tkv_ll_node *next;
    tkv_key key;
} tkv_ll_node;

/* Parses text into the TKV binary layout. */

static bool parse_float_component(token *value, const char **source, i32 *line, f64 *result)
{
    bool negative = false;

    if (value->type == TOK_MINUS)
    {
        negative = true;
        *value = token_next(source, line);
    }

    if (value->type != TOK_FLOAT && value->type != TOK_NUMBER)
    {
        ERROR_PUSHF("Expected a float value at %d, got '%s'\n", *line, value->text);
        return false;
    }

    *result = value->type == TOK_FLOAT ? strtod(value->text, NULL) : (f64)value->value;
    if (negative)
        *result = -*result;

    return true;
}

static bool parse_vector_components(token value, const char **source, i32 *line, f32 *components, u8 count)
{
    if (value.type != TOK_BRACKET_LEFT)
    {
        ERROR_PUSHF("Expected a '(' at %d, got '%s'\n", *line, value.text);
        return false;
    }

    for (u8 i = 0; i < count; i++)
    {
        value = token_next(source, line);
        f64 component;
        if (!parse_float_component(&value, source, line, &component))
            return false;
        components[i] = (f32)component;

        if (i + 1 < count)
        {
            value = token_next(source, line);
            if (value.type != TOK_COMMA)
            {
                ERROR_PUSHF("Expected a comma at %d, got '%s'\n", *line, value.text);
                return false;
            }
        }
    }

    value = token_next(source, line);
    if (value.type != TOK_BRACKET_RIGHT)
    {
        ERROR_PUSHF("Expected a ')' at %d, got '%s'\n", *line, value.text);
        return false;
    }

    return true;
}

static void copy_scratch_value(u8 type, u8 **scratch, u8 **destination)
{
    u32 size;
    switch (type)
    {
#define FIXED_VALUE_SIZE(type_enum, value_type)                                                                        \
    case TKV_VALUE_##type_enum:                                                                                        \
        size = sizeof(value_type);                                                                                     \
        break;
        FIXED_VALUE_SIZE(BOOL, bool)
        FIXED_VALUE_SIZE(I8, i8)
        FIXED_VALUE_SIZE(I16, i16)
        FIXED_VALUE_SIZE(I32, i32)
        FIXED_VALUE_SIZE(I64, i64)
        FIXED_VALUE_SIZE(U8, u8)
        FIXED_VALUE_SIZE(U16, u16)
        FIXED_VALUE_SIZE(U32, u32)
        FIXED_VALUE_SIZE(U64, u64)
        FIXED_VALUE_SIZE(F32, f32)
        FIXED_VALUE_SIZE(F64, f64)
        FIXED_VALUE_SIZE(VEC3, vec3)
        FIXED_VALUE_SIZE(QUAT, quaternion)
#undef FIXED_VALUE_SIZE
    case TKV_VALUE_STR:
        size = strlen((char *)*scratch) + 1;
        break;
    case TKV_VALUE_ARR:
        size = 2 * sizeof(u16) + (u32) * (u16 *)*scratch * *(u16 *)(*scratch + sizeof(u16));
        break;
    case TKV_VALUE_TKV:
        size = sizeof(tkv_object);
        break;
    default:
        assert(0 && "Unknown tkv type");
        return;
    }

    memcpy(*destination, *scratch, size);
    *scratch += size;
    *destination += size;
}

tkv_object tkv_parse_object(const char **tkv_source, arena *scratchpad_arena, arena *tkv_arena)
{
    const char *s = *tkv_source;
    tkv_ll_node *root = NULL;
    tkv_ll_node *prev = NULL;
    u32 scratch_reset_point = scratchpad_arena->length;
    u32 tkv_reset_point = tkv_arena->length;
    u32 nodes_total = 0;
    u32 values_size_bytes = 0;

    i32 line = 1;

    token first_token = token_next(&s, &line);

    error_clear(); // clear any previous errors before parsing

    if (first_token.type != TOK_CURLY_BRACKET_LEFT)
    {
        ERROR_PUSHF("\'{\' not found at the beginning of the tkv tree\n");
        goto parse_fail;
    }

    // this will save some space when going recursive

#define SCRATCH_ADD(type, thing) *(type *)arena_alloc(scratchpad_arena, sizeof(type)) = thing

    bool closed = false;
    while (true)
    {
        token tok = token_next(&s, &line);

        if (tok.type == TOK_EOF)
        {
            ERROR_PUSHF("Unexpected end of input before closing the TKV object at line %d\n", line);
            goto parse_fail;
        }

        if (tok.type == TOK_CURLY_BRACKET_RIGHT)
        {
            closed = true;
            break;
        }

        if (tok.type != TOK_LABEL)
        {
            ERROR_PUSHF("Expected a string for a variable type but got \'%s\' at line %d\n", tok.text, line);
            goto parse_fail;
        }

        i8 tkv_type = tkv_string_to_tkv_type(tok.text);

        if (tkv_type < 0)
        {
            ERROR_PUSHF("Invalid typename \'%s\' at line %d\n", tok.text, line);
            goto parse_fail;
        }

        // got token type, next token name

        token tok_current = token_next(&s, &line);

        if (tok_current.type != TOK_LABEL)
        {
            ERROR_PUSHF("Expected a string for a variable name but got \'%s\' at line %d\n", tok_current.text, line);
            goto parse_fail;
        }

        if (strlen(tok_current.text) > TKV_KEY_LEN_MAX)
        {
            ERROR_PUSHF("Variable name \'%s\' is longer than %d bytes at line %d\n", tok_current.text, TKV_KEY_LEN_MAX,
                        line);
            goto parse_fail;
        }

        if (!tkv_is_valid_key(tok_current.text))
        {
            ERROR_PUSHF("Variable name \'%s\'  contains invalid characters at line %d\n", tok_current.text, line);
            goto parse_fail;
        }

        tkv_key key = tkv_make_key(tok_current.text);

        // valid tkv key is here, tok_current can be reused

        tok_current = token_next(&s, &line);

        if (tok_current.type != TOK_EQUAL)
        {
            ERROR_PUSHF("Expected an equal sign at line %d, got \'%s\'\n", line, tok_current.text);
            goto parse_fail;
        }

        // contents of the variable will follow, we branch here based on the type
        // and build the tkv linked list for keys and values, to construct the final structure later

        // Save position before reading value token - for recursive TKV parsing
        const char *value_src = s;

        tok_current = token_next(&s, &line);

        // Allocate linked list node from heap, not scratchpad, so scratchpad is clean for types/values
        tkv_ll_node *new_node = calloc(1, sizeof(tkv_ll_node));
        if (!new_node)
            goto parse_fail;

        new_node->key = key;

        if (prev)
            prev->next = new_node;

        if (!root)
            root = new_node;

        SCRATCH_ADD(u8, tkv_type);

        switch (tkv_type)
        {
        case TKV_VALUE_BOOL:;
            bool value_b = 0;

            if (strcmp(tok_current.text, "true") == 0)
                value_b = true;
            else if (strcmp(tok_current.text, "false") == 0)
                value_b = false;
            else
            {
                ERROR_PUSHF("Expected a boolean value at %d, got \'%s\'\n", line, tok_current.text);
                goto parse_fail;
            }

            SCRATCH_ADD(bool, value_b);
            values_size_bytes += tkv_type_length_flat(tkv_type);

            break;
        case TKV_VALUE_I8:;
        case TKV_VALUE_I16:;
        case TKV_VALUE_I32:;
        case TKV_VALUE_I64:;
        case TKV_VALUE_U8:;
        case TKV_VALUE_U16:;
        case TKV_VALUE_U32:;
        case TKV_VALUE_U64:;
            bool is_negative = false;

            if (tok_current.type == TOK_MINUS) // hmmm
            {
                if (tkv_type == TKV_VALUE_U8 || tkv_type == TKV_VALUE_U16 || tkv_type == TKV_VALUE_U32 ||
                    tkv_type == TKV_VALUE_U64)
                {
                    ERROR_PUSHF("Unsigned types cannot have negative values at %d, got \'%s\'\n", line,
                                tok_current.text);
                    goto parse_fail;
                }
                is_negative = true;
                tok_current = token_next(&s, &line);
            }

            if (tok_current.type != TOK_NUMBER)
            {
                ERROR_PUSHF("Expected an integer value at %d, got \'%s\'\n", line, tok_current.text);
                goto parse_fail;
            }

            switch (tkv_type)
            {
            case TKV_VALUE_I8:
            case TKV_VALUE_U8:
                SCRATCH_ADD(i8, (i8)(is_negative ? -tok_current.value : tok_current.value));
                break;
            case TKV_VALUE_I16:
            case TKV_VALUE_U16:
                SCRATCH_ADD(i16, (i16)(is_negative ? -tok_current.value : tok_current.value));
                break;
            case TKV_VALUE_I32:
            case TKV_VALUE_U32:
                SCRATCH_ADD(i32, (i32)(is_negative ? -tok_current.value : tok_current.value));
                break;
            case TKV_VALUE_I64:
            case TKV_VALUE_U64:
                SCRATCH_ADD(i64, (i64)(is_negative ? -tok_current.value : tok_current.value));
                break;
            }
            values_size_bytes += tkv_type_length_flat(tkv_type);

            break;
        case TKV_VALUE_F32:;
        case TKV_VALUE_F64:;
            f64 val_f64;
            if (!parse_float_component(&tok_current, &s, &line, &val_f64))
                goto parse_fail;

            switch (tkv_type)
            {
            case TKV_VALUE_F32:
                SCRATCH_ADD(f32, (f32)val_f64);
                break;
            case TKV_VALUE_F64:
                SCRATCH_ADD(f64, val_f64);
                break;
            }
            values_size_bytes += tkv_type_length_flat(tkv_type);

            break;

        case TKV_VALUE_STR:
            if (tok_current.type != TOK_STRING)
            {
                ERROR_PUSHF("Expected a string value at %d, got \'%s\'\n", line, tok_current.text);
                goto parse_fail;
            }

            char *dest = arena_alloc(scratchpad_arena, tok_current.text_length + 1);

            strcpy(dest, tok_current.text);
            dest[tok_current.text_length] = '\0';

            values_size_bytes += tok_current.text_length + 1;

            break;
        case TKV_VALUE_ARR:
            if (tok_current.type != TOK_SQUARE_BRACKET_LEFT)
            {
                ERROR_PUSHF("Expected an \'[\' at %d, got \'%s\'\n", line, tok_current.text);
                goto parse_fail;
            }

            // Store array metadata on scratchpad: element_size (always 1 for u8) and array_length
            u16 array_length = 0;
            u32 array_length_offset = scratchpad_arena->length;

            // Reserve space for element_size and array_length metadata
            SCRATCH_ADD(u16, 1); // element_size = 1 (all elements are u8)
            SCRATCH_ADD(u16, 0); // array_length placeholder, we'll fill this in after parsing

            tok_current = token_next(&s, &line);
            while (tok_current.type != TOK_SQUARE_BRACKET_RIGHT)
            {
                if (tok_current.type != TOK_NUMBER && tok_current.type != TOK_CHAR_LITERAL)
                {
                    ERROR_PUSHF("Expected a number or a character literal value at %d, got \'%s\'\n", line,
                                tok_current.text);
                    goto parse_fail;
                }

                if (tok_current.type == TOK_NUMBER)
                {
                    if (tok_current.value > 0xff || tok_current.value < 0)
                    {
                        ERROR_PUSHF("Number is out of the byte's range at %d, got \'%lld\' as an input\n", line,
                                    tok_current.value);
                        goto parse_fail;
                    }
                    SCRATCH_ADD(u8, tok_current.value);
                }
                else
                {
                    SCRATCH_ADD(u8, tok_current.text[0]);
                }

                array_length++;
                values_size_bytes += tkv_type_length_flat(TKV_VALUE_U8);

                tok_current = token_next(&s, &line);
            }

            // Write the actual array_length to the reserved location
            *(u16 *)((u8 *)scratchpad_arena->base + array_length_offset + sizeof(u16)) = array_length;

            values_size_bytes += 2 * sizeof(u16); // Account for metadata size

            break;
        case TKV_VALUE_VEC3:;
            f32 vec3_components[3] = {0};
            if (!parse_vector_components(tok_current, &s, &line, vec3_components, 3))
                goto parse_fail;

            SCRATCH_ADD(vec3, *(vec3 *)vec3_components);
            values_size_bytes += tkv_type_length_flat(TKV_VALUE_VEC3);
            break;

        case TKV_VALUE_QUAT:;
            f32 quat_components[4] = {0};
            if (!parse_vector_components(tok_current, &s, &line, quat_components, 4))
                goto parse_fail;

            SCRATCH_ADD(quaternion, *(quaternion *)quat_components);
            values_size_bytes += tkv_type_length_flat(TKV_VALUE_QUAT);
            break;

        case TKV_VALUE_TKV:;
            // TODO: sigsegvs here when the tkv is malformed, need to handle that better
            // Start parsing from the saved position (before the "{" token)
            // and update s to after the parsed TKV
            const char *s_child = value_src;
            tkv_object child_tkv = tkv_parse_object(&s_child, scratchpad_arena, tkv_arena);
            if (!child_tkv)
                goto parse_fail;
            s = s_child; // Update parent's s to after the child TKV

            // Store a pointer to the child tkv object on the scratchpad
            // The child object remains in tkv_arena, we just store a reference to it
            SCRATCH_ADD(tkv_object, child_tkv);

            values_size_bytes += sizeof(tkv_object);

            break;

        default:
            assert(0 && "Not implemented");
        } // done writing the key-value pair

        prev = new_node;

        tok_current = token_next(&s, &line);

        if (tok_current.type != TOK_SEMICOLON)
        {
            ERROR_PUSHF("Expected an semicolon at line %d, got \'%s\'\n", line, tok_current.text);
            // ERROR_PUSHF( "Token type was %s\n", token_str(tok_current.type));
            // print some more info about the current parsed line
            ERROR_PUSHF("Current line content around error: ...%.*s...\n", 50, s);
            goto parse_fail;
        }

        nodes_total++;
    } // the tkv struct has ended. Now start writing the final tkv struct

#undef SCRATCH_ADD

    if (!closed)
        goto parse_fail;

    const u32 key_meta_size = nodes_total * (sizeof(tkv_key) + sizeof(tkv_value_meta));
    const u32 total_size = values_size_bytes + key_meta_size + sizeof(u32) + sizeof(u16);

    // Allocate the entire tkv object at once
    u8 *tkv_start = arena_alloc(tkv_arena, total_size);
    if (!tkv_start)
        goto parse_fail;

    // Calculate section pointers
    u8 *header = tkv_start;
    u8 *keys_section = header + sizeof(u16) + sizeof(u32);
    u8 *metas_section = keys_section + nodes_total * sizeof(tkv_key);
    u8 *values_section = metas_section + nodes_total * sizeof(tkv_value_meta);

    // Write header
    *(u16 *)header = nodes_total;
    *(u32 *)(header + sizeof(u16)) = total_size;

    // Write keys from linked list
    tkv_ll_node *node = root;
    u16 key_index = 0;

    while (node != NULL && key_index < nodes_total)
    {
        *(tkv_key *)(keys_section + key_index * sizeof(tkv_key)) = node->key;
        node = node->next;
        key_index++;
    }

    // Walk scratchpad to extract types and values, write metadata and values
    u8 *scratch_ptr = (u8 *)scratchpad_arena->base + scratch_reset_point;
    u8 *scratch_end = (u8 *)scratchpad_arena->base + scratchpad_arena->length;
    u8 *values_write_ptr = values_section;
    u32 value_index = 0;

    while (value_index < nodes_total && scratch_ptr < scratch_end)
    {
        u8 type = *scratch_ptr;
        scratch_ptr += sizeof(u8);

        u32 value_offset = (u32)(values_write_ptr - tkv_start);
        copy_scratch_value(type, &scratch_ptr, &values_write_ptr);

        // Write metadata
        tkv_value_meta meta = {
            .tkv_value_type = type,
            .tkv_value_state = TKV_STATE_CONST,
            .tkv_value_offset = value_offset,
        };

        *(tkv_value_meta *)(metas_section + value_index * sizeof(tkv_value_meta)) = meta;

        value_index++;
    }

    // reset consumed space for the caller above us
    // without it, each child tkv would eat double-ish the memory on the scratchpad, while leaving the result arena
    // completely empty.
    scratchpad_arena->length = scratch_reset_point;

    // Free the temporary linked list nodes
    tkv_ll_node *next_node;
    node = root;
    while (node != NULL)
    {
        next_node = node->next;
        SAFE_FREE(node);
        node = next_node;
    }

    // Update the caller's source pointer to after the parsed TKV
    *tkv_source = s;

    return (tkv_object)tkv_start;

parse_fail:
#undef SCRATCH_ADD
    scratchpad_arena->length = scratch_reset_point;
    tkv_arena->length = tkv_reset_point;
    while (root)
    {
        tkv_ll_node *next = root->next;
        SAFE_FREE(root);
        root = next;
    }
    return NULL;
}

tkv_object tkv_parse_object_once(const char **tkv_source, arena *tkv_arena)
{
    arena *scratch = arena_create(4096);
    if (!scratch)
        return NULL;

    tkv_object obj = tkv_parse_object(tkv_source, scratch, tkv_arena);

    arena_destroy(scratch);
    return obj;
}
