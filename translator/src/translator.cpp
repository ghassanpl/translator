#include "../include/ghassanpl/translator/translator.hpp"
#include "format.h"
#include <string.h>
#include <charconv>


namespace translator
{
	constexpr bool ismodifier(char cp) noexcept 
	{ 
		return cp == '?' || cp == '*' || cp == '+';
	}

	constexpr bool isspace(char32_t cp) noexcept { return (cp >= 9 && cp <= 13) || cp == 32; }

	constexpr void trim_whitespace_left(std::string_view& str) noexcept
	{
		str.remove_prefix(std::distance(str.begin(), std::find_if_not(str.begin(), str.end(), translator::isspace)));
	}

	constexpr bool starts_with(std::string_view str, std::string_view with) noexcept {
		const auto with_size = with.size();
		if (str.size() < with_size) return false;
		return std::char_traits<char>::compare(str.data(), with.data(), with_size) == 0;
	}

	constexpr bool starts_with(std::string_view str, char with) noexcept {
		return !str.empty() && str.front() == with;
	}

	char consume(std::string_view& str) noexcept
	{
		if (str.empty())
			return {};
		const auto result = str[0];
		str.remove_prefix(1);
		return result;
	}

	bool consume(std::string_view& str, char val) noexcept
	{
		if (starts_with(str, val))
		{
			str.remove_prefix(1);
			return true;
		}
		return false;
	}

	std::string_view consume_until(std::string_view& str, char c) noexcept
	{
		const auto start = str.data();
		while (!str.empty() && str[0] != c)
			str.remove_prefix(1);
		return std::string_view{ start, size_t(str.data() - start) };
	}

	template <typename FUNC>
	std::string_view consume_until(std::string_view& str, FUNC&& pred) noexcept
	{
		const auto start = str.data();
		while (!str.empty() && !pred(str[0]))
			str.remove_prefix(1);
		return std::string_view{ start, size_t(str.data() - start) };
	}

	auto default_json_value_to_str_func(context const& c, json const& j) -> std::string {
		switch (j.type())
		{
		case json::value_t::string: return j.get_ref<json::string_t const&>();
		case json::value_t::binary: return "<binary>";
		case json::value_t::null: return "<null>";
		case json::value_t::array: return c.array_to_string(j);
		default: return j.dump();
		}
	}

	context::context(context* parent) noexcept
		: m_json_value_to_str_func(&default_json_value_to_str_func)
	{
		parent_context = (translator_context*)parent;
		user_data = nullptr;
		if (parent)
			options = parent->options;
	}

	context::context() noexcept
		: context(nullptr)
	{
		translator_init_context_options(this);
	}

	std::string context::consume_c_string(std::string_view& strv) const
	{
		std::string result;

		if (strv.empty())
			return report_error("C string must start with delimiter");

		const char delimiter = consume(strv);
		std::string_view view = strv;

		while (view[0] != delimiter)
		{
			if (auto cp = consume(view); cp == '\\')
			{
				cp = consume(view);
				if (view.empty())
				{
					if (options.strict_syntax)
						return report_error("unterminated C string");
					return result;
				}

				switch (cp)
				{
				case 'n': result += '\n'; break;
				case '"': result += '"'; break;
				case '\'': result += '\''; break;
				case '\\': result += '\\'; break;
				default:
					if (options.strict_syntax)
						return report_error(format("unknown escape character '{}'", cp));
					result += cp;
					break;
				}
			}
			else
			{
				result += cp;
			}

			if (view.empty())
			{
				if (options.strict_syntax)
					return report_error("unterminated C string");
				break;
			}
		}

		if (!consume(view, delimiter) && options.strict_syntax)
			return report_error("C string must end with delimiter");

		strv = view;
		return result;
	}

