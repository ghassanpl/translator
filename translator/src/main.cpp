#include "../include/ghassanpl/translator/translator.h"

#include <iostream>
#include <sstream>

#define FMT_HEADER_ONLY
#include <fmt/format.h>

using namespace translator;

struct e_break : context::e_scope_terminator { virtual std::string type() const noexcept override { return "break"; } };
struct e_continue : context::e_scope_terminator { virtual std::string type() const noexcept override { return "continue"; } };

static inline json if_then_else(context& e, std::vector<json> args)
{
	e.assert_args(args, 3);
	if (is_true(e.eval_arg(args, 0)))
		return e.eval_arg(args, 1);
	return e.eval_arg(args, 2);
}

static inline json op_is(context& e, std::vector<json> args)
{
	e.eval_args(args, json::value_t::discarded, json::value_t::string);
	return args[0].type_name() == args[1];
}

static inline json op_eq(context& e, std::vector<json> args) { 
	e.eval_args(args, 2);  
	return args[0] == args[1]; 
}
static inline json op_neq(context& e, std::vector<json> args) { e.eval_args(args, 2); return args[0] != args[1]; }
static inline json op_gt(context& e, std::vector<json> args) { e.eval_args(args, 2);  return args[0] > args[1]; }
static inline json op_ge(context& e, std::vector<json> args) { e.eval_args(args, 2);  return args[0] >= args[1]; }
static inline json op_lt(context& e, std::vector<json> args) { e.eval_args(args, 2);  return args[0] < args[1]; }
static inline json op_le(context& e, std::vector<json> args) { e.eval_args(args, 2);  return args[0] <= args[1]; }

static inline json op_not(context& e, std::vector<json> args) { e.eval_args(args, 1);  return !is_true(args[0]); }
static inline json op_and(context& e, std::vector<json> args) {
	e.assert_min_args(args, 2);
	json left;
	for (size_t i = 0; i < args.size(); ++i)
	{
		left = e.eval_arg(args, i);
		if (!is_true(left))
			return left;
	}
	return left;
}
static inline json op_or(context& e, std::vector<json> args) {
	e.assert_min_args(args, 2);
	json left;
	for (size_t i = 0; i < args.size(); ++i)
	{
		left = e.eval_arg(args, i);
		if (is_true(left))
			return left;
	}
	return left;
}

#define IMPL_OPF(lhs, op, rhs) \
	const auto lhs_type = lhs.type();                                                                    \
	const auto rhs_type = rhs.type();                                                                    \
	if (lhs_type == json::value_t::number_integer && rhs_type == json::value_t::number_float) \
		return static_cast<json::number_float_t>(lhs) op (json::number_float_t)rhs;      \
	else if (lhs_type == json::value_t::number_float && rhs_type == json::value_t::number_integer)                   \
		return (json::number_float_t)lhs op static_cast<json::number_float_t>(rhs);          \
	else if (lhs_type == json::value_t::number_unsigned && rhs_type == json::value_t::number_float)                  \
		return static_cast<json::number_float_t>(lhs) op (json::number_float_t)rhs;         \
	else if (lhs_type == json::value_t::number_float && rhs_type == json::value_t::number_unsigned)                  \
		return (json::number_float_t)lhs op static_cast<json::number_float_t>(rhs);         \
	else if (lhs_type == json::value_t::number_unsigned && rhs_type == json::value_t::number_integer)                \
		return static_cast<json::number_integer_t>(lhs) op (json::number_integer_t)rhs;     \
	else if (lhs_type == json::value_t::number_integer && rhs_type == json::value_t::number_unsigned)                \
		return (json::number_integer_t)lhs op static_cast<json::number_integer_t>(rhs);     \
	else return 0;

#define IMPL_OPI(lhs, op, rhs) \
	const auto lhs_type = lhs.type();                                                                    \
	const auto rhs_type = rhs.type();                                                                    \
	if (lhs_type == json::value_t::number_unsigned && rhs_type == json::value_t::number_integer)                \
		return static_cast<json::number_integer_t>(lhs) op (json::number_integer_t)rhs;     \
	else if (lhs_type == json::value_t::number_integer && rhs_type == json::value_t::number_unsigned)                \
		return (json::number_integer_t)lhs op static_cast<json::number_integer_t>(rhs);     \
	else return 0;

static inline json op_plus(context& e, std::vector<json> args) { e.eval_args(args, 2);  IMPL_OPF(args[0], +, args[1]); }
static inline json op_minus(context& e, std::vector<json> args) { e.eval_args(args, 2); IMPL_OPF(args[0], -, args[1]); }
static inline json op_mul(context& e, std::vector<json> args) { e.eval_args(args, 2);   IMPL_OPF(args[0], *, args[1]); }
static inline json op_div(context& e, std::vector<json> args) { e.eval_args(args, 2);   IMPL_OPF(args[0], /, args[1]); }
static inline json op_mod(context& e, std::vector<json> args) { e.eval_args(args, 2);   IMPL_OPI(args[0], %, args[1]); }


static inline json type_of(context& e, std::vector<json> args) {
	const auto val = e.eval_arg(args, 0);
	return val.type_name();
}
static inline json size_of(context& e, std::vector<json> args) {
	const auto val = e.eval_arg(args, 0);
	const json& j = val;
	return j.is_string() ? j.get_ref<json::string_t const&>().size() : j.size();
}

static inline json str(context& e, std::vector<json> args)
{
	auto arg = e.eval_arg(args, 0);
	return e.value_to_string(arg);
}

