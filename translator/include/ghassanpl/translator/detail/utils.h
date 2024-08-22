#pragma once

#include <string>
#include <map>
#include <functional>
/// TODO: No reason not to switch to boost::json (much faster) or even a fully custom JSON lib
#include <nlohmann/json.hpp>
#include <array>
#include <sstream>

namespace translator
{
	template <typename RESULT_TYPE, typename ENUM_TYPE>
	constexpr RESULT_TYPE flag_bit(ENUM_TYPE e) noexcept
	{
		return (static_cast<RESULT_TYPE>(1) << static_cast<RESULT_TYPE>(e));
	}

	template <typename ENUM_TYPE, typename VALUE_TYPE = unsigned long long>
	struct enum_flags
	{
		using value_type = VALUE_TYPE;
		using enum_type = ENUM_TYPE;
		using self_type = enum_flags<ENUM_TYPE, VALUE_TYPE>;

		value_type bits;

		constexpr enum_flags() noexcept : bits(0) {}
		constexpr enum_flags(const enum_flags& other) noexcept : bits(other.bits) {};
		constexpr enum_flags& operator=(const enum_flags& other) noexcept { bits = other.bits; return *this; }

		constexpr enum_flags(enum_type base_value) noexcept : bits(flag_bit<VALUE_TYPE>(base_value)) {}
		constexpr explicit enum_flags(value_type value) noexcept : bits(value) {}

		[[nodiscard]] constexpr static self_type all() noexcept { return self_type(~static_cast<VALUE_TYPE>(0)); }
		[[nodiscard]] constexpr static self_type all(enum_type last) noexcept { return self_type(flag_bit<VALUE_TYPE>(last) | (flag_bit<VALUE_TYPE>(last) - 1)); }
		[[nodiscard]] constexpr static self_type none() noexcept { return self_type(); }

		[[nodiscard]] constexpr bool is_set(enum_type flag) const noexcept { return (bits & flag_bit<VALUE_TYPE>(flag)) != 0; }

		[[nodiscard]] constexpr bool contain(enum_type flag) const noexcept { return this->is_set(flag); }
		[[nodiscard]] constexpr bool contains(enum_type flag) const noexcept { return this->is_set(flag); }

		[[nodiscard]] constexpr bool are_any_set() const noexcept { return bits != 0; }
		[[nodiscard]] constexpr bool are_any_set(self_type other) const noexcept { return other.bits == 0 /* empty set */ || (bits & other.bits) != 0; }
		[[nodiscard]] constexpr bool are_all_set(self_type other) const noexcept { return (bits & other.bits) == other.bits; }

		constexpr explicit operator bool() const noexcept { return bits != 0; }
		constexpr bool operator<(self_type other) const noexcept { return bits < other.bits; }

		constexpr self_type& set(enum_type e) noexcept { bits |= flag_bit<VALUE_TYPE>(e); return *this; }
		constexpr self_type& set(self_type other) noexcept { bits |= other.bits; return *this; }

		constexpr self_type& unset(enum_type e) noexcept { bits &= ~flag_bit<VALUE_TYPE>(e); return *this; }
		constexpr self_type& unset(self_type other) noexcept { bits &= ~other.bits; return *this; }

		constexpr self_type& toggle(enum_type e) noexcept { bits ^= flag_bit<VALUE_TYPE>(e); return *this; }
		constexpr self_type& toggle(self_type other) noexcept { bits ^= other.bits; return *this; }

		constexpr self_type& set_to(enum_type e, bool val) noexcept
		{
			if (val) bits |= flag_bit<VALUE_TYPE>(e); else bits &= ~flag_bit<VALUE_TYPE>(e);
			return *this;
		}
		constexpr self_type& set_to(self_type other, bool val) noexcept
		{
			if (val) bits |= other.bits; else bits &= ~other.bits;
			return *this;
		}

		[[nodiscard]] constexpr self_type but_only(self_type flags) const noexcept { return self_type(bits & flags.bits); }
		[[nodiscard]] constexpr self_type intersected_with(self_type flags) const noexcept { return self_type(bits & flags.bits); }

		[[nodiscard]] constexpr self_type operator+(enum_type flag) const noexcept { return self_type(bits | flag_bit<VALUE_TYPE>(flag)); }
		[[nodiscard]] constexpr self_type operator-(enum_type flag) const noexcept { return self_type(bits & ~flag_bit<VALUE_TYPE>(flag)); }

		[[nodiscard]] constexpr self_type operator+(self_type flags) const noexcept { return self_type(bits | flags.bits); }
		[[nodiscard]] constexpr self_type operator-(self_type flags) const noexcept { return self_type(bits & ~flags.bits); }

		constexpr bool operator==(self_type other) const noexcept { return bits == other.bits; }
		constexpr bool operator!=(self_type other) const noexcept { return bits != other.bits; }
	};

	using nlohmann::json;

	inline bool is_true(json const& val) noexcept
	{
		switch (val.type())
		{
		case json::value_t::boolean: return bool(val);
		case json::value_t::null: return false;
		default: return true;
		}
	}

	struct context;
	using eval_func = std::function<json(context&, std::vector<json>)>;


	/// Returns a string that is created by joining together string representation of the elements in the `source` range, separated by `delim`; `delim` is only added between elements.
	template <typename T, typename DELIM>
	auto join(T&& source, DELIM const& delim)
	{
		std::stringstream strm;
		bool first = true;
		for (auto&& p : std::forward<T>(source))
		{
			if (!first) strm << delim;
			strm << p;
			first = false;
		}
		return std::move(strm).str();
	}
	
	template <typename T, typename DELIM>
	auto simple_join(T&& source, DELIM const& delim)
	{
		std::stringstream strm;
		for (auto&& p : std::forward<T>(source))
			strm << p << delim;
		return std::move(strm).str();
	}

	template <typename T, typename DELIM, typename FUNC>
	auto join(T&& source, DELIM const& delim, FUNC&& func)
	{
		std::stringstream strm;
		bool first = true;
		for (auto&& p : std::forward<T>(source))
		{
			if (!first) strm << delim;
			strm << func(p);
			first = false;
		}
		return std::move(strm).str();
	}
	
	template <typename T, typename DELIM, typename FUNC>
	auto simple_join(T&& source, DELIM const& delim, FUNC&& func)
	{
		std::stringstream strm;
		for (auto&& p : std::forward<T>(source))
			strm << func(p) << delim;
		return std::move(strm).str();
	}
}
