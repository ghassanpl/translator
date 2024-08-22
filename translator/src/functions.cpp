#include "../include/ghassanpl/translator/translator.hpp"
#include "format.h"

namespace translator
{
	defined_function const* context::get_unknown_func_handler() const noexcept
	{
		if (m_unknown_func_handler.func)
			return &m_unknown_func_handler;
		if (parent_context)
			return parent()->get_unknown_func_handler();
		return nullptr;
	}

	defined_function const* context::bind_function(std::string_view signature_spec, eval_func func)
	{
		if (!func)
		{
			report_error("cannot bind a null function");
			return {};
		}
		/// "if [] then [] else []"
		/// "[] , []"

		/// TODO: Param modifiers: 
		/// - optional
		/// - eval/noeval - if we make `bind_function` and `bind_macro` separate... :)

		const auto copy = signature_spec;
		auto param_array = consume_list(signature_spec);
		if (!signature_spec.empty())
		{
			report_error(format("invalid function signature at point: {} (signature: {})", signature_spec, copy));
			return {};
		}

		const auto elem_count = param_array.size();

		if (elem_count == 1) /// noargs
		{
			if (!param_array[0].is_string() || param_array[0].empty())
			{
				report_error(format("function name must be a string, not '{}'", value_to_string(param_array[0])));
				return {};
			}

			std::string signature = param_array[0];
			auto& definition = this->m_functions_by_sig[signature];
			if (!definition.func)
				definition.signature = std::move(signature);
			definition.func = std::move(func);

			return &definition;
		}

		const bool infix = (elem_count % 2) == 1;

		for (size_t i = 0; i < elem_count + infix; i += 2)
		{
			auto const& param_decl = param_array[i + !infix];
			if (!param_decl.is_array())
			{
				report_error(format("function parameter declaration must be an array, not '{}'", value_to_string(param_decl)));
				return {};
			}
		}

		if (infix && (!param_array.is_array() || !param_array[0].empty()))
		{
			report_error(format("first function parameter of infix functions cannot have attributes"));
			return {};
		}

		auto* tree = infix ? &m_infix_function_tree : &m_prefix_function_tree;
		func_tree_element const* last_func_element = nullptr;
		std::string signature;
		if (infix) {
			//signature += "[]";
			signature += options.opening_delimiter;
			signature += options.closing_delimiter;
		}

		for (size_t i = infix; i < elem_count; i += 2)
		{
			auto& prefix = param_array[i];
			auto const& param_decl = param_array[i + 1];

			if (i != 0) signature += ' ';
			signature += prefix;
			signature += ' ';
			signature += options.opening_delimiter;
			signature += join(param_decl, ' ', [](json const& wut) -> std::string { return wut; });
			signature += options.closing_delimiter;

			func_tree_element el;
			el.name = std::move(prefix.get_ref<json::string_t&>());
			el.modifier = 0;
			//el.leaf_signature = signature;

			for (auto& modifier : param_decl)
			{
				static const json zero_or_more = "*";
				static const json one_or_more = "+";
				static const json optional = "?";
				if (modifier == one_or_more)
					el.modifier = '+';
				else if (modifier == zero_or_more)
					el.modifier = '*';
				else if (modifier == optional)
					el.modifier = '?';
				else
				{
					report_error(format("function parameter attribute '{}' not supported", value_to_string(modifier)));
					return {};
				}

				if (i == 0 && el.modifier != '+')
				{
					/// TODO: Technically we COULD generate a `name` and `name:` overload for sig `name [?]`
					///		but then we'd have the same function bound with multiple signatures...
					report_error(format("first function parameter cannot be optional", value_to_string(modifier)));
					return {};
				}
			}

			last_func_element = &*tree->insert(std::move(el)).first;
			tree = &last_func_element->child_elements;
		}

		auto& definition = this->m_functions_by_sig[signature];
		if (definition.func)
			assert(definition.signature == signature);
		if (!definition.func)
		{
			definition.signature = std::move(signature);
			definition.element = last_func_element;
		}
		definition.func = std::move(func);
		last_func_element->leaf = &definition;
		return &definition;
	}

	std::vector<defined_function const*> context::find_functions(std::vector<json> const& arguments, bool only_in_local) const
	{
		auto candidates = find_local_functions(arguments);
		if (!only_in_local && candidates.empty() && parent_context)
			return parent()->find_functions(arguments, false);
		return candidates;
	}

	/*
	std::string context::array_to_string(std::vector<json> const& args) const
	{
		const auto elem_count = args.size();
		const bool infix = (elem_count % 2) == 1;

		if (elem_count == 1)
			return value_to_string(args[0]);

		std::string result;

		/// TODO: Should we use `arg[i].type_name()` instead of "<arg>"?

		if (infix)
		{
			//result += "[] ";
			//result += value_to_string(args[0]);

		}

		for (size_t i = infix; i < elem_count; i += 2)
		{
			if (i != infix) result += ' ';
			result += value_to_string(args[i]);
			result += " []";
		}

		return result;
	}
	*/

	void context::find_local_functions(
		tree_type const& in_tree, 
		std::vector<json>::const_iterator name_it, 
		std::vector<json>::const_iterator names_end,
		std::vector<defined_function const*>& found
	) const
	{
		std::vector<tree_type::const_iterator> candidates;

		auto [begin, end] = in_tree.equal_range(std::string_view{ *name_it });
		for (auto element = begin; element != end && name_it != names_end; ++element)
		{
			auto name = name_it;
			if (element->modifier == '+')
			{
				while ((name += 2) != names_end && *name == element->name)
					;

				candidates.push_back(element);
			}
			else if (element->modifier == '?')
			{
				throw "unimplemeted";
			}
			else if (element->modifier == '*')
			{
				throw "unimplemeted";
			}
			else
			{
				candidates.push_back(element);
			}
		}

		name_it += 2;

		if (name_it == names_end)
		{
			for (auto const& candidate : candidates)
			{
				if (candidate->leaf)
					found.push_back(candidate->leaf);
			}
		}
		else
		{
			for (auto const& candidate : candidates)
				find_local_functions(candidate->child_elements, name_it, names_end, found);
		}
	}

	std::vector<defined_function const*> context::find_local_functions(std::vector<json> const& arguments) const
	{
		std::vector<defined_function const*> result;
		if (arguments.size() == 1) /// no args
		{
			if (auto it = m_functions_by_sig.find(arguments[0]); it != m_functions_by_sig.end())
				return { &it->second };
			return {};
		}
		else if (arguments.size() % 2) /// infix
			find_local_functions(m_infix_function_tree, arguments.begin() + 1, arguments.end(), result);
		else /// prefix
			find_local_functions(m_prefix_function_tree, arguments.begin() + 0, arguments.end(), result);
		return result;
	}

}