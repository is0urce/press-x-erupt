
#include <px/core/application.hpp>

#include <iostream>
#include <stdexcept>

int main()
{
	int code = EXIT_FAILURE;
	try
	{
		code = px::application{}.run();
	}
	catch (std::runtime_error const& exception)
	{
		std::cerr << exception.what() << std::endl;
	}
	return code;
}