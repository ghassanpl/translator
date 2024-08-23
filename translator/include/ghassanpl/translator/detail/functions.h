#pragma once

#include "utils.h"
#include <set>

namespace translator
{
	struct context;
	struct func_tree_element;
	using tree_type = std::set<func_tree_element, std::less<>>;

	struct defined_function
	{
		std::string signature;
		std::function<json(context&, std::vector<json>)> func;
		func_tree_element const* element = nullptr;
	};

	struct func_tree_element
	{
		std::string name;
		char modifier = 0; /// ? or * or +
		enum_flags<json::value_t> valid_types = enum_flags<json::value_t>::all();

		func_tree_element* parent = nullptr;

		bool operator<(func_tree_element const& other) const noexcept { return std::tie(name, modifier, valid_types) < std::tie(other.name, other.modifier, other.valid_types); }
		friend bool operator<(func_tree_element const& self, std::string_view other) noexcept { return self.name < other; }
		friend bool operator<(std::string_view other, func_tree_element const& self) noexcept { return other < self.name; }

		mutable tree_type child_elements;
		mutable defined_function* leaf = nullptr;
	};
}
