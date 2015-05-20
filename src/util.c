#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include "util.h"
#include "dect.h"



void util_dump(unsigned char *buf, int size, char *start) {

	int i, dumpsize;
	int maxdump = 10000;
	//unsigned char* cdata = (unsigned char*)buf;

	printf("%s", start);
	printf("[%04d] - ",size);
    
	if ( size > maxdump ) {
		dumpsize = maxdump; 
	} else {
		dumpsize = size; 
	}

	for (i=0 ; i<dumpsize ; i++) {
		printf("%02x ",buf[i]);
	}
	printf("\n");
	
}


int check_args(int argc, char * argv[], config_t * c) {

	int arg ;
	const char * short_opt = "hpant:";
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"prog", no_argument, NULL, 'p'},
		{"app", no_argument, NULL, 'a'},
		{"nvs", no_argument, NULL, 'n'},
		{"test", no_argument, NULL, 't'},
		{NULL, 0, NULL, 0}
	};

	while (( arg = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {

		switch (arg) {
		case -1:
		case 0:
			break;

		case 'a':
			c->mode = APP_MODE;
			break;

		case 'n':
			c->mode = NVS_MODE;
			break;

		case 'p':
			c->mode = PROG_MODE;
			break;

		case 't':
			break;

			
		case 'h':
			printf("Usage %s [OPTIONS]\n", argv[0]);
			printf("-a, --app : Start in application mode\n");
			printf("-h, --help : print this help and exit\n");
			return -1;

		default:
			fprintf(stderr, "Try %s --help for more information.\n", argv[0]);
			return -1;
		}
	}
	
	return 0;
}
