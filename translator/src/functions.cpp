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

		/// TODO: Param modifiers: 
		/// - eval/noeval - if we make `bind_function` and `bind_macro` separate... :)

		/// Parse input signature
		const auto copy = signature_spec;
		auto param_array = consume_list(signature_spec, false);
		const auto elem_count = param_array.size();
		const bool infix = (elem_count % 2) == 1;

		if (!signature_spec.empty())
		{
			report_error(format("invalid function signature at point: {} (signature: {})", signature_spec, copy));
			return {};
		}
		if (elem_count == 0)
		{
			report_error(format("function signature cannot be empty"));
			return {};
		}

		/// Create canonical signature
		std::string signature;
		if (infix)
			signature += param_array[0];
		for (size_t i = infix; i < elem_count; i += 2)
		{
			auto& prefix = param_array[i];
			std::string_view param_decl = param_array[i + 1];
			if (i != 0) signature += ' ';
			signature += prefix;
			signature += ' ';
			signature += param_decl;
		}

		/// Special case for noargs functions
		if (elem_count == 1) /// noargs
		{
			if (!param_array[0].is_string() || param_array[0].empty())
			{
				report_error(format("function name part must be a non-empty string, not '{}'", value_to_string(param_array[0])));
				return {};
			}

			return add_function(std::move(signature), std::move(func));
		}

		/// Verify parameter names
		for (size_t i = 0; i < elem_count + infix; i += 2)
		{
			auto const& param_decl = param_array[i + !infix];
			if (!param_decl.is_string() || param_decl.empty())
			{
				report_error(format("function parameter name must be a non-empty string, not '{}'", value_to_string(param_decl)));
				return {};
			}
		}

		static constexpr char zero_or_more = '*';
		static constexpr char one_or_more = '+';
		static constexpr char optional = '?';
		static constexpr std::string_view modifiers = "*+?";

		/// Check for invalid modifiers
		if (infix && modifiers.find(std::string_view{ param_array[0] }.back()) != std::string::npos)
		{
			report_error(format("first function parameter of infix functions cannot have modifiers"));
			return {};
		}

		/// Go through all the function parts and add them to the tree
		auto* tree = infix ? &m_infix_function_tree : &m_prefix_function_tree;
		func_tree_element const* last_func_element = nullptr;

		for (size_t i = infix; i < elem_count; i += 2)
		{
			auto& prefix = param_array[i];
			std::string_view param_decl = param_array[i + 1];

			func_tree_element el;
			el.name = std::move(prefix.get_ref<json::string_t&>());
			el.modifier = 0;

			if (param_decl.back() == one_or_more)
				el.modifier = '+';
			else if (param_decl.back() == zero_or_more)
				el.modifier = '*';
			else if (param_decl.back() == optional)
				el.modifier = '?';

			last_func_element = &*tree->insert(std::move(el)).first;
			tree = &last_func_element->child_elements;
		}

		/// Actually create the function definition and attach it to the leaf element of the tree
		return last_func_element->leaf = add_function(std::move(signature), std::move(func));
	}

	std::vector<defined_function const*> context::find_functions(std::vector<json> const& arguments, bool only_in_local) const
	{
		auto candidates = find_local_functions(arguments);
		if (!only_in_local && candidates.empty() && parent_context)
			return parent()->find_functions(arguments, false);
		return candidates;
	}

	void context::find_local_functions(
		tree_type const& in_tree,
		std::vector<json>::const_iterator arg_name_it,
		std::vector<json>::const_iterator arg_names_end,
		std::set<defined_function const*>& found
	) const
	{
		/// TODO: This function needs a good thrashing cause it's not working properly for variadic functions

		/// NOTE: This function probably has a high O() complexity, but it's not a problem for now.
		/// TODO: 
		/// We could probably cache a function set per list of argument names, but that's a later optimization.
		/// We could probably even cache it based on the hash of the list of argument names, to improve perf.
		/// We'd have to invalidate this cache when functions are added/removed/changed tho.

		std::vector<std::pair<std::vector<json>::const_iterator, tree_type::const_iterator>> candidates;

		/// Look for functions with optional parameters at this point
		for (auto it = in_tree.begin(); it != in_tree.end(); ++it)
		{
			auto& el = *it;
			if (el.modifier == '*' || el.modifier == '?')
			{
				candidates.push_back({ arg_name_it, it });
				if (el.leaf && arg_name_it == arg_names_end)
					found.insert(el.leaf);
			}
		}

		/// If we have no more argument names given, don't search in tree
		if (arg_name_it == arg_names_end)
			return;

		/// Find all function subtrees that start with the next argument name
		auto [begin, end] = in_tree.equal_range(std::string_view{ *arg_name_it });
		for (auto element = begin; element != end && arg_name_it != arg_names_end; ++element)
		{
			auto name = arg_name_it;
			if (element->modifier == '+' || element->modifier == '*') /// Variadic parameters
			{
				while ((name += 2) != arg_names_end && *name == element->name)
					;

				candidates.push_back({ name, element });
			}
			else
			{
				candidates.push_back({ name + 2, element });
			}
		}

		/// We now have a list of candidate subtrees
		for (auto const& candidate : candidates)
		{
			/// If we have no more argument names, and this candidate is a leaf, we found a function
			if (candidate.first == arg_names_end && candidate.second->leaf)
				found.insert(candidate.second->leaf);
			/// If we have no more argument names, but this candidate is not a leaf, search in its subtree
			else if (candidate.first != arg_names_end)
				find_local_functions(candidate.second->child_elements, candidate.first, arg_names_end, found);
			/// If we have more argument names, but we have a candidate, it's probably because the candidate is variadic
			/// at this point, so search in its subtree
			else if (candidate.first == arg_names_end && candidate.second->child_elements.size() > 0)
				find_local_functions(candidate.second->child_elements, candidate.first, arg_names_end, found);
		}
	}

	std::vector<defined_function const*> context::find_local_functions(std::vector<json> const& arguments) const
	{
		std::set<defined_function const*> result;
		if (arguments.size() == 1) /// no args
		{
			if (auto it = m_functions_by_sig.find(arguments[0]); it != m_functions_by_sig.end())
				result.insert(&it->second);

			if (auto it = m_prefix_function_tree.find(std::string_view{ arguments[0] }); it != m_prefix_function_tree.end())
			{
				if ((it->modifier == '?' || it->modifier == '*') && it->leaf)
					result.insert(it->leaf);
			}
		}
		else if (arguments.size() % 2) /// infix
			find_local_functions(m_infix_function_tree, arguments.begin() + 1, arguments.end(), result);
		else /// prefix
			find_local_functions(m_prefix_function_tree, arguments.begin() + 0, arguments.end(), result);
		return { result.begin(), result.end() };
	}

	defined_function* context::add_function(std::string signature, eval_func func)
	{
		auto& definition = this->m_functions_by_sig[signature];
		if (definition.func)
			assert(definition.signature == signature);
		else
			definition.signature = std::move(signature);
		definition.func = std::move(func);
		return &definition;
	}
}