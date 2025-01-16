#include <stdio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "wtinfo.h"

int main(int argc, char* argv[])
{
	int op;
	unsigned char *key = NULL;
	const char *val;
	void *info;
	unsigned char *j_str = NULL;

	while ((op = getopt(argc, argv, "g:s:")) != -1) {
		switch (op)
		{
		case 'g':
			key = optarg;
			break;
		case 's':
			j_str = optarg;
			break;
		}
	}

	if (j_str) {
		return wtinfo_save(j_str);
	} else {
		if (key) {
			info = wtinfo_init();

			if (info) {
				val = wtinfo_get_val(info, key);

				if (val)
					printf("%s\n", val);
				wtinfo_deinit(info);
			}
		}
	}

	return 0;
}