	auto context::consume_atom(std::string_view& sexp_str) const -> nlohmann::json
	{
		trim_whitespace_left(sexp_str);

		/// Try string literals first
		if (starts_with(sexp_str, '\'') || starts_with(sexp_str, '"'))
			return consume_c_string(sexp_str);

		/// Try comma as a unique token
		if (consume(sexp_str, ','))
			return nlohmann::json(",");

		/// Then, try everything else until space, closing brace or comma
		const std::string_view result = consume_until(sexp_str, [=](auto ch) {
			return translator::isspace(ch) ||
				ch == options.closing_delimiter ||
				ch == ',';
			});

		/// TODO: We could have just a map from atom to `value` in `options`
		if (result == "true") return true;
		if (result == "false") return false;
		if (result == "null") return nullptr;

		if (options.hex_prefix && starts_with(result, options.hex_prefix))
		{
			const auto hex_data = result.substr(1);
			json::number_unsigned_t num_result{};
			const auto fcres = std::from_chars(hex_data.data(), hex_data.data() + hex_data.size(), num_result, 16);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
				return num_result;
		}

		/// Try paring as number
		{
			nlohmann::json::number_integer_t num_result{};
			const auto fcres = std::from_chars(result.data(), result.data() + result.size(), num_result);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
				return num_result;
		}

		{
			nlohmann::json::number_unsigned_t num_result{};
			const auto fcres = std::from_chars(result.data(), result.data() + result.size(), num_result);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
				return num_result;
		}

		{
			nlohmann::json::number_float_t num_result{};
			const auto fcres = std::from_chars(result.data(), result.data() + result.size(), num_result);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
				return num_result;
		}

		return result;
	}

	auto context::consume_list(std::string_view& sexp_str, bool require_closing_delim) const -> nlohmann::json
	{
		nlohmann::json result = nlohmann::json::array();
		trim_whitespace_left(sexp_str);
		while (!sexp_str.empty() && !starts_with(sexp_str, options.closing_delimiter))
		{
			result.push_back(consume_value(sexp_str));
			trim_whitespace_left(sexp_str);
		}
		auto closing = consume(sexp_str, options.closing_delimiter);
		if (require_closing_delim && options.strict_syntax && !closing)
			return report_error("list must end with closing delimiter");
		return result;
	}

	auto context::consume_value(std::string_view& sexp_str) const -> nlohmann::json
	{
		trim_whitespace_left(sexp_str);
		if (consume(sexp_str, options.opening_delimiter))
			return consume_list(sexp_str);
		return consume_atom(sexp_str);
	}

	std::pair<context*, std::map<std::string, json, std::less<>>::iterator> context::find_variable(std::string_view name)
	{
		if (auto it = m_context_variables.find(name); it != m_context_variables.end())
			return std::pair{ this, it };
		else
			return parent_context ? parent()->find_variable(name) : std::pair<context*, decltype(it)>{};
	}

	std::string context::interpolate(std::string_view str)
	{
		std::string result;
		while (!str.empty())
		{
			result += consume_until(str, options.opening_delimiter);
			if (str.empty()) break;
			str.remove_prefix(1);
			if (consume(str, options.opening_delimiter))
				result += options.opening_delimiter;
			else
			{
				json call = consume_list(str);
				json call_result = safe_eval(std::move(call));
				result += value_to_string(call_result);
			}
		}
		return result;
	}

	json context::parse(std::string_view str) const
	{
		json result = json::array();
		std::string latest_str;
		while (!str.empty())
		{
			latest_str += consume_until(str, options.opening_delimiter);
			if (str.empty()) break;
			str.remove_prefix(1);
			if (consume(str, options.opening_delimiter))
				latest_str += options.opening_delimiter;
			else
			{
				if (!latest_str.empty())
					result.push_back(std::exchange(latest_str, {}));
				result.push_back(std::move(consume_list(str)));
			}
		}
		if (!latest_str.empty())
			result.push_back(std::move(latest_str));
		return result;
	}

	json context::parse_call(std::string_view str) const
	{
		auto result = consume_list(str, false);
		if (!str.empty())
			return report_error("Additional tokens after end of list: " + std::string{ str });
		return result;
	}

	std::string context::interpolate_parsed(json const& parsed)
	{
		std::string result;
		if (!parsed.is_array())
			return report_error("Invalid parsed value: must be an array of strings or call arrays");
		for (auto const& r : parsed)
		{
			if (r.is_array())
			{
				json call_result = safe_eval(r);
				result += value_to_string(call_result);
			}
			else if (r.is_string())
				result += r.get_ref<json::string_t const&>();
			else
				return report_error("Invalid parsed value: must be an array of strings or call arrays");
		}
		return result;
	}

	std::string context::interpolate_parsed(json&& parsed)
	{
		std::string result;
		if (!parsed.is_array())
			return report_error("Invalid parsed value: must be an array of strings or call arrays");
		for (auto&& r : std::move(parsed))
		{
			if (r.is_array())
			{
				json call_result = safe_eval(std::move(r));
				result += value_to_string(call_result);
			}
			else if (r.is_string())
			{
				std::string el = std::move(r.get_ref<json::string_t&>());
				result += std::move(el);
			}
			else
				return report_error("Invalid parsed value: must be an array of strings or call arrays");
		}
		return result;
	}

