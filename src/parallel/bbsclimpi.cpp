#include <../../nrnconf.h>
#include <nrnmpi.h>
#include "../nrnmpi/bbsmpipack.h"
#include "bbsconf.h"
#ifdef NRNMPI	// to end of file
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <InterViews/resource.h>
#include "oc2iv.h"
#include "bbs.h"
#include "bbsrcli.h"
#include "bbssrv.h"

#define debug 0

#if defined(HAVE_STL)
#if defined(HAVE_SSTREAM) // the standard ...
#include <map>
#else
#include <pair.h>
#include <map.h>
#endif

struct ltint {
	boolean operator() (int i, int j) const {
		return i < j;
	}
};

class KeepArgs : public map<int, bbsmpibuf*, ltint>{};

#endif

int BBSClient::sid_;

BBSClient::BBSClient() {
	sendbuf_ = nil;
	recvbuf_ = nil;
	request_ = nrnmpi_newbuf(100);
	nrnmpi_ref(request_);
#if defined(HAVE_STL)
	keepargs_ = new KeepArgs();
#endif
	BBSClient::start();
}

BBSClient::~BBSClient() {
	nrnmpi_unref(sendbuf_);
	nrnmpi_unref(recvbuf_);
	nrnmpi_unref(request_);
#if defined(HAVE_STL)
	delete keepargs_;
#endif
}

void BBSClient::perror(const char* s) {
	printf("BBSClient error: %s\n", s);
}

void BBSClient::upkbegin() {
	nrnmpi_upkbegin(recvbuf_);
}

char* BBSClient::getkey() {
	return nrnmpi_getkey(recvbuf_);
}

int BBSClient::getid() {
	return nrnmpi_getid(recvbuf_);
}

int BBSClient::upkint() {
	return nrnmpi_upkint(recvbuf_);
}

double BBSClient::upkdouble() {
	return nrnmpi_upkdouble(recvbuf_);
}

void BBSClient::upkvec(int n, double* x) {
	nrnmpi_upkvec(n, x, recvbuf_);
}

char* BBSClient::upkstr() {
	return nrnmpi_upkstr(recvbuf_); // do not forget to free(string)
}

void BBSClient::pkbegin() {
	if (!sendbuf_) {
		sendbuf_ = nrnmpi_newbuf(100);
		nrnmpi_ref(sendbuf_);
	}
	nrnmpi_pkbegin(sendbuf_);
}

void BBSClient::pkint(int i) {
	nrnmpi_pkint(i, sendbuf_);
}

void BBSClient::pkdouble(double x) {
	nrnmpi_pkdouble(x, sendbuf_);
}

void BBSClient::pkvec(int n, double* x) {
	nrnmpi_pkvec(n, x, sendbuf_);
}

void BBSClient::pkstr(const char* s) {
	nrnmpi_pkstr(s, sendbuf_);
}

void BBSClient::post(const char* key) {
#if debug
printf("%d BBSClient::post |%s|\n", nrnmpi_myid, key);
fflush(stdout);
#endif
	nrnmpi_enddata(sendbuf_);
	nrnmpi_pkstr(key, sendbuf_);
	nrnmpi_bbssend(sid_, POST, sendbuf_);
	nrnmpi_unref(sendbuf_);
	sendbuf_ = nil;
}

void BBSClient::post_todo(int parentid) {
#if debug
printf("%d BBSClient::post_todo for %d\n", nrnmpi_myid, parentid);
fflush(stdout);
#endif
	nrnmpi_enddata(sendbuf_);
	nrnmpi_pkint(parentid, sendbuf_);
	nrnmpi_bbssend(sid_, POST_TODO, sendbuf_);
	nrnmpi_unref(sendbuf_);
	sendbuf_ = nil;
}

void BBSClient::post_result(int id) {
#if debug
printf("%d BBSClient::post_result %d\n", nrnmpi_myid, id);
fflush(stdout);
#endif
	nrnmpi_enddata(sendbuf_);
	nrnmpi_pkint(id, sendbuf_);
	nrnmpi_bbssend(sid_, POST_RESULT, sendbuf_);
	nrnmpi_unref(sendbuf_);
	sendbuf_ = nil;
}

