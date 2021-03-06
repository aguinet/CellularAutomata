/*
 * Copyright (c) 2014 David Chisnall
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <iostream>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <libgen.h>
#include <time.h>
#include <unistd.h>
#include "parser.hh"
#include "ast.hh"

static int enableTiming = 0;

static void logTimeSince(clock_t c1, const char *msg)
{
	if (!enableTiming) { return; }
	clock_t c2 = clock();
	struct rusage r;
	getrusage(RUSAGE_SELF, &r);
	fprintf(stderr, "%s took %f seconds.	Peak used %ldKB.\n", msg,
		(static_cast<double>(c2) - static_cast<double>(c1)) / static_cast<double>(CLOCKS_PER_SEC), r.ru_maxrss);
}

int main(int argc, char **argv)
{
	std::string cmd = argv[0];
	std::string path = dirname(argv[0]);
	int iterations = 1;
	bool useJIT = false;
	bool debugGrid = false;
	int optimiseLevel = 0;
	int gridSize = 5;
	int maxValue = 1;
	clock_t c1;
	int c;
	auto usage = [=]() {
		std::cerr << "usage: " << cmd << " [-hjt] -i {iterations} -O {level} -x {size} -m {max} {file name}" << std::endl
		          << " -h          Display this help" << std::endl
		          << " -j          Compile (don't interpret) the program" << std::endl
		          << " -t          Display timing information" << std::endl
		          << " -O {level}  Set the optimisation level [default: " <<optimiseLevel << ']' << std::endl
		          << " -x {size}   Use a size by size grid [default: " << gridSize << ']' << std::endl
		          << " -m {max}    The maximum value for a random grid [default: " << maxValue << ']' << std::endl
		          << " {file name} The .ca source to run" << std::endl;
	};
	while ((c = getopt(argc, argv, "dji:tO:x:m:")) != -1)
	{
		switch (c)
		{
			default:
				usage();
				return EXIT_SUCCESS;
			case 'j':
				useJIT = 1;
				break;
			case 'x':
				gridSize = strtol(optarg, 0, 10);
				break;
			case 'm':
				maxValue = strtol(optarg, 0, 10);
				break;
			case 'i':
				iterations = strtol(optarg, 0, 10);
				break;
			case 't':
				enableTiming = true;
				break;
			case 'd':
				debugGrid = true;
				break;
			case 'O':
				optimiseLevel = strtol(optarg, 0, 10);
		}
	}
	argc -= optind;
	if (argc < 1)
	{
		usage();
		return EXIT_FAILURE;
	}
	if (gridSize < 1 || gridSize >= 1<<15)
	{
		fprintf(stderr, "Grid size must be between 1 and 2^15\n");
		return EXIT_FAILURE;
	}
	argv += optind;

	// Do the parsing
	Parser::CellAtomParser p;
	pegmatite::AsciiFileInput input(open(argv[0], O_RDONLY));
	std::unique_ptr<AST::StatementList> ast = 0;
	c1 = clock();
	pegmatite::ErrorReporter err =
		[](const pegmatite::InputRange& r, const std::string& msg) {
		std::cout << "error: " << msg << std::endl;
		std::cout << "line " << r.start.line
		          << ", col " << r.start.col << std::endl;
	};
	if (!p.parse(input, p.g.statements, p.g.ignored, err, ast))
	{
		return EXIT_FAILURE;
	}
	logTimeSince(c1, "Parsing program");
	assert(ast);

	int16_t oldgrid[] = {
		 0,0,0,0,0,
		 0,0,0,0,0,
		 0,1,1,1,0,
		 0,0,0,0,0,
		 0,0,0,0,0
	};
	int16_t newgrid[25];
	int16_t *g1;
	int16_t *g2;
	if (debugGrid)
	{
		gridSize = 5;
		g1 = oldgrid;
		g2 = newgrid;
	}
	else
	{
		g1 = new int16_t[gridSize * gridSize];
		g2 = new int16_t[gridSize * gridSize];
		c1 = clock();
		for (int i=0 ; i<(gridSize*gridSize) ; i++)
		{
			g1[i] = random() % (maxValue + 1);
		}
		logTimeSince(c1, "Generating random grid");
	}
	int i=0;
	if (useJIT)
	{
		
		c1 = clock();
		Compiler::automaton ca = Compiler::compile(ast.get(), optimiseLevel, path);
		logTimeSince(c1, "Compiling");
		c1 = clock();
		for (int i=0 ; i<iterations ; i++)
		{
			ca(g1, g2, gridSize, gridSize);
			std::swap(g1, g2);
		}
		logTimeSince(c1, "Running compiled version");
	}
	else
	{
		c1 = clock();
		for (int i=0 ; i<iterations ; i++)
		{
			Interpreter::runOneStep(g1, g2, gridSize, gridSize, ast.get());
			std::swap(g1, g2);
		}
		logTimeSince(c1, "Interpreting");
	}
	for (int x=0 ; x<gridSize ; x++)
	{
		for (int y=0 ; y<gridSize ; y++)
		{
			printf("%d ", g1[i++]);
		}
		putchar('\n');
	}
	return 0;
}
