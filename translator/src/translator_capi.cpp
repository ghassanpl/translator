#include "../include/ghassanpl/translator/translator_capi.h"
#include "../include/ghassanpl/translator/translator.hpp"

/// C API

using cpp_context = translator::context;

translator_context* translator_new_context()
{
	return new cpp_context{};
}

void translator_init_context_options(translator_context* context)
{
	assert(context);
	context->options = {};
	context->options.opening_delimiter = '[';
	context->options.closing_delimiter = ']';
	context->options.var_symbol = '.';
}

#define self ((cpp_context*)context)

void translator_delete_context(translator_context* context)
{
	delete self;
}

using nlohmann::json;

value to_value(json j)
{
	return (value)(new json(std::move(j)));
}
json& to_json(value_ref v)
{
	assert(v);
	return *(json*)v;
}
json& to_json(value v)
{
	assert(v);
	return *(json*)v;
}
value_ref to_value_ref(json& j)
{
	return (value_ref)&j;
}

json take_result(value result_value)
{
	if (!result_value) return nullptr;
	json result = std::move(to_json(result_value));
	translator_delete_value(result_value);
	return result;
}

auto bind_c_eval_func(translator_eval_func func, void* func_user_data)
{
	return [func, func_user_data](cpp_context& in_contxt, std::vector<json> arguments) {
		std::vector<value_ref> value_copies(arguments.size());
		for (auto& argument : arguments)
			value_copies.push_back(to_value_ref(argument));
		return take_result(func(&in_contxt, value_copies.data(), (int)value_copies.size(), func_user_data));
	};
}

void translator_set_unknown_func_eval(translator_context* context, translator_eval_func func, void* func_user_data)
{
	assert(context);
	if (func)
		self->unknown_func_handler() = bind_c_eval_func(func, func_user_data);
	else
		self->unknown_func_handler() = {};
}

void translator_set_unknown_var_eval(translator_context* context, unknown_var_eval_func func, void* user_data)
{
	assert(context);
	if (func)
	{
		self->unknown_var_value_getter() = [func, user_data](cpp_context& in_context, std::string_view name) {
			return take_result(func(&in_context, std::string{ name }.c_str(), user_data));
		};
	}
	else
		self->unknown_var_value_getter() = {};
}

void translator_set_error_handler(translator_context* context, error_handler_func func, void* user_data)
{
	assert(context);
	if (func)
	{
		self->error_handler() = [func, user_data](cpp_context const& in_context, std::string_view error) {
			return in_context.value_to_string(take_result(func(&in_context, std::string{ error }.c_str(), user_data)));
		};
	}
	else
		self->error_handler() = {};
}

value_ref translator_set_user_var(translator_context* context, const char* name, value_ref v)
{
	assert(context);
	assert(name);
	return to_value_ref(self->set_user_var(name, v ? to_json(v) : nullptr, false));
}

value_ref translator_set_local_user_var(translator_context* context, const char* name, value_ref v)
{
	assert(context);
	assert(name);
	assert(v);
	return to_value_ref(self->set_user_var(name, v ? to_json(v) : nullptr, true));
}

void translator_bind_function(translator_context* context, const char* signature, translator_eval_func func, void* func_user_data)
{
	assert(context);
	assert(signature);
	assert(func);
	//self->functions[name] = bind_c_eval_func(func, func_user_data);
	self->bind_function(signature, bind_c_eval_func(func, func_user_data));
}
/*

bool translator_function_exists(translator_context* context, const char* signature)
{
	assert(context);
	assert(signature);
	return !self->find_functions_by_signature(signature).empty();
}

bool translator_has_own_function(translator_context* context, const char* signature)
{
	assert(context);
	assert(signature);
	return !self->find_functions_by_signature(signature, true).empty();
}

void translator_erase_own_function(translator_context* context, const char* signature)
{
	assert(context);
	assert(signature);
	self->functions.erase(name);
}

void translator_clear_own_functions(translator_context* context)
{
	assert(context);
	self->functions.clear();
}
*/

value_ref translator_user_var(translator_context* context, const char* name)
{
	if (auto existing = translator_get_user_var(context, name))
		return existing;
	return translator_set_user_var(context, name, nullptr);
}

value_ref translator_get_user_var(translator_context* context, const char* name)
{
	assert(context);
	assert(name);
	auto [owner, it] = self->find_variable(name);
	if (!owner) return {};
	return to_value_ref(it->second);
}

void translator_remove_user_var(translator_context* context, const char* name, bool only_local)
{
	assert(context);
	assert(name);
	auto [owner, it] = self->find_variable(name);
	if (!owner) return;

	if (!only_local || context == owner)
		owner->context_variables().erase(it);
}