int BBSClient::get(const char* key, int type) {
#if debug
printf("%d BBSClient::get |%s| type=%d\n", nrnmpi_myid, key, type);
fflush(stdout);
#endif
	nrnmpi_pkbegin(request_);
	nrnmpi_enddata(request_);
	nrnmpi_pkstr(key, request_);
	return get(type);
}

int BBSClient::get(int key, int type) {
#if debug
printf("%d BBSClient::get %d type=%d\n", nrnmpi_myid, key, type);
fflush(stdout);
#endif
	nrnmpi_pkbegin(request_);
	nrnmpi_enddata(request_);
	nrnmpi_pkint(key, request_);
	return get(type)-1; // sent id+1 so cannot be mistaken for QUIT
}

int BBSClient::get(int type) { // blocking
fflush(stdout);
fflush(stderr);
	double ts = time();
	nrnmpi_unref(recvbuf_);
	recvbuf_ = nrnmpi_newbuf(100);
	nrnmpi_ref(recvbuf_);
	int msgtag = nrnmpi_bbssendrecv(sid_, type, request_, recvbuf_);
	errno = 0;
	wait_time_ += time() - ts;
#if debug
printf("%d BBSClient::get return msgtag=%d\n", nrnmpi_myid, msgtag);
fflush(stdout);
#endif
	if (msgtag == QUIT) {
		done();
	}
	return msgtag;
}
	
boolean BBSClient::look_take(const char* key) {
#if debug
printf("%d BBSClient::look_take %s\n", nrnmpi_myid, key);
#endif
	int type = get(key, LOOK_TAKE);
	boolean b = (type == LOOK_TAKE_YES);
	if (b) {
		upkbegin();
	}
	return b;
}

boolean BBSClient::look(const char* key) {
#if debug
printf("%d BBSClient::look %s\n", nrnmpi_myid, key);
#endif
	int type = get(key, LOOK);
	boolean b = (type == LOOK_YES);
	if (b) {
		upkbegin();
	}
	return b;
}

void BBSClient::take(const char* key) { // blocking
	int bufid;
	get(key, TAKE);	
	upkbegin();
}
	
int BBSClient::look_take_todo() {
	int type = get(0, LOOK_TAKE_TODO);
	if (type) {
		upkbegin();
	}
	return type;
}

int BBSClient::take_todo() {
	int type;
	while((type = get(0, TAKE_TODO)) == CONTEXT) {
		upkbegin();
		upkint(); // throw away userid
#if debug
printf("%d execute context\n", nrnmpi_myid);
fflush(stdout);
#endif
		execute_helper();
	}
	upkbegin();
	return type;
}

int BBSClient::look_take_result(int pid) {
	int type = get(pid, LOOK_TAKE_RESULT);
	if (type) {
		upkbegin();
	}
	return type;
}

void BBSClient::save_args(int userid) {
#if defined(HAVE_STL)
	nrnmpi_ref(sendbuf_);
	keepargs_->insert(
		pair<const int, bbsmpibuf* >(userid, sendbuf_)
	);
	
#endif
	post_todo(working_id_);
}

void BBSClient::return_args(int userid) {
#if defined(HAVE_STL)
	KeepArgs::iterator i = keepargs_->find(userid);
	nrnmpi_unref(recvbuf_);
	recvbuf_ = nil;
	if (i != keepargs_->end()) {
		recvbuf_ = (*i).second;
		nrnmpi_ref(recvbuf_);
		keepargs_->erase(i);
		upkbegin();
		BBSImpl::return_args(userid);
	}
#endif
}

void BBSClient::done() {
#if debug
printf("%d BBSClient::done\n", nrnmpi_myid);
fflush(stdout);
#endif
	BBSImpl::done();
	nrnmpi_terminate();
	exit(0);
}

void BBSClient::start() {
	char* client = 0;
	int tid;
	int n;
	if (started_) { return; }
#if debug
printf("%d BBSClient start\n", nrnmpi_myid);
fflush(stdout);
#endif
	BBSImpl::start();
	sid_ = 0;
#if 0
	{ // a worker
		is_master_ = false;
		nrnmpi_pkbegin(request_);
		nrnmpi_enddata(request_);
		assert(get(HELLO) == HELLO);
		return;
	}
#endif
}

#endif //NRNMPI
