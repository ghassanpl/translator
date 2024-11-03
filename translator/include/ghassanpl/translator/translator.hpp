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
		
		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Main API
		/// ////////////////////////////////////////////////////////////////////////// ///

		std::string interpolate(std::string_view str);
		
		/// TODO: Add these functions to the C api
		json parse(std::string_view str) const;
		json parse_call(std::string_view str) const;
		std::string interpolate_parsed(json const& parsed);
		std::string interpolate_parsed(json&& parsed);

		using error_handler_func = std::function<std::string(context const&, std::string_view)>;

		error_handler_func& error_handler() { return m_error_handler; }

		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Variables
		/// ////////////////////////////////////////////////////////////////////////// ///
		
		auto& context_variables() { return m_context_variables; }
		auto& own_variables() { return m_context_variables; }

		auto find_variable(std::string_view name) -> std::pair<context*, std::map<std::string, json, std::less<>>::iterator>;

		/// If the variable does not exist, will call the function set via unknown_var_value_getter
		json user_var(std::string_view name);

		/// Will return reference to the variable value if it exists, otherwise will return the provided value
		json const& user_var(std::string_view name, json const& val_if_not_found);

		json& set_user_var(std::string_view name, json val, bool force_local = false);
		
		using var_value_getter_func = std::function<json(context&, std::string_view)>;

		/// Gets a mutable reference to the callback that will be called when a variable is not found
		var_value_getter_func& unknown_var_value_getter() { return m_unknown_var_value_getter; }

		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Functions
		/// ////////////////////////////////////////////////////////////////////////// ///
		
		using eval_func = std::function<json(context&, std::vector<json>)>;

		auto& context_functions() const { return m_functions_by_sig; }
		auto& own_functions() const { return m_functions_by_sig; }

		defined_function const* bind_function(std::string_view signature, eval_func func
			///, std::source_location loc = std::source_location::current()
		);
		/// TODO: void unbind_function(defined_function const*);
		/// TODO: void unbind_function(std::string_view signature);
		/// TODO: void rebind_function(defined_function const*, eval_func func);
		/// TODO: void rebind_function(std::string_view, eval_func func);
		/// TODO: void unbind_all_functions();
		/// TODO: void unbind_functions(std::span<defined_function const*>);

		std::vector<defined_function const*> find_functions(std::vector<json> const& arguments, bool only_in_local = false) const;
		/// TODO: std::vector<defined_function const*> find_functions_by_signature(std::string_view signature, bool only_in_local = false) const;
		/// TODO: std::vector<defined_function const*> find_closest(std::vector<json> const& arguments, bool only_in_local = false) const;
		
		eval_func& unknown_func_handler() { return m_unknown_func_handler.func; }

		std::string report_error(std::string_view error) const;

		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Functions past here are for
		/// more advanced use
		/// ////////////////////////////////////////////////////////////////////////// ///
	public:

		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Helper functions for writing 
		/// handler functions
		/// ////////////////////////////////////////////////////////////////////////// ///

		struct e_scope_terminator {
			json result = json::value_t::discarded;
			virtual std::string type() const noexcept = 0;
			virtual ~e_scope_terminator() noexcept = default;
		};

		/// Reports and throws error if there aren't exactly `arg_count` arguments
		void assert_args(std::vector<json> const& args, size_t arg_count) const;

		/// Reports and throws error if there aren't between `min_args` and `max_args` arguments
		void assert_args(std::vector<json> const& args, size_t min_args, size_t max_args) const;

		/// Reports and throws error if there aren't at least `min_args` arguments
		void assert_min_args(std::vector<json> const& args, size_t arg_count) const;

		/// Reports and throws error if argument at index `arg_num` does not exist, or if it's a different type than type
		/// \param type If `json::value_t::discarded`, will not check the type
		json::value_t assert_arg(std::vector<json> const& args, size_t arg_num, json::value_t type = json::value_t::discarded) const;

		/// Reports and throws error if `value` is a different type than `type`; useful when you want to report `args` and `arg_num` 
		/// in the error, but want to specifically check value (i.e. after evaluating it)
		json::value_t assert_arg(json const& value, std::vector<json> const& args, size_t arg_num, json::value_t type) const;
		
		/// Reports and throws error if the values in args are of different types than the arguments given in the 
		/// `arg_types` pack.
		template <typename... T>
		std::enable_if_t<std::conjunction_v<std::is_same<T, nlohmann::json::value_t>...>, void> 
		assert_args(std::vector<json> const& args, T... arg_types) const
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

		[[nodiscard]] json eval_arg_steal(std::vector<json>& args, size_t arg_num, json::value_t type = json::value_t::discarded);
		[[nodiscard]] json eval_arg_copy(std::vector<json> const& args, size_t arg_num, json::value_t type = json::value_t::discarded);
		json& eval_arg_in_place(std::vector<json>& args, size_t arg_num, json::value_t type = json::value_t::discarded);

		/// Checks that there are exactly `n` values in `args`, and evaluates them in-place
		void eval_args(std::vector<json>& args, size_t n);

		/// Evaluates all values in `args` in-place
		void eval_args(std::vector<json>& args);

		/// Evaluates all values in `args` in-place, checking that they are of the types given in `arg_types`
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
					args[i] = eval_arg_in_place(args, i, types[i]);
			}
		}

		json eval(json const& val);
		json safe_eval(json const& value);

		json eval(json&& al);
		json safe_eval(json&& value);

		json eval_list(std::vector<json> args);

		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Debugging
		/// ////////////////////////////////////////////////////////////////////////// ///

		struct call_stack_element
		{
			defined_function const* actual_function = nullptr;
			std::string debug_call_sig;
		};
		auto& call_stack() const noexcept { return m_call_stack; }

		/// ////////////////////////////////////////////////////////////////////////// ///
		/// Parsing; used by internal functions, but provided here for convenience
		/// ////////////////////////////////////////////////////////////////////////// ///
		
		auto consume_atom(std::string_view& sexp_str) const -> nlohmann::json;
		auto consume_list(std::string_view& sexp_str, bool require_closing_delim = true) const -> nlohmann::json;
		auto consume_value(std::string_view& sexp_str) const -> nlohmann::json;
		auto consume_c_string(std::string_view& strv) const -> std::string;

		std::string value_to_string(json const& j) const;
		std::string array_to_string(std::vector<json> const& arguments) const;

	private:

		std::map<std::string, json, std::less<>> m_context_variables;

		defined_function m_unknown_func_handler;
		var_value_getter_func m_unknown_var_value_getter;
		error_handler_func m_error_handler;

		std::function<std::string(context const&, json const&)> m_json_value_to_str_func;

		std::vector<call_stack_element> m_call_stack;

		json call(defined_function const* func, std::vector<json> arguments, std::string call_frame_desc);

		tree_type m_prefix_function_tree;
		tree_type m_infix_function_tree;

		std::map<std::string, defined_function, std::less<>> m_functions_by_sig; 
		/// TODO: or `std::map<std::string, std::pair<defined_function*, size_t>> for multiple signatures

		void find_local_functions(
			tree_type const& in_tree,
			std::vector<json>::const_iterator name_it,
			std::vector<json>::const_iterator names_end,
			std::set<defined_function const*>& found
		) const;
		std::vector<defined_function const*> find_local_functions(std::vector<json> const& arguments) const;

		defined_function* add_function(std::string signature, eval_func func);

		defined_function const* get_unknown_func_handler() const noexcept;
	};
}
