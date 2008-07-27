// this is the wrapper that allows one to create shortcuts and
// associate hoc files with neuron.exe without having to consider
// the issues regarding shells and the rxvt console. The latter
// can be given extra arguments in the lib/neuron.sh file which
// finally executes nrniv.exe.  Nrniv.exe can be run by itself if
// it does not need a console or system("command")

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

char* back2forward(const char*);

char* nrnhome;
char* nh;

static void setneuronhome() {
	int i, j;
	char buf[256];
	GetModuleFileName(NULL, buf, 256);
	for (i=strlen(buf); i >= 0 && buf[i] != '\\'; --i) {;}
	buf[i] = '\0'; // /neuron.exe gone
	for (i=strlen(buf); i >= 0 && buf[i] != '\\'; --i) {;}
	buf[i] = '\0'; // /bin gone
	nrnhome = new char[strlen(buf)+1];
	strcpy(nrnhome, buf);
}

char* back2forward(const char* back) {
	char *forward;
	char *cp;
   	forward = new char[strlen(back) + 1];
		strcpy(forward, back);
		for (cp = forward; *cp; ++cp) {
			if (*cp == '\\') {
				*cp = '/';
			}
		}
	return forward;
}

static char* argstr(int argc, char** argv) {
	// put args into single string, escaping spaces in args.
	int i, j, cnt;
	char* s;
	char* a;
	cnt = 100;
	for (i=1; i < argc; ++i) {
		cnt += strlen(argv[i]);
	}
	s = new char[cnt];
	j = 0;
	for (i=1; i < argc; ++i) {
		// convert \ to / and space to @@
		// the latter will be converted back to space in src/oc/hoc.c
		for (a = argv[i]; *a; ++a) {
			if (*a == '\\') {
				s[j++] = '/';
			}else if (*a == ' ') {
				s[j++] = '@';
				s[j++] = '@';
			}else{
				s[j++] = *a;
			}
		}
		if (i < argc-1) {
			s[j++] = ' ';
		}
	}
	s[j] = '\0';
	return s;
}
	
int main(int argc, char** argv) {
	int err;
	char* buf;
	char* args;
	char* msg;

	setneuronhome();
	nh = back2forward(nrnhome);
	args = argstr(argc, argv);
	buf = new char[strlen(args) + 3*strlen(nh) + 200];
	sprintf(buf, "%s\\bin\\sh %s/lib/neuron.sh %s %s", nrnhome, nh, nh, args);
	msg = new char[strlen(buf) + 100];
	err = WinExec(buf, SW_SHOW);
	if (err < 32) {
		sprintf(msg, "Cannot WinExec %s\n", buf);
		MessageBox(0, msg, "NEURON", MB_OK);
	}
	return 0;
}
