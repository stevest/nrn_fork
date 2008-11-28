#include <../../nrnconf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mos2nrn.h"

static void getdname(char* dname);

#include <string.h>

const char* back2forward(const char*);
const char* basefile(const char*);

int main(int argc, char** argv) {
	char buf[256];
	char dname[256];

	if (argc != 2 ) {
		fprintf(stderr, "Usage: mos2nrn [hocfile | nrnzipfile]\n");
		return 1;
	}
	FILE* f = fopen(argv[1], "r");
	if (!f) {
		return 1;
	}
	fread(buf, 1, 5, f);
	fclose(f);
	if (strncmp(buf, "PK", 2) == 0) { // its a nrnzip file
		getdname(dname);
sprintf(buf, "xterm -sb -e %s/mos2nrn2.sh %s %s %d", NEURON_BIN_DIR,
			argv[1], dname, 0);
		system(buf);
	}else{ // its a hoc file
		sprintf(buf, "xterm -sb -e %s/nrniv %s -",
			NEURON_BIN_DIR, argv[1]);
		system(buf);
	}
	return 0;
}

const char* back2forward(const char* back) {
	static char forward[256];
	char *cp;
		strcpy(forward, back);
		for (cp = forward; *cp; ++cp) {
			if (*cp == '\\') {
				*cp = '/';
			}
		}
	return forward;
}

const char* basefile(const char* path) {
	const char* cp;
	for (cp = path + strlen(path) - 1; cp >= path; --cp) {
		if (*cp == '\\' || *cp == ':' || *cp == '/') {
			return cp+1;
		}
	}
	return path;
}

static void getdname(char* dname) {
	int fd;
	char* tdir;
	if ((tdir = getenv("TEMP")) == NULL) {
		tdir = "/tmp";
	}
	sprintf(dname, "%s/nrnXXXXXX", tdir);
#if HAVE_MKSTEMP
	fd = mkstemp(dname);
	close(fd);
#else
	mktemp(dname);
#endif
}
