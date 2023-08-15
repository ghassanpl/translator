#pragma once

#ifdef __cplusplus
extern "C" {
#else
typedef char bool;
#endif

struct translator_context
{
	struct translator_context* parent_context;
	void* user_data;

	/// TODO: Respect these
	struct
	{
		bool parse_escapes;
		char opening_delimiter;
		char closing_delimiter;
		char var_symbol;
		bool maintain_call_stack;
		bool call_stack_store_call_string;
	} options;
};
typedef struct translator_context translator_context;

translator_context* translator_new_context();
void translator_delete_context(translator_context* context);

typedef struct value_t* value;
typedef struct value_ref_t* value_ref;

/// TODO: Retrieving the three following three callbacks

typedef value(*translator_eval_func)(translator_context* context, value_ref* arguments, int num_arguments, void* user_data);
void translator_set_unknown_func_eval(translator_context* context, translator_eval_func func, void* user_data);

typedef value(*unknown_var_eval_func)(translator_context* context, const char* var_name, void* user_data);
void translator_set_unknown_var_eval(translator_context* context, unknown_var_eval_func func, void* user_data);

typedef value(*error_handler_func)(translator_context const* context, const char* error_desc, void* user_data);
void translator_set_error_handler(translator_context* context, error_handler_func func, void* user_data);

/// TODO: Changing `json_value_to_str_func` from C api

/// TODO: Enumerating and retrieving eval_funcs

void translator_bind_function(translator_context* context, const char* signature, translator_eval_func func, void* func_user_data);
bool translator_function_exists(translator_context* context, const char* signature);
bool translator_has_own_function(translator_context* context, const char* signature);
void translator_erase_own_function(translator_context* context, const char* signature);
void translator_clear_own_functions(translator_context* context);

/// TODO: Enumerating user vars

value_ref translator_user_var(translator_context* context, const char* name);
value_ref translator_set_user_var(translator_context* context, const char* name, value_ref v);
value_ref translator_set_local_user_var(translator_context* context, const char* name, value_ref v);
value_ref translator_get_user_var(translator_context* context, const char* name);
void translator_remove_user_var(translator_context* context, const char* name, bool only_local);
void translator_clear_local_user_vars(translator_context* context);
bool translator_is_var_local(translator_context* context, const char* name);
translator_context* translator_user_var_owner_context(translator_context* context, const char* name);

value translator_interpolate(translator_context* context, const char* str);

/// Interpolates `str` and puts the result to `out_buf` as if by `strncpy_s(out_buf, buf_size, result);`
const char* translator_interpolate_to(translator_context* context, const char* str, char* out_buf, int buf_size);

/// Interpolates `str` and returns a string that can be released with free();
const char* translator_interpolate_str(translator_context* context, const char* str);

enum {
	TRVAL_NULL,
	TRVAL_OBJECT,
	TRVAL_ARRAY,
	TRVAL_STRING,
	TRVAL_BOOLEAN,
	TRVAL_NUMBER_INTEGER,
	TRVAL_NUMBER_UNSIGNED,
	TRVAL_NUMBER_FLOAT,
	TRVAL_BINARY,
};

value translator_new_null_value();
value translator_new_bool_value(bool val);
value translator_new_string_value(const char* zstr);
value translator_new_string_value_n(const char* str, int n);
value translator_new_integer_value(long long int val);
value translator_new_unsigned_value(unsigned long long int val);
value translator_new_double_value(double val);

void translator_set_null_value(value_ref val);
void translator_set_bool_value(value_ref val, bool b);
void translator_set_string_value(value_ref val, const char* zstr);
void translator_set_string_value_n(value_ref val, const char* str, int n);
void translator_set_integer_value(value_ref val, long long int num);
void translator_set_unsigned_value(value_ref val, unsigned long long int num);
void translator_set_double_value(value_ref val, double num);

value translator_duplicate_value(value_ref v);
value translator_take_value(value_ref v);
#define translator_ref_value(v__) ((value_ref)v__)

int translator_value_type(value_ref v);

int translator_is_value_true(value_ref v);

void translator_value_clear(value_ref v);
void translator_string_value_append(value_ref v, const char* zstr);
void translator_string_value_append_n(value_ref v, const char* str, int n);

/// TODO: array/object value functions

const char* translator_value_get_string(value_ref v);
bool translator_value_get_bool(value_ref v);
long long int translator_value_get_integer(value_ref v);
unsigned long long int translator_value_get_unsigned(value_ref v);
double translator_value_get_double(value_ref v);

void translator_delete_value(value v);

#ifdef __cplusplus
}
#endif
