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
	if (is_true(e.eval_arg_steal(args, 0)))
		return e.eval_arg_steal(args, 1);
	return e.eval_arg_steal(args, 2);
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
		left = e.eval_arg_steal(args, i);
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
		left = e.eval_arg_steal(args, i);
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
	const auto val = e.eval_arg_steal(args, 0);
	return val.type_name();
}

static inline json size_of(context& e, std::vector<json> args) {
	const auto val = e.eval_arg_steal(args, 0);
	const json& j = val;
	return j.is_string() ? j.get_ref<json::string_t const&>().size() : j.size();
}

static inline json str(context& e, std::vector<json> args)
{
	auto arg = e.eval_arg_steal(args, 0);
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
	
	e.bind_function("if arg then arg else arg", if_then_else);
	e.bind_function("arg ? arg : arg", if_then_else);

	e.bind_function("arg == arg", op_eq);
	e.bind_function("arg eq arg", op_eq);
	e.bind_function("arg != arg", op_neq);
	e.bind_function("arg neq arg", op_neq);
	e.bind_function("arg > arg", op_gt);
	e.bind_function("arg gt arg", op_gt);
	e.bind_function("arg >= arg", op_ge);
	e.bind_function("arg ge arg", op_ge);
	e.bind_function("arg < arg", op_lt);
	e.bind_function("arg lt arg", op_lt);
	e.bind_function("arg <= arg", op_le);
	e.bind_function("arg le arg", op_le);
	e.bind_function("not arg", op_not);

	e.bind_function("arg + arg", op_plus);
	e.bind_function("arg - arg", op_minus);
	e.bind_function("arg * arg", op_mul);
	e.bind_function("arg / arg", op_div);
	e.bind_function("arg % arg", op_mod);

	e.bind_function("arg is arg", op_is);
	e.bind_function("type-of arg", type_of);
	e.bind_function("typeof arg", type_of);
	e.bind_function("size-of arg", size_of);
	e.bind_function("sizeof arg", size_of);
	e.bind_function("# arg", size_of);
	e.bind_function("str arg", str);

	e.bind_function("arg , arg+", op_cat);
	e.bind_function("arg and arg+", op_and);
	e.bind_function("arg or arg+", op_or);
	e.bind_function("list arg , arg*", list);
	//e.bind_function("list", list);
	e.bind_function("cat arg , arg* and arg", op_cat);
	
	///e.bind_function("interpolate arg with arg?", 
	e.bind_function("interpolate arg", [](context& e, std::vector<json> args) -> json {
		return e.interpolate(e.eval_arg_steal(args, 0, json::value_t::string));
	});
	e.bind_function("parse arg", [](context& e, std::vector<json> args) -> json {
		return e.parse(e.eval_arg_steal(args, 0, json::value_t::string));
	});
	e.bind_function("run arg", [](context& e, std::vector<json> args) -> json {
		return e.interpolate_parsed(e.eval_arg_steal(args, 0, json::value_t::array));
	});

	/// TODO: 'default' should be optional
	///e.bind_function("match arg with arg+ default arg?", [](context& e, std::vector<json> args) -> json {
	///e.bind_function("match arg [with arg]+ [default arg]?", [](context& e, std::vector<json> args) -> json {
	///e.bind_function("match arg [with arg]+ default arg", [](context& e, std::vector<json> args) -> json {
	e.bind_function("match arg with arg* default arg", [](context& e, std::vector<json> args) -> json {
		e.assert_min_args(args, 2);
		auto val = e.eval_arg_steal(args, 0);
		for (size_t i = 1; i < args.size() - 1; i++)
		{
			e.assert_arg(args, i, json::value_t::array);
			auto& match_case = args[i];
			if (match_case.size() < 2)
				return e.report_error(format("case #{} in match must have at least 2 arguments", i));
			auto case_val = e.eval(move(match_case[0]));
			if (val == case_val)
				return e.eval(move(match_case[1]));
		}
		return e.eval_arg_steal(args, args.size() - 1);
	});
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
			throw std::runtime_error(std::string{ err });
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
	auto a = ctx.bind_function("a arg", if_then_else);
	auto b = ctx.bind_function("a arg b arg", if_then_else);
	auto c = ctx.bind_function("a arg b arg c arg", if_then_else);
	EXPECT_NE(a, b);
	EXPECT_NE(b, c);
	EXPECT_NE(a, c);
}

