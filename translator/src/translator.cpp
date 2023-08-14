#include "../include/ghassanpl/translator/translator.hpp"
#include<string.h>
#include <charconv>

namespace translator
{

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

	char consume(std::string_view& str)
	{
		if (str.empty())
			return {};
		const auto result = str[0];
		str.remove_prefix(1);
		return result;
	}

	bool consume(std::string_view& str, char val)
	{
		if (starts_with(str, val))
		{
			str.remove_prefix(1);
			return true;
		}
		return false;
	}

	std::string_view consume_until(std::string_view& str, char c)
	{
		const auto start = str.data();
		while (!str.empty() && str[0] != c)
			str.remove_prefix(1);
		return std::string_view{ start, size_t(str.data() - start) };
	}

	template <typename FUNC>
	std::string_view consume_until(std::string_view& str, FUNC&& pred)
	{
		const auto start = str.data();
		while (!str.empty() && !pred(str[0]))
			str.remove_prefix(1);
		return std::string_view{ start, size_t(str.data() - start) };
	}

	static auto default_json_value_to_str_func(json const& j) -> json {
		switch (j.type())
		{
		case json::value_t::string: return j.get_ref<json::string_t const&>();
		case json::value_t::binary: return "<binary>";
		case json::value_t::null: return "<null>";
		default: return j.dump();
		}
	}

	context::context(context* parent)
		: json_value_to_str_func(&default_json_value_to_str_func)
	{
		parent_context = (translator_context*)parent;
		user_data = nullptr;
		if (parent)
			options = parent->options;
	}

	context::context()
		: context(nullptr)
	{
		options.parse_escapes = false;
		options.opening_delimiter = '[';
		options.closing_delimiter = ']';
		options.var_symbol = '.';
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
					return report_error("unterminated C string");

				switch (cp)
				{
				case 'n': result += '\n'; break;
				case '"': result += '"'; break;
				case '\'': result += '\''; break;
				case '\\': result += '\\'; break;
				default:
					return report_error("unknown escape character");
				}
			}
			else
			{
				result += cp;
			}