	std::string context::report_error(std::string_view error) const
	{
		if (m_error_handler)
			return m_error_handler(*this, error);
		throw std::runtime_error(std::string{ error });
	}

	json context::user_var(std::string_view name)
	{
		auto [owning_context, iterator] = find_variable(name);
		if (owning_context)
			return iterator->second;
		return m_unknown_var_value_getter ? m_unknown_var_value_getter(*this, name) : nullptr;
	}

	json& context::set_user_var(std::string_view name, json val, bool force_local)
	{
		auto* storage = &m_context_variables;
		if (!force_local)
		{
			auto [owning_store, it] = find_variable(name);
			if (owning_store)
				storage = &owning_store->m_context_variables;
		}
		auto it = storage->find(name);
		if (it == storage->end())
			return storage->emplace(name, std::move(val)).first->second;
		return it->second = std::move(val);
	}

	json const& context::user_var(std::string_view name, json const& val_if_not_found)
	{
		auto [owning_context, iterator] = find_variable(name);
		if (owning_context)
			return iterator->second;
		return val_if_not_found;
	}

	json context::eval_list(std::vector<json> args)
	{
		if (args.empty())
			return nullptr;

		if (args.size() == 1 && args[0].is_string() && !args[0].empty() && std::string_view{ args[0] } [0] == options.var_symbol)
			return user_var(std::string_view{ args[0] }.substr(1));

		auto function_candidates = this->find_functions(args);
		if (function_candidates.empty())
		{
			if (auto unknown = get_unknown_func_handler())
			{
				return call(unknown, std::move(args), array_to_string(args));
			}
			else
			{
				std::vector<std::string> signatures; /// = find_closest(args) | transform(to_signature)
				if (signatures.empty())
					return report_error(format("function for call '{}' not found", array_to_string(args)));
				else
					return report_error(format("function for call '{}' not found, did you mean:\n{}?", array_to_string(args), join(signatures, "?\n")));
			}
		}
		else if (function_candidates.size() > 1)
		{
			std::vector<std::string> signatures;
			for (auto& candidate : function_candidates)
				signatures.push_back(candidate->signature);
			return report_error(format("multiple functions for call '{}' found: {}", array_to_string(args), 
				join(signatures, ", ", [](auto sig) { return format("[{}]", sig); })));
		}
		const auto func = function_candidates[0];
		assert(func);

		std::string call_frame_desc;
		if (options.maintain_call_stack && options.call_stack_store_call_string)
			call_frame_desc = array_to_string(args);

		const auto elem_count = args.size();
		const bool infix = (elem_count % 2) == 1;
		//std::vector<json const*> parameter_names;
		std::vector<json> arguments;
		if (elem_count > 1) /// Hack for calling a one-optional-param prefix function with no parameters
		{
			arguments.reserve(elem_count / 2 + infix);
			if (infix)
				arguments.push_back(std::move(args[0]));
		}

		/// TODO: We should match the number of arguments to the number of parameters, even for optional parameters:
		/// For '?' just substitute null (or better yet, discarded)
		/// For '*' and '+', make the argument an array of all the arguments for that parameter
		/// To facilitate that, the find_functions function should not only return the candidates,
		///	but also (if exactly 1 function was found) the number of arguments that should be eaten for each parameter (like std::vector<size_t>)

		for (size_t i = infix; i < elem_count; i += 2)
		{
			//parameter_names.push_back(&args[i]);
			arguments.push_back(std::move(args[i + 1]));
		}
	
		return call(func, std::move(arguments), std::move(call_frame_desc));
	}

	json context::call(defined_function const* func, std::vector<json> arguments, std::string call_frame_desc)
	{
		assert(func);
		assert(func->func);

		if (options.maintain_call_stack)
		{
			auto& call_frame = m_call_stack.emplace_back();
			call_frame.actual_function = func;
			if (options.call_stack_store_call_string)
				call_frame.debug_call_sig = std::move(call_frame_desc);
		}

		/// TODO: This
		//auto prev_parameter_names = std::exchange(m_parameter_names, &parameters);
		json result;
		try
		{
			result = func->func(*this, std::move(arguments));
		}
		catch (...)
		{
			//m_parameter_names = prev_parameter_names;
			throw;
		}

		if (options.maintain_call_stack)
			m_call_stack.pop_back();

		return result;
	}

	std::string context::value_to_string(json const& j) const
	{
		if (m_json_value_to_str_func)
			return m_json_value_to_str_func(*this, j);
		return j.dump();
	}