TEST_F(translator_f, fluent_features_lol)
{
	constexpr auto str = 
	R"([.userName] [.photoCount 
		1? "added a new photo" 
		else ["added ", .photoCount, " new photos"]
	] to [
		match .userGender
		with [male "his stream"]
		with [female "her stream"]
		default "their stream"
	].)";
	ctx.options.maintain_call_stack = true;
	ctx.bind_function("arg 1? arg else arg", [](context& e, std::vector<json> args) -> json {
		int num = e.eval_arg_steal(args, 0, json::value_t::number_integer);
		if (num == 1)
			return e.eval_arg_steal(args, 1);
		return e.eval_arg_steal(args, 2);
	});
	ctx.error_handler() = [&](context const&, std::string_view err) -> std::string {
		EXPECT_FALSE(true) << err;
		return std::string{ err };
	};
	ctx.set_user_var("userName", "Ghassan");
	ctx.set_user_var("photoCount", 1);
	ctx.set_user_var("userGender", "female");
	EXPECT_EQ("Ghassan added a new photo to her stream.", ctx.interpolate(str));
	ctx.set_user_var("userName", "Steve");
	ctx.set_user_var("photoCount", 3);
	ctx.set_user_var("userGender", "male");
	EXPECT_EQ("Steve added 3 new photos to his stream.", ctx.interpolate(str));
	ctx.set_user_var("userName", "Xen");
	ctx.set_user_var("photoCount", 0);
	ctx.set_user_var("userGender", "non-binary");
	EXPECT_EQ("Xen added 0 new photos to their stream.", ctx.interpolate(str));
}

TEST_F(translator_f, defining_functions_in_code_works)
{
	ctx.eval(ctx.parse_call("define [a <> b] as [not [.a == .b]]"));
	ctx.eval(ctx.parse_call("[a != b] => [not [.a == .b]]"));
	EXPECT_EQ(ctx.interpolate("[3 <> 2]"), "true");
	EXPECT_EQ(ctx.interpolate("[3 <> 3]"), "false");
}

void open_repl_lib(context& c)
{
	c.bind_function("arg = arg", [](context& e, std::vector<json> args) -> json {
		e.assert_args(args, json::value_t::string, json::value_t::discarded);
		return format("{} => {}", e.value_to_string(args[0]), e.value_to_string(e.set_user_var(args[0], e.eval_arg_steal(args, 1))));
	});
	c.bind_function("imode", [](context& e, std::vector<json> args) -> json {
		return e.set_user_var("$mode", false);
	});
	c.bind_function("emode", [](context& e, std::vector<json> args) -> json {
		return e.set_user_var("$mode", true);
	});
	c.set_user_var("$mode", true);
}

void repl()
{
	translator::context ctx;
	open_core_lib(ctx);
	open_repl_lib(ctx);
	ctx.unknown_var_value_getter() = [](context const& ctx, std::string_view var) -> json {
		return ctx.report_error(format("variable '{}' not found", var));
	};
	ctx.error_handler() = [](context const&, std::string_view err) -> std::string {
		throw std::runtime_error(std::string{ err });
	};
	std::string in;
	bool exec_mode = false;
	while (print("{}> ", (exec_mode = is_true(ctx.user_var("$mode"))) ? "E" : "I"), std::getline(std::cin, in))
	{
		if (in == "exit")
			break;
		try
		{
			if (exec_mode)
			{
				in.insert(0, 1, '[');
				in.push_back(']');
				std::string_view insv = in;
				json call = ctx.consume_value(insv);
				if (!insv.empty())
					throw std::runtime_error(format("unexpected token: '{}'", insv));
				json call_result = ctx.safe_eval(std::move(call));
				println("{}", ctx.value_to_string(call_result));
			}
			else
			{
				println("{}", ctx.interpolate(in));
			}
		}
		catch (std::exception const& e)
		{
			println("Error: {}", e.what());
		}
	}
}

TEST_F(translator_f, noarg_functions_work)
{
	ctx.bind_function("ass", [](context& e, std::vector<json> args) -> json {
		return "asstastic";
	});
	EXPECT_EQ(ctx.interpolate("[ass]"), "asstastic");
}

