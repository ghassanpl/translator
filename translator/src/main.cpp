#include "../include/ghassanpl/translator/translator.h"
#include "../src/format.h"

#include <iostream>
#include <sstream>
#include <gtest/gtest.h>

using namespace translator;
using namespace std::string_view_literals;

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
	e.eval_args(args, 2);
	e.assert_arg(args, 1, json::value_t::string);
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
	for (size_t i = 0; i < args.size(); ++i)
		last = e.eval(std::move(args[i]));
	return last;
}

/// Will evaluate each argument and return a list of the results
static inline json list(context& e, std::vector<json> args)
{
	e.eval_args(args);
	std::vector<json> result;
	for (size_t i = 0; i < args.size(); ++i)
		result.push_back(std::move(args[i]));
	return result;
}

/// Will evaluate each argument and concatenate them in a string
static inline json op_cat(context& e, std::vector<json> args)
{
	e.eval_args(args);
	std::string result;
	for (size_t i = 0; i < args.size(); ++i)
		result += e.value_to_string(args[i]);
	return result;
}

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
	e.bind_function("[] and [+]", op_and);
	e.bind_function("[] or [+]", op_or);
	e.bind_function("list [] , [*]", list);
	e.bind_function("cat [] , [*] and []", op_cat);

	/*	
	add_prefix_variadic(e, list, "list");
	add_prefix_variadic(e, eval, "eval");

	add_infix_variadic(e, op_cat, ",");
	*/
}


struct translator_f : public testing::Test {
	translator_f() {
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
	}

	context ctx;
};


TEST_F(translator_f, capi_works)
{
	auto cctx = translator_new_context();
	translator_set_string_value(translator_user_var(cctx, "asd"), "booba");

	auto result = translator_interpolate_str(cctx, "hello world [.asd]");
	EXPECT_EQ(result, "hello world booba"sv);
	free((void*)result);
}

TEST_F(translator_f, variadic_arguments_work)
{
	//println("{}", ctx.interpolate("[list]"));

	EXPECT_EQ("567", ctx.interpolate("[5,6,7]"));
	EXPECT_EQ("[5]", ctx.interpolate("[list 5]"));
	EXPECT_EQ("[5 6]", ctx.interpolate("[list 5,6]"));
	EXPECT_EQ("[5 6 7]", ctx.interpolate("[list 5,6,7]"));
	EXPECT_EQ("ad", ctx.interpolate("[cat a and d]"));
	EXPECT_EQ("abcd", ctx.interpolate("[cat a, b, c and d]"));
}

TEST_F(translator_f, user_vars_work)
{
	ctx.set_user_var("kills", 2);
	EXPECT_EQ("Killed 2 monsters.", ctx.interpolate("Killed [.kills] [ [.kills == 1] ? monster. : monsters. ]"));
	ctx.set_user_var("kills", 1);
	EXPECT_EQ("Killed 1 monster.", ctx.interpolate("Killed [.kills] [ [.kills == 1] ? monster. : monsters. ]"));
}

TEST_F(translator_f, preparse_works)
{
	auto parsed = ctx.parse("Killed [.kills] [ [.kills == 1] ? monster. : monsters. ]");
	ctx.set_user_var("kills", 2);
	EXPECT_EQ("Killed 2 monsters.", ctx.interpolate_parsed(parsed));
	ctx.set_user_var("kills", 1);
	EXPECT_EQ("Killed 1 monster.", ctx.interpolate_parsed(parsed));
	ctx.set_user_var("kills", 20);
	EXPECT_EQ("Killed 20 monsters.", ctx.interpolate_parsed(std::move(parsed)));
}

TEST_F(translator_f, unnamed_test_1)
{
	ctx.set_user_var("kills", 25);
	EXPECT_EQ("true", ctx.interpolate("[.kills is number]"));
}

TEST_F(translator_f, can_bind_different_functions_with_same_prefix)
{
	auto a = ctx.bind_function("a []", if_then_else);
	auto b = ctx.bind_function("a [] b []", if_then_else);
	auto c = ctx.bind_function("a [] b [] c []", if_then_else);
	EXPECT_NE(a, b);
	EXPECT_NE(b, c);
	EXPECT_NE(a, c);
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}