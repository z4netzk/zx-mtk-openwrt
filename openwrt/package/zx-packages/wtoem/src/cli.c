#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "wtoem.h"

int main(int argc, char* argv[])
{
	int op;
	unsigned char part = 0;
	int res = 0;

	while ((op = getopt(argc, argv, "p:r")) != -1) {
		switch (op)
		{
		case 'p':
			part = atoi(optarg);
			break;
		case 'r':
			res = 1;
			break;
		}
	}

	if (res == 1)
		wtoem_reverse();
	else
		wtoem_save(part);

	return 0;
}
