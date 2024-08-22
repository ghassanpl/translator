#pragma once

#include "translator_capi.h"
#include "detail/functions.h"
#include <vector>

namespace translator
{
	/// TODO: Should we use tl::expected and never throw exceptions?
	///		Instead of `binary` support, we'd have to use `json::value_t::binary` for errors (We could also use the first 1/2 bytes of binary to store type id)
	///		This would make the C api nice
	///		OR we could use JSON objects for additional types :P

	struct context : translator_context
	{
		explicit context(context* parent) noexcept;
		context() noexcept;
		
		context* parent() const noexcept { return (context*)parent_context; }
		context const* get_root_context() const noexcept { return parent() ? parent()->get_root_context() : this; }

		std::string interpolate(std::string_view str);
		
		/// TODO: Add these two functions to the C api
		json parse(std::string_view str);
		std::string interpolate_parsed(json const& parsed);
		std::string interpolate_parsed(json&& parsed);

		std::string report_error(std::string_view error) const
		{
			if (m_error_handler)
				return m_error_handler(*this, error);
			throw std::runtime_error(std::string{ error });
		}

		std::string value_to_string(json const& j) const;
		std::string array_to_string(std::vector<json> const& arguments) const;

		std::function<std::string(context const&, std::string_view)>& error_handler() { return m_error_handler; }

		/// Variables
		
		auto& context_variables() { return m_context_variables; }

		auto find_variable(std::string_view name) ->
			std::pair<context*, std::map<std::string, json, std::less<>>::iterator>;

		json user_var(std::string_view name);
		json& set_user_var(std::string_view name, json val, bool force_local = false);

		std::function<json(context&, std::string_view)>& unknown_var_value_getter() { return m_unknown_var_value_getter; }

		/// Functions

		auto& context_functions() { return m_functions_by_sig; }

		defined_function const* bind_function(std::string_view signature, eval_func func);

		std::vector<defined_function const*> find_functions(std::vector<json> const& arguments, bool only_in_local = false) const;
		std::vector<defined_function const*> find_functions_by_signature(std::string_view signature, bool only_in_local = false) const;
		std::vector<defined_function const*> find_closest(std::vector<json> const& arguments, bool only_in_local = false) const;
		
		eval_func& unknown_func_handler() { return m_unknown_func_handler.func; }

	public:

		struct e_scope_terminator {
			json result = json(json::value_t::discarded);
			virtual std::string type() const noexcept = 0;
			virtual ~e_scope_terminator() noexcept = default;
		};

		void assert_args(std::vector<json> const& args, size_t arg_count) const;
		void assert_args(std::vector<json> const& args, size_t min_args, size_t max_args) const;
		void assert_min_args(std::vector<json> const& args, size_t arg_count) const;

		json::value_t assert_arg(std::vector<json> const& args, size_t arg_num, json::value_t type = json::value_t::discarded) const;

		template <typename... T>
		std::enable_if_t<std::conjunction_v<std::is_same<T, nlohmann::json::value_t>...>, void> 
		assert_args(std::vector<json> args, T... arg_types)
		{
			static constexpr size_t arg_count = sizeof...(T);
			assert_args(args, arg_count);

			const auto types = std::array<nlohmann::json::value_t, arg_count>{ arg_types... };
			for (size_t i = 0; i < types.size(); ++i)
			{
				if (types[i] != json::value_t::discarded)
					assert_arg(args, i, types[i]);
			}
		}

		/// NOTE: Will invalidate value in `args` (but return an evaluated version of it)
		[[nodiscard]] json eval_arg(std::vector<json>& args, size_t n, json::value_t type = json::value_t::discarded);
		void eval_args(std::vector<json>& args, size_t n);
		void eval_args(std::vector<json>& args);

		template <typename... T>
		std::enable_if_t<std::conjunction_v<std::is_same<T, nlohmann::json::value_t>...>, void>
		eval_args(std::vector<json>& args, T... arg_types)
		{
			static constexpr size_t arg_count = sizeof...(T);
			assert_args(args, arg_count);

			const auto types = std::array<nlohmann::json::value_t, arg_count>{ arg_types... };
			for (size_t i = 0; i < types.size(); ++i)
			{
				if (types[i] != json::value_t::discarded)
					args[i] = eval_arg(args, i, types[i]);
			}
		}

		json eval(json const& val);
		json safe_eval(json const& value);

		json eval(json&& al);
		json safe_eval(json&& value);

		json eval_list(std::vector<json> args);

		auto& call_stack() const noexcept { return m_call_stack; }

	private:

		std::map<std::string, json, std::less<>> m_context_variables;

		defined_function m_unknown_func_handler;
		std::function<json(context&, std::string_view)> m_unknown_var_value_getter;
		std::function<std::string(context const&, std::string_view)> m_error_handler;

		std::function<std::string(context const&, json const&)> m_json_value_to_str_func;

		struct call_stack_element
		{
			defined_function const* actual_function = nullptr;
			std::string debug_call_sig;
		};
		std::vector<call_stack_element> m_call_stack;

		json call(defined_function const* func, std::vector<json> arguments, std::string call_frame_desc);

		tree_type m_prefix_function_tree;
		tree_type m_infix_function_tree;

		std::map<std::string, defined_function, std::less<>> m_functions_by_sig;

		void find_local_functions(
			tree_type const& in_tree,
			std::vector<json>::const_iterator name_it,
			std::vector<json>::const_iterator names_end,
			std::set<defined_function const*>& found
		) const;
		std::vector<defined_function const*> find_local_functions(std::vector<json> const& arguments) const;

		defined_function const* get_unknown_func_handler() const noexcept;

		std::string consume_c_string(std::string_view& strv) const;

		auto consume_atom(std::string_view& sexp_str) const -> nlohmann::json;
		auto consume_list(std::string_view& sexp_str) const -> nlohmann::json;
		auto consume_value(std::string_view& sexp_str) const -> nlohmann::json;
	};
}