TEST_F(translator_f, variadic_functions_are_sanely_defined)
{
	EXPECT_NO_THROW(ctx.bind_function("ignoring arg? print arg", [](context& e, std::vector<json> args) -> json { return nullptr; }));
	EXPECT_NO_THROW(ctx.interpolate("[print arg]"));
	EXPECT_NO_THROW(ctx.interpolate("[ignoring 'asd' print arg]"));

	ctx.bind_function("boop arg+", [](context& e, std::vector<json> args) -> json {
		return e.array_to_string(args);
	});
	EXPECT_THROW(ctx.interpolate("[boop]"), std::runtime_error);
	EXPECT_EQ(ctx.interpolate("[boop a]"), "[a]");
	EXPECT_EQ(ctx.interpolate("[boop a boop b]"), "[a b]");
	EXPECT_EQ(ctx.interpolate("[boop a boop b boop c]"), "[a b c]");

	ctx.bind_function("boop2 arg+ or arg", [](context& e, std::vector<json> args) -> json { 
		return e.array_to_string(args);;
	});
	EXPECT_THROW(ctx.interpolate("[boop2 a]"), std::runtime_error);
	EXPECT_THROW(ctx.interpolate("[or a]"), std::runtime_error);
	EXPECT_EQ(ctx.interpolate("[boop2 a or e]"), "[a e]");
	EXPECT_EQ(ctx.interpolate("[boop2 a boop2 b or e]"), "[a b e]");
	EXPECT_THROW(ctx.interpolate("[boop2 a or e or c]"), std::runtime_error);

	EXPECT_NO_THROW((ctx.bind_function("boop3 arg*", [](context& e, std::vector<json> args) -> json { return e.array_to_string(args); })));
	EXPECT_EQ(ctx.interpolate("[]"), "<null>");
	EXPECT_NO_THROW(ctx.interpolate("[boop3]"));
	EXPECT_EQ(ctx.interpolate("[boop3 5]"), "[5]");
	EXPECT_EQ(ctx.interpolate("[boop3 5 boop3 ass]"), "[5 ass]");
	EXPECT_THROW(ctx.interpolate("[boop3 5 boop3 ass or ess]"), std::runtime_error);

	EXPECT_NO_THROW(ctx.bind_function("boop4 arg* or arg", [](context& e, std::vector<json> args) -> json { return nullptr; }));
	EXPECT_NO_THROW(ctx.interpolate("[or arg]"));
	EXPECT_THROW(ctx.bind_function("arg* : arg*", [&](context& e, std::vector<json> args)->json { return nullptr; }), std::runtime_error);
	EXPECT_THROW(ctx.bind_function("arg* : arg", [&](context& e, std::vector<json> args)->json { return nullptr; }), std::runtime_error);
	
	ctx.bind_function("arg : arg*", [&](context& e, std::vector<json> args)->json { return nullptr; });
	ctx.interpolate("[a : b]");
	ctx.interpolate("[a : b : c]");
	ctx.interpolate("[a : b : c : d]");
	EXPECT_THROW(ctx.interpolate("[a : b : c : d or e]"), std::runtime_error);

	ctx.bind_function("arg : arg : arg", [&](context& e, std::vector<json> args)->json { return nullptr; });
	EXPECT_THROW(ctx.interpolate("[a : b : c]"), std::runtime_error);

	ctx.bind_function("list arg , arg*", list);
	EXPECT_THROW(ctx.interpolate("[list a, b, c or d]"), std::runtime_error);

	ctx.bind_function("find text or file?", [&](context& e, std::vector<json> args)->json { return nullptr; });
	EXPECT_NO_THROW(ctx.interpolate("[find hello]"));
	EXPECT_NO_THROW(ctx.interpolate("[find hello or ass]"));
	EXPECT_THROW(ctx.interpolate("[find hello or ass or dupa]"), std::runtime_error);
	EXPECT_THROW(ctx.interpolate("[find hello or ass and dupa]"), std::runtime_error);
}

TEST_F(translator_f, single_optional_should_work)
{
	/// TODO: This is a special case, and I'm not sure I'm keen on actually implementing it
	///		Though we could just make it syntactic sugar, that is,
	///		bind "hello arg?" as both "hello" and "hello arg", and
	///		bind "hello arg*" as both "hello" and "hello arg+"
	EXPECT_NO_THROW(ctx.bind_function("hello arg?", [](context& e, std::vector<json> args) -> json {
		return e.array_to_string(args);
	}));
	EXPECT_EQ(ctx.interpolate("[hello]"), "[]");
	EXPECT_EQ(ctx.interpolate("[hello world]"), "[world]");
	EXPECT_THROW(ctx.interpolate("[oofa]"), std::runtime_error);
	EXPECT_NO_THROW(ctx.bind_function("hello", [](context& e, std::vector<json> args) -> json { return e.array_to_string(args); }));
	EXPECT_THROW(ctx.interpolate("[hello]"), std::runtime_error);

	EXPECT_NO_THROW(ctx.bind_function("hullo arg*", [](context& e, std::vector<json> args) -> json { return e.array_to_string(args); }));
	EXPECT_EQ(ctx.interpolate("[hullo]"), "[]");
	EXPECT_EQ(ctx.interpolate("[hullo world]"), "[world]");
	EXPECT_EQ(ctx.interpolate("[hullo world hullo ass]"), "[world ass]");
	EXPECT_EQ(ctx.interpolate("[hullo world hullo ass hullo bees]"), "[world ass bees]");
	EXPECT_THROW(ctx.interpolate("[hullo world hullo ass hullo bees or not]"), std::runtime_error);
	EXPECT_THROW(ctx.interpolate("[oofa]"), std::runtime_error);
	EXPECT_NO_THROW(ctx.bind_function("hullo", [](context& e, std::vector<json> args) -> json { return e.array_to_string(args); }));
	EXPECT_THROW(ctx.interpolate("[hullo]"), std::runtime_error);
}

TEST_F(translator_f, simple_bindings_work)
{
	//ctx.bind_simple_function("arg + arg", [](int a, int b) { return a + b; });
	//EXPECT_EQ(ctx.interpolate("[hello]"), "world");
}

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);
	auto result = RUN_ALL_TESTS();
	repl();
	return result;
}