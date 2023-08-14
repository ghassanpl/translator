#pragma once

#include "translator_capi.h"

#include <string>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace translator
{
	using nlohmann::json;

	inline bool is_true(json const& val)
	{
		switch (val.type())
		{
		case json::value_t::boolean: return bool(val);
		case json::value_t::null: return false;
		default: return true;
		}
	}

	struct context : translator_context
	{
		explicit context(context* parent);
		context();

		using eval_func = std::function<json(context&, std::vector<json>)>;
		
		eval_func unknown_func_eval;
		std::function<json(context&, std::string_view)> unknown_var_value_getter;
		std::function<std::string(context const&, std::string_view)> error_handler;

		std::map<std::string, eval_func, std::less<>> functions;
		std::map<std::string, json, std::less<>> user_storage;
		using user_storage_iterator = decltype(user_storage)::iterator;

		std::function<std::string(json const&)> json_value_to_str_func;

		context* parent() const { return (context*)parent_context; }

		std::string interpolate(std::string_view str);

		context const* get_root_context() const noexcept { return parent() ? parent()->get_root_context() : this; }

		std::pair<context*, user_storage_iterator> find_in_user_storage(std::string_view name);

		json user_var(std::string_view name);
		json& set_user_var(std::string_view name, json val, bool force_local = false);

		eval_func const* get_unknown_func_eval() const noexcept;
		eval_func const* find_func(std::string_view name) const;

		struct e_scope_terminator {
			json result = json(json::value_t::discarded);
			virtual std::string type() const noexcept = 0;
			virtual ~e_scope_terminator() noexcept = default;
		};

		std::string report_error(std::string_view error) const
		{
			if (error_handler)
				return error_handler(*this, error);
			throw std::runtime_error(std::string{ error });
		}

		std::string json_to_string(json const& j) const;

	private:

		json eval(json const& val);
		json safe_eval(json const& value);

		json eval(json&& al);
		json safe_eval(json&& value);

		json eval_call(std::vector<json> args);

		std::string consume_c_string(std::string_view& strv) const;

		auto consume_atom(std::string_view& sexp_str) const -> nlohmann::json;
		auto consume_list(std::string_view& sexp_str) const -> nlohmann::json;
		auto consume_value(std::string_view& sexp_str) const -> nlohmann::json;
	};
}