	std::string context::array_to_string(std::vector<json> const& arguments) const
	{
		return format("{}{}{}", options.opening_delimiter, join(arguments, " ", [this](json const& v) { return value_to_string(v); }), options.closing_delimiter);
	}


	/// TODO: These functions use array_to_string(args) to determine the function signature, which is not correct
	
	void context::assert_args(std::vector<json> const& args, size_t arg_count) const
	{
		if (args.size() != arg_count)
			throw std::runtime_error{ report_error(format("function {} requires exactly {} arguments, {} given", array_to_string(args), arg_count, args.size() - 1)) };
	}

	void context::assert_args(std::vector<json> const& args, size_t min_args, size_t max_args) const
	{
		if (args.size() < min_args && args.size() >= max_args)
			throw std::runtime_error{ report_error(format("function {} requires between {} and {} arguments, {} given", array_to_string(args), min_args, max_args, args.size())) };
	}

	void context::assert_min_args(std::vector<json> const& args, size_t arg_count) const
	{
		if (args.size() < arg_count)
			throw std::runtime_error{ report_error(format("function {} requires at least {} arguments, {} given", array_to_string(args), arg_count, args.size())) };
	}

	json::value_t context::assert_arg(std::vector<json> const& args, size_t arg_num, json::value_t type) const
	{
		if (arg_num >= args.size())
			throw std::runtime_error{ report_error(format("function {} requires {} arguments, {} given", array_to_string(args), arg_num, args.size())) };

		return assert_arg(args[arg_num], args, arg_num, type);
	}

	json::value_t context::assert_arg(json const& value, std::vector<json> const& args, size_t arg_num, json::value_t type) const
	{
		if (type != json::value_t::discarded && value.type() != type)
		{
			if (options.maintain_call_stack)
			{
				auto& entry = m_call_stack.back();

				throw std::runtime_error{ report_error(format("argument {} to function {} must be of type {}, {} given",
					arg_num, entry.actual_function->signature, json(type).type_name(), value.type_name())) };
			}
			throw std::runtime_error{ report_error(format("argument {} to function {} must be of type {}, {} given",
				arg_num, array_to_string(args), json(type).type_name(), value.type_name())) };
		}
		return value.type();
	}

	json context::eval_arg_steal(std::vector<json>& args, size_t arg_num, json::value_t type)
	{
		if (arg_num >= args.size())
			throw std::runtime_error{ report_error(format("function {} requires {} arguments, {} given", array_to_string(args), arg_num, args.size())) };

		auto result = eval(std::move(args[arg_num]));
		assert_arg(result, args, arg_num, type);
		return result;
	}

	json context::eval_arg_copy(std::vector<json> const& args, size_t arg_num, json::value_t type)
	{
		if (arg_num >= args.size())
			throw std::runtime_error{ report_error(format("function {} requires {} arguments, {} given", array_to_string(args), arg_num, args.size())) };

		auto result = eval(args[arg_num]);
		assert_arg(result, args, arg_num, type);
		return result;
	}

	json& context::eval_arg_in_place(std::vector<json>& args, size_t arg_num, json::value_t type)
	{
		args[arg_num] = eval(std::move(args[arg_num]));
		assert_arg(args, arg_num, type);
		return args[arg_num];
	}

	void context::eval_args(std::vector<json>& args, size_t n)
	{
		assert_args(args, n);
		for (auto& arg: args)
			arg = eval(std::move(arg));
	}

	void context::eval_args(std::vector<json>& args)
	{
		eval_args(args, args.size());
	}

	json context::eval(json const& val)
	{
		if (val.is_string())
		{
			if (auto str = std::string_view{ val }; starts_with(str, options.var_symbol))
				return this->user_var(str.substr(1));
		}

		if (!val.is_array())
			return val;

		return eval_list(val.get_ref<json::array_t const&>());
	}

	json context::safe_eval(json const& val)
	{
		return safe_eval(json{ val });
	}

	json context::eval(json&& val)
	{
		if (val.is_string())
		{
			if (auto str = std::string_view{ val }; starts_with(str, options.var_symbol))
				return this->user_var(str.substr(1));
		}

		if (val.is_array())
			return eval_list(std::move(val.get_ref<json::array_t&>()));

		return val;
	}

	json context::safe_eval(json&& value)
	{
		try
		{
			return eval(std::move(value));
		}
		catch (e_scope_terminator const& e)
		{
			return report_error(format("'{}' not in loop", e.type()));
		}
	}

}
