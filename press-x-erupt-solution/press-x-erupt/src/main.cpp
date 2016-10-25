
#include "application.hpp"

#include <iostream>
#include <stdexcept>

int main()
{
	try
	{
		return px::application{}.run();
	}
	catch (std::runtime_error const& exception)
	{
		std::cerr << exception.what() << std::endl;
		return EXIT_FAILURE;
	}
}