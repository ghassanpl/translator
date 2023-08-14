#include "../include/ghassanpl/translator/translator.h"

#include <iostream>

using namespace translator;
int main()
{
	context ctx;
	ctx.unknown_var_value_getter = [](context const&, std::string_view var) -> json {
		return "<no var " + std::string{ var } + " found>";
	};
	ctx.error_handler = [](context const&, std::string_view err) -> std::string {
		std::cerr << "Error interpolating: " << err << "\n";
		return {};
	};
	ctx.set_user_var("asd", { {"asd", "asd"} });
	std::cout << ctx.interpolate("hello world [.asd]") << "\n";

	auto cctx = translator_new_context();
	translator_set_string_value(translator_user_var(cctx, "asd"), "booba");
	
	auto result = translator_interpolate_str(cctx, "hello world [.asd]");
	std::cout << result << "\n";
	free((void*)result);

	return 0;
}