void translator_clear_local_user_vars(translator_context* context)
{
	assert(context);
	self->context_variables().clear();
}

bool translator_is_var_local(translator_context* context, const char* name)
{
	assert(context);
	auto [owner, it] = self->find_variable(name);
	return context == owner;
}

translator_context* translator_user_var_owner_context(translator_context* context, const char* name)
{
	assert(context);
	auto [owner, it] = self->find_variable(name);
	return owner;
}

value translator_interpolate(translator_context* context, const char* str)
{
	assert(context);
	if (!str) return to_value("");
	return to_value(self->interpolate(str));
}

const char* translator_interpolate_to(translator_context* context, const char* str, char* out_buf, int buf_size)
{
	assert(context);
	if (!str || !out_buf || buf_size < 1) return nullptr;
	const auto result = self->interpolate(str);
	if (strncpy_s(out_buf, (size_t)buf_size, result.data(), result.size()) == 0)
		return out_buf;
	return nullptr;
}

const char* translator_interpolate_str(translator_context* context, const char* str)
{
	assert(context);
	if (!str) return nullptr;
	const auto result = self->interpolate(str);
	return _strdup(result.c_str());
}

value translator_new_null_value()
{
	return to_value(nullptr);
}

value translator_new_bool_value(bool val)
{
	return to_value(val);
}

value translator_new_string_value(const char* zstr)
{
	if (!zstr) return to_value("");
	return to_value(zstr);
}

value translator_new_string_value_n(const char* str, int n)
{
	if (!str) return to_value("");
	return to_value(std::string_view{str, (size_t)n});
}

value translator_new_integer_value(long long int val)
{
	return to_value(val);
}

value translator_new_unsigned_value(unsigned long long int val)
{
	return to_value(val);
}

value translator_new_double_value(double val)
{
	return to_value(val);
}

/// TODO: Instead of asserts, maybe make error reporting in C api configurable?

void translator_set_null_value(value_ref val)
{
	assert(val);
	to_json(val) = nullptr;
}

void translator_set_bool_value(value_ref val, bool b)
{
	assert(val);
	to_json(val) = b;
}

void translator_set_string_value(value_ref val, const char* zstr)
{
	assert(val);
	if (!zstr) zstr = "";
	to_json(val) = zstr;
}

void translator_set_string_value_n(value_ref val, const char* str, int n)
{
	assert(val);
	if (!str)
		to_json(val) = "";
	else
		to_json(val) = std::string_view{ str, (size_t)n };
}

void translator_set_integer_value(value_ref val, long long int num)
{
	assert(val);
	to_json(val) = num;
}

void translator_set_unsigned_value(value_ref val, unsigned long long int num)
{
	assert(val);
	to_json(val) = num;
}

void translator_set_double_value(value_ref val, double num)
{
	assert(val);
	to_json(val) = num;
}

value translator_duplicate_value(value_ref v)
{
	assert(v);
	return to_value(to_json(v));
}

value translator_take_value(value_ref v)
{
	assert(v);
	return to_value(std::move(to_json(v)));
}

int translator_value_type(value_ref v)
{
	assert(v);
	return (int)to_json(v).type();
}

int translator_is_value_true(value_ref v)
{
	assert(v);
	return translator::is_true(to_json(v));
}

void translator_value_clear(value_ref v)
{
	assert(v);
	to_json(v).clear();
}

void translator_string_value_append(value_ref v, const char* zstr)
{
	assert(v);
	auto& j = to_json(v);
	if (j.is_string() && zstr)
		j += zstr;
}

void translator_string_value_append_n(value_ref v, const char* str, int n)
{
	assert(v);
	auto& j = to_json(v);
	if (j.is_string() && str)
		j += std::string_view{ str, (size_t)n };
}

const char* translator_value_get_string(value_ref v)
{
	assert(v);
	if (auto const& j = to_json(v); j.is_string())
		return j.get_ref<json::string_t const&>().c_str();
	return nullptr;
}

bool translator_value_get_bool(value_ref v)
{
	assert(v);
	if (auto const& j = to_json(v); j.is_boolean())
		return j;
	return false;
}

long long int translator_value_get_integer(value_ref v)
{
	assert(v);
	if (auto const& j = to_json(v); j.is_number_integer())
		return j;
	return 0;
}

unsigned long long int translator_value_get_unsigned(value_ref v)
{
	assert(v);
	if (auto const& j = to_json(v); j.is_number_unsigned())
		return j;
	return 0;
}

double translator_value_get_double(value_ref v)
{
	assert(v);
	if (auto const& j = to_json(v); j.is_number_float())
		return j;
	return 0;
}

void translator_delete_value(value v)
{
	delete (json*)v;
}