/// Will evaluate each argument and return the last one
static inline json eval(context& e, std::vector<json> args)
{
	json last = nullptr;
	for (size_t i = 1; i < args.size(); ++i)
		last = e.eval(std::move(args[i]));
	return last;
}

/// Will evaluate each argument and return a list of the results
static inline json list(context& e, std::vector<json> args)
{
	e.eval_args(args);
	std::vector<json> result;
	for (size_t i = 1; i < args.size(); ++i)
		result.push_back(std::move(args[i]));
	return result;
}

/// Will evaluate each argument and concatenate them in a string
static inline json op_cat(context& e, std::vector<json> args)
{
	e.eval_args(args);
	std::string result;
	for (size_t i = 1; i < args.size(); ++i)
		result += e.value_to_string(args[i]);
	return result;
}

/*
void add_prefix_variadic(context& e, translator::context::eval_func const& func, std::string const& name, size_t min_args = 0)
{
	switch (min_args)
	{
	case 0:
		e.functions[name] = func;
	case 1:
		e.functions[name + ":"] = func;
	case 2:
		e.functions[name + ":,:"] = func;
		e.functions[name + ":,*:"] = func;
		break;
	}
}

void add_infix_variadic(context& e, translator::context::eval_func const& func, std::string const& name)
{
	e.functions[':' + name + ':'] = func;
	e.functions[':' + name + "*:"] = func;
}

/// TODO: Instead of building a string name for every call (slow) we could just use the arguments list (every other value)
///		to search through a trie-like container that stores these functions

template <typename... NAMES_RANGE>
void add_infix(context& e, translator::context::eval_func const& func, NAMES_RANGE&&... name_parts)
{
	static_assert(sizeof...(name_parts) > 0);
	std::string name{ ':' };
	((name += name_parts, name += ':'), ...);
	e.functions[name] = func;
}

/// TODO: Instead of building a string name for every call (slow) we could just use the arguments list (every other value)
///		to search through a trie-like container that stores these functions

template <typename... NAMES_RANGE>
void add_prefix(context& e, translator::context::eval_func const& func, NAMES_RANGE&&... name_parts)
{
	static_assert(sizeof...(name_parts) > 0);
	std::string name;
	((name += name_parts, name += ':'), ...);
	e.functions[name] = func;
}
*/


namespace translator
{
}


/// TODO: Instead of building a string name for every call (slow) we could just use the arguments list (every other value)
///		to search through a trie-like container that stores these functions

void open_core_lib(context& e)
{
	/// TODO: [pred .kills with [one? 'bla'], [zero? 'bleh'], [many? 'bluh']]
	
	e.bind_function("if [] then [] else []", if_then_else);
	e.bind_function("[] ? [] : []", if_then_else);

	e.bind_function("[] == []", op_eq);
	e.bind_function("[] eq []", op_eq);
	e.bind_function("[] != []", op_neq);
	e.bind_function("[] neq []", op_neq);
	e.bind_function("[] > []", op_gt);
	e.bind_function("[] gt []", op_gt);
	e.bind_function("[] >= []", op_ge);
	e.bind_function("[] ge []", op_ge);
	e.bind_function("[] < []", op_lt);
	e.bind_function("[] lt []", op_lt);
	e.bind_function("[] <= []", op_le);
	e.bind_function("[] le []", op_le);
	e.bind_function("not []", op_not);

	e.bind_function("[] + []", op_plus);
	e.bind_function("[] - []", op_minus);
	e.bind_function("[] * []", op_mul);
	e.bind_function("[] / []", op_div);
	e.bind_function("[] % []", op_mod);

	e.bind_function("[] is []", op_is);
	e.bind_function("type-of []", type_of);
	e.bind_function("typeof []", type_of);
	e.bind_function("size-of []", size_of);
	e.bind_function("sizeof []", size_of);
	e.bind_function("# []", size_of);
	e.bind_function("str []", str);

	e.bind_function("[] , [+]", op_cat);

	/*	
	add_prefix_variadic(e, list, "list");
	add_prefix_variadic(e, eval, "eval");

	add_infix_variadic(e, op_and, "and");
	add_infix_variadic(e, op_or, "or");

	add_infix_variadic(e, op_cat, ",");
	*/
}

int main()
{
	context ctx;
	open_core_lib(ctx);
	ctx.unknown_var_value_getter() = [](context const&, std::string_view var) -> json {
		throw var;
		//return "<no var " + std::string{ var } + " found>";
	};
	ctx.unknown_func_handler() = [](context& c, std::vector<json> args) -> json {
		return c.report_error("function for call '" + c.array_to_string(args) + "' not found");
	};
	ctx.error_handler() = [](context const&, std::string_view err) -> std::string {
		throw err;
		//return fmt::format("<error: {}>", err);
	};

	ctx.set_user_var("kills", 2);
	fmt::print("{}\n", ctx.interpolate("Killed [.kills] [ [.kills == 1] ? monster. : monsters. ]"));

	ctx.bind_function("a []", if_then_else);
	ctx.bind_function("a [] b []", if_then_else);
	ctx.bind_function("a [] b [] c []", if_then_else);

	ctx.set_user_var("asd", { {"asd", "asd"} });
	std::cout << ctx.interpolate("hello world [if false then 5 else 7]") << "\n";

	ctx.interpolate("[.kills is number]");

	auto cctx = translator_new_context();
	translator_set_string_value(translator_user_var(cctx, "asd"), "booba");
	
	auto result = translator_interpolate_str(cctx, "hello world [.asd]");
	std::cout << result << "\n";
	free((void*)result);

	return 0;
}