			if (view.empty())
				return report_error("unterminated C string");
		}

		if (!consume(view, delimiter))
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
		std::string_view result = consume_until(sexp_str, [=](auto ch) {
			return translator::isspace(ch) ||
				ch == ']' ||
				ch == ',';
			});

		if (result == "true") return true;
		if (result == "false") return false;
		if (result == "null") return nullptr;

		/// Try paring as number
		{
			nlohmann::json::number_integer_t num_result{};
			const auto fcres = std::from_chars(result.data(), result.data() + result.size(), num_result);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
			{
				trim_whitespace_left(sexp_str);
				return num_result;
			}
		}

		{
			nlohmann::json::number_unsigned_t num_result{};
			const auto fcres = std::from_chars(result.data(), result.data() + result.size(), num_result);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
			{
				trim_whitespace_left(sexp_str);
				return num_result;
			}
		}

		{
			nlohmann::json::number_float_t num_result{};
			const auto fcres = std::from_chars(result.data(), result.data() + result.size(), num_result);
			if (fcres.ec == std::errc{} && fcres.ptr == sexp_str.data()) /// If we ate the ENTIRE number
			{
				trim_whitespace_left(sexp_str);
				return num_result;
			}
		}

		trim_whitespace_left(sexp_str);
		return result;
	}

	auto context::consume_list(std::string_view& sexp_str) const -> nlohmann::json
	{
		nlohmann::json result = nlohmann::json::array();
		trim_whitespace_left(sexp_str);
		while (!sexp_str.empty() && !starts_with(sexp_str, ']'))
		{
			result.push_back(consume_value(sexp_str));
			trim_whitespace_left(sexp_str);
		}
		consume(sexp_str, ']');
		return result;
	}

	auto context::consume_value(std::string_view& sexp_str) const -> nlohmann::json
	{
		trim_whitespace_left(sexp_str);
		if (consume(sexp_str, '['))
			return consume_list(sexp_str);
		return consume_atom(sexp_str);
	}

	std::pair<context*, context::user_storage_iterator> context::find_in_user_storage(std::string_view name)
	{
		if (auto it = user_storage.find(name); it != user_storage.end())
			return std::pair{ this, it };
		else
			return parent_context ? parent()->find_in_user_storage(name) : std::pair<context*, decltype(it)>{};
	}

	std::string context::interpolate(std::string_view str)
	{
		std::string result;
		while (!str.empty())
		{
			result += consume_until(str, '[');
			if (str.empty()) break;
			str.remove_prefix(1);
			if (consume(str, '['))
				result += '[';
			else
			{
				json call = consume_list(str);
				json call_result = safe_eval(std::move(call));
				result += json_to_string(call_result);
			}
		}
		return result;
	}

	json context::user_var(std::string_view name)
	{
		auto [owning_context, iterator] = find_in_user_storage(name);
		if (owning_context)
			return iterator->second;
		return unknown_var_value_getter ? unknown_var_value_getter(*this, name) : nullptr;
	}

	json& context::set_user_var(std::string_view name, json val, bool force_local)
	{
		auto* storage = &user_storage;
		if (!force_local)
		{
			auto [owning_store, it] = find_in_user_storage(name);
			if (owning_store)
				storage = &owning_store->user_storage;
		}
		auto it = storage->find(name);
		if (it == storage->end())
			return storage->emplace(name, std::move(val)).first->second;
		return it->second = std::move(val);
	}

	context::eval_func const* context::get_unknown_func_eval() const noexcept
	{
		if (unknown_func_eval)
			return &unknown_func_eval;
		if (parent_context)
			return parent()->get_unknown_func_eval();
		return nullptr;
	}

	context::eval_func const* context::find_func(std::string_view name) const
	{
		if (auto it = functions.find(name); it != functions.end())
			return &it->second;
		if (parent_context)
			return parent()->find_func(name);
		return get_unknown_func_eval();
	}

	json context::eval_call(std::vector<json> args)
	{
		if (args.empty())
			return nullptr;

		std::string funcname;
		std::vector<json> arguments;
		const auto args_count = args.size();
		const bool infix = (args_count % 2) == 1;

		if (args_count == 1)
		{
			if (args[0].is_string() && starts_with(args[0], '.'))
				return user_var(std::string_view{ args[0] }.substr(1));

			funcname = args[0];
			arguments = std::move(args);
		}
		else
		{
			arguments.emplace_back(); /// placeholder for name so we don't have to insert later
			if (infix)
			{
				arguments.push_back(std::move(args[0]));
				funcname += ':';
			}

			/// TODO: We need to put every variadic run of arguments into a separate list!
			std::string last_function_identifier;
			bool argument_variadic = false;
			for (size_t i = infix; i < args_count; i += 2)
			{
				auto const& function_identifier = args[i];
				if (function_identifier.is_string() && !std::string_view{ function_identifier }.empty())
				{
					if (last_function_identifier == std::string_view{ function_identifier })
					{
						if (!argument_variadic)
						{
							funcname.back() = '*';
							funcname += ':';
							argument_variadic = true;
						}
					}
					else
					{
						argument_variadic = false;
						funcname += function_identifier;
						funcname += ':';
						last_function_identifier = function_identifier;
					}
					arguments.push_back(std::move(args[i + 1]));
				}
				else
					return report_error("expected function name part, got: " + function_identifier.dump());
			}

			arguments[0] = funcname;
		}

		if (eval_func const* func = find_func(funcname))
			return (*func)(*this, std::move(arguments));

		return report_error("func with name '" + funcname + "' not found");
	}

	std::string context::json_to_string(json const& j) const
	{
		return json_value_to_str_func ? json_value_to_str_func(j) : j.dump();
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

		return eval_call(val.get_ref<json::array_t const&>());
	}

	json context::safe_eval(json const& val)
	{
		try
		{
			return eval(val);
		}
		catch (e_scope_terminator const& e)
		{
			return report_error("'" + e.type() + "' not in loop");
		}
	}
	json context::eval(json&& val)
	{
		if (val.is_string())
		{
			if (auto str = std::string_view{ val }; starts_with(str, options.var_symbol))
				return this->user_var(str);
		}

		if (!val.is_array())
			return val;

		return eval_call(std::move(val.get_ref<json::array_t&>()));
	}

	json context::safe_eval(json&& value)
	{
		try
		{
			return eval(std::move(value));
		}
		catch (e_scope_terminator const& e)
		{
			return report_error("'" + e.type() + "' not in loop");
		}
	}

}
