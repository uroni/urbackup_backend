#include "../../Interface/Server.h"
#define BLOCKALIGN_NO_MAIN
#ifndef _WIN32
#define BLOCKALIGN_USE_CRYPTOPP
#endif
#include "../../blockalign_src/main.cpp"


int blockalign()
{
	std::string restore_fn = Server->getServerParameter("restore");
	std::string output_fn = Server->getServerParameter("output");

	std::string input_fn;
	std::string name;
	bool restore = false;
	bool verbose = false;
	if (!restore_fn.empty())
	{
		restore = true;

		if (output_fn.empty())
		{
			Server->Log("No output file for restore operation", LL_ERROR);
			return 1;
		}

		input_fn = restore_fn;
	}
	else
	{
		input_fn = Server->getServerParameter("input");

		if (input_fn.empty())
		{
			Server->Log("No input file specified", LL_ERROR);
			return 1;
		}

		if (output_fn.empty())
		{
			Server->Log("No output file specified", LL_ERROR);
			return 1;
		}

		name = Server->getServerParameter("hash_fn");

		if (name.empty())
		{
			Server->Log("No hash file name specified", LL_ERROR);
			return 1;
		}

		if (!Server->getServerParameter("verbose").empty())
		{
			verbose = true;
		}
	}

	std::istream* input;
	std::fstream in_stream;

	if (input_fn == "-")
	{
		if (restore)
		{
			std::cerr << "Restore from stdin not supported. Please provide seekable file." << std::endl;
			return 2;
		}

#ifdef _WIN32
		_setmode(_fileno(stdin), _O_BINARY);
#endif
		input = &std::cin;
	}
	else
	{
		in_stream.open(input_fn, std::ios::binary | std::ios::in);
		if (!in_stream.is_open())
		{
			std::cerr << "Cannot open input stream \"" << input_fn << "\"" << std::endl;
			return 1;
		}

		input = &in_stream;
	}

	std::ostream* output;
	std::fstream out_stream;

	if (output_fn == "-")
	{
#ifdef _WIN32
		_setmode(_fileno(stdout), _O_BINARY);
#endif
		output = &std::cout;
	}
	else
	{
		out_stream.open(output_fn, std::ios::binary | std::ios::out);
		if (!out_stream.is_open())
		{
			std::cerr << "Cannot open output stream \"" << output_fn << "\"" << std::endl;
			return 1;
		}

		output = &out_stream;
	}

	input->exceptions(std::istream::failbit | std::istream::badbit);
	output->exceptions(std::istream::failbit | std::istream::badbit);

	int ret;
	try
	{
		if (restore)
		{
			return restore_stream(input_fn, *input, *output) ? 0 : 1;
		}

		ret = blockalign(name, *input, *output, verbose);
	}
	catch (const std::ios_base::failure& e)
	{
		std::cerr << "Input/output error. " << e.what() << std::endl;
		ret = 2;
	}

	if (ret == 0
		&& output == &out_stream)
	{
		out_stream.close();

		if (rename((name + ".new").c_str(), name.c_str()) != 0)
		{
			std::cerr << "Renaming \"" << name << ".new\" to \"" << name << "\" failed" << std::endl;
			return 1;
		}
	}

	return ret;
}