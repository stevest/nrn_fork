#include <../../nrnconf.h>
#include "bbsconf.h"
#include <InterViews/resource.h>
#include "classreg.h"
#include "oc2iv.h"
#include "ivocvect.h"
#include "hoclist.h"
#include "bbs.h"
#include "bbsimpl.h"
#include "ivocvect.h"
#include "parse.h"
#include "section.h"
#include "membfunc.h"
#include <nrnmpi.h>
#include <errno.h>

#undef MD
#define MD 2147483648.

extern "C" {
	extern int vector_arg_px(int, double**);
	Symbol* hoc_which_template(Symbol*);
	void bbs_done();
	extern double t;
#if BLUEGENE_CHECKPOINT
	int BGLCheckpoint();
#endif
	extern void nrnmpi_source_var(), nrnmpi_target_var(), nrnmpi_setup_transfer();
	extern int nrnmpi_spike_compress(int nspike, boolean gid_compress, int xchng_meth);
	extern int nrnmpi_splitcell_connect(int that_host);
	extern int nrnmpi_multisplit(double x, int sid, int backbonestyle);
	extern void nrnmpi_gid_clear(int);
	double nrnmpi_rtcomp_time_;
	extern double nrn_bgp_receive_time(int);
	char* (*nrnpy_po2pickle)(Object*, size_t*);
	Object* (*nrnpy_pickle2po)(char*, size_t);
	char* (*nrnpy_callpicklef)(char*, size_t, int, size_t*);
#if PARANEURON
	double nrnmpi_transfer_wait_;
	double nrnmpi_splitcell_wait_;
#endif
#if NRNMPI
	void nrnmpi_barrier();
	double nrnmpi_dbl_allreduce(double, int);
	void nrnmpi_dbl_allgather(double*, double*, int);
	void nrnmpi_int_alltoallv(int*, int*, int*, int*, int*, int*);
	void nrnmpi_dbl_alltoallv(double*, int*, int*, double*, int*, int*);
	double nrmpi_wtime();
	void nrnmpi_int_broadcast(int*, int, int);
	void nrnmpi_char_broadcast(char*, int, int);
	void nrnmpi_dbl_broadcast(double*, int, int);
#endif

	extern double* nrn_mech_wtime_;
	extern int nrn_nthread;
	extern void nrn_threads_create(int, int);
	extern void nrn_thread_partition(int, Object*);
	extern void nrn_thread_stat();
	extern int nrn_allow_busywait(int);
	extern int nrn_how_many_processors();
}

class OcBBS : public BBS , public Resource {
public:
	OcBBS(int nhost_request);
	virtual ~OcBBS();
public:
	double retval_;
	int userid_;
	int next_local_;
};

OcBBS::OcBBS(int n) : BBS(n) {
	next_local_ = 0;
}

OcBBS::~OcBBS() {
}

static boolean posting_ = false;
static void pack_help(int, OcBBS*);
static void unpack_help(int, OcBBS*);
static int submit_help(OcBBS*);
static char* key_help();

static double ihost(void* v) {
#if USEBBS
	OcBBS* bbs = (OcBBS*)v;
	return double(bbs->myid());
#else
	return nrnmpi_myid;
#endif
}

void bbs_done() {
#if USEBBS
	Symbol* sym = hoc_lookup("ParallelContext");
	sym = hoc_which_template(sym);
	hoc_Item* q, *ql;
	ql = sym->u.ctemplate->olist;
	q = ql->next;
	if (q != ql) {
		Object* ob = OBJ(q);
		OcBBS* bbs = (OcBBS*)ob->u.this_pointer;
		if (bbs->is_master()) {bbs->done();}
	}
#endif
}

static int submit_help(OcBBS* bbs) {
	int id, i, firstarg, style;
	char* pname = 0; // if using Python callable
	posting_ = true;
	bbs->pkbegin();
	i = 1;
	if (hoc_is_double_arg(i)) {
		bbs->pkint((id = (int)chkarg(i++, 0, 1e7)));
	}else{
		bbs->pkint((id = --bbs->next_local_));
	}
	if (ifarg(i+1)) {
#if 1
		int argtypes = 0;
		int ii = 1;
		if (hoc_is_str_arg(i)) {
			style = 1;
			bbs->pkint(style); // "fname", arg1, ... style
			bbs->pkstr(gargstr(i++));
		}else{
			Object* ob = *hoc_objgetarg(i++);
			size_t size;
			if (nrnpy_po2pickle) {
				pname = (*nrnpy_po2pickle)(ob, &size);
			}
			if (pname) {
				style = 3;
				bbs->pkint(style); // pyfun, arg1, ... style
				bbs->pkpickle(pname, size);
				delete [] pname;
			}else{
				style = 2;
				bbs->pkint(style); // [object],"fname", arg1, ... style
				bbs->pkstr(ob->ctemplate->sym->name);
				bbs->pkint(ob->index);
//printf("ob=%s\n", hoc_object_name(ob));
				bbs->pkstr(gargstr(i++));

			}
		}
		firstarg = i;
		for (; ifarg(i); ++i) { // first is least significant
			if (hoc_is_double_arg(i)) {
				argtypes += 1*ii;
			}else if (hoc_is_str_arg(i)) {
				argtypes += 2*ii;
			}else if (is_vector_arg(i)) { //hoc Vector
				argtypes += 3*ii;
			}else{ // must be a PythonObject
				argtypes += 4*ii;
			}
			ii *= 5;
		}
//printf("submit style %d %s argtypes=%o\n", style, gargstr(firstarg-1), argtypes);
		bbs->pkint(argtypes);
		pack_help(firstarg, bbs);
#endif
	}else{
		if (hoc_is_str_arg(i)) {
			bbs->pkint(0); // hoc statement style
			bbs->pkstr(gargstr(i));
		}else if (nrnpy_po2pickle) {
			size_t size;
			pname = (*nrnpy_po2pickle)(*hoc_objgetarg(i), &size);
			bbs->pkint(3); // pyfun with no arg style
			bbs->pkpickle(pname, size);
			delete [] pname;
		}
	}
	posting_ = false;
	return id;
}

static double submit(void *v) {
	int id;
	OcBBS* bbs = (OcBBS*)v;
	id = submit_help(bbs);
	bbs->submit(id);
	return double(id);
}
	
static double context(void *v) {
	OcBBS* bbs = (OcBBS*)v;
	submit_help(bbs);
//	printf("%d context %s %s\n", bbs->myid(), hoc_object_name(*hoc_objgetarg(1)), gargstr(2));
	bbs->context();
	return 1.;
}
	
static double working(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	int id;
	boolean b = bbs->working(id, bbs->retval_, bbs->userid_);
	if (b) {
		return double(id);
	}else{
		return 0.;
	}
}

static double retval(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->retval_;
}

static double userid(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return (double)bbs->userid_;
}

static double nhost(void* v) {
#if defined(HAVE_STL)
	OcBBS* bbs = (OcBBS*)v;
	return double(bbs->nhost());
#else
	return nrnmpi_numprocs;
#endif
}

static double worker(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->worker();
	return 0.;
}

static double done(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->done();
	return 0.;
}

static void pack_help(int i, OcBBS* bbs) {
	if (!posting_) {
		bbs->pkbegin();
		posting_ = true;
	}
	for (; ifarg(i); ++i) {
		if (hoc_is_double_arg(i)) {
			bbs->pkdouble(*getarg(i));
		}else if (hoc_is_str_arg(i)) {
			bbs->pkstr(gargstr(i));
		}else if (is_vector_arg(i)){
			int n; double* px;
			n = vector_arg_px(i, &px);
			bbs->pkint(n);
			bbs->pkvec(n, px);
		}else{ // must be a PythonObject
			size_t size;
			char* s = nrnpy_po2pickle(*hoc_objgetarg(i), &size);
			bbs->pkpickle(s, size);
			delete [] s;
		}
	}
}

static double pack(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	pack_help(1, bbs);
	return 0.;
}

static double post(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	pack_help(2, bbs);
	posting_ = false;
	if (hoc_is_str_arg(1)) {
		bbs->post(gargstr(1));
	}else{
		char key[50];
		sprintf(key, "%g", *getarg(1));
		bbs->post(key);
	}
	return 1.;
}

static void unpack_help(int i, OcBBS* bbs) {
	for (; ifarg(i); ++i) {
		if (hoc_is_pdouble_arg(i)) {
			*hoc_pgetarg(i) = bbs->upkdouble();
		}else if (hoc_is_str_arg(i)) {
			char* s = bbs->upkstr();
			char** ps = hoc_pgargstr(i);
			hoc_assign_str(ps, s);
			delete [] s;
		}else if (is_vector_arg(i)){
			Vect* vec = vector_arg(i);
			int n = bbs->upkint();
			vec->resize(n);
			bbs->upkvec(n, vec->vec());
		}else{
hoc_execerror("pc.unpack can only unpack str, scalar, or Vector.",
"use pc.upkpyobj to unpack a Python Object");
		}
	}
}

static double unpack(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	unpack_help(1, bbs);
	return 1.;
}

static double upkscalar(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->upkdouble();
}

static char** upkstr(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	char* s = bbs->upkstr();
	char** ps = hoc_pgargstr(1);
	hoc_assign_str(ps, s);
	delete [] s;
	return ps;
}

static Object** upkvec(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	Vect* vec;
	int n = bbs->upkint();
	if (ifarg(1)) {
		vec = vector_arg(1);
		vec->resize(n);
	}else{
		vec = new Vect(n);
	}
	bbs->upkvec(n, vec->vec());
	return vec->temp_objvar();
}

static Object** upkpyobj(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	size_t n;
	char* s = bbs->upkpickle(&n);
	assert(nrnpy_pickle2po);
	Object* po = (*nrnpy_pickle2po)(s, n);
	delete [] s;
	return hoc_temp_objptr(po);
}

static Object** pyret(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->pyret();
}
Object** BBS::pyret() {
	assert(impl_->pickle_ret_);
	assert(nrnpy_pickle2po);
	Object* po = (*nrnpy_pickle2po)(impl_->pickle_ret_, impl_->pickle_ret_size_);
	delete [] impl_->pickle_ret_;
	impl_->pickle_ret_ = 0;
	impl_->pickle_ret_size_ = 0;
	return hoc_temp_objptr(po);
}

static char* key_help() {
	static char key[50];
	if (hoc_is_str_arg(1)) {
		return gargstr(1);
	}else{
		sprintf(key, "%g", *getarg(1));
		return key;
	}
}

static double take(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->take(key_help());
	unpack_help(2, bbs);
	return 1.;
}

static double look(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	if (bbs->look(key_help())) {
		unpack_help(2, bbs);
		return 1.;
	}
	return 0.;
}

static double look_take(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	if (bbs->look_take(key_help())) {
		unpack_help(2, bbs);
		return 1.;
	}
	return 0.;
}

static double pctime(void* v) {
	return ((OcBBS*)v)->time();
}

static double vtransfer_time(void* v) {
	int mode = ifarg(1) ? int(chkarg(1, 0., 2.)) : 0;
	if (mode == 2) {
		return nrnmpi_rtcomp_time_;
#if PARANEURON
	}else if (mode == 1) {
		return nrnmpi_splitcell_wait_;
	}else{
		return nrnmpi_transfer_wait_;
	}
#else
	}
	return 0;
#endif
}

static double mech_time(void* v) {
	if (ifarg(1)) {
		if (nrn_mech_wtime_) {
			int i = (int)chkarg(1, 0, n_memb_func-1);
			return nrn_mech_wtime_[i];
		}
	}else{
		if (!nrn_mech_wtime_) {
			nrn_mech_wtime_ = new double[n_memb_func];
		}
		for (int i=0; i < n_memb_func; ++i) {
			nrn_mech_wtime_[i] = 0.0;
		}
	}
	return 0;
}

static double wait_time(void* v) {
	double w = ((OcBBS*)v)->wait_time();
	return w;
}

static double step_time(void* v) {
	double w =  ((OcBBS*)v)->integ_time();
#if PARANEURON
	w -= nrnmpi_transfer_wait_ + nrnmpi_splitcell_wait_;
#endif
	return w;
}

static double send_time(void* v) {
	int arg = ifarg(1) ? int(chkarg(1, 0, 10)) : 0;
	if (arg) {
		return nrn_bgp_receive_time(arg);
	}
	return ((OcBBS*)v)->send_time();
}

static double event_time(void* v) {
	return 0.;
}

static double integ_time(void* v) {
	return 0.;
}

static double set_gid2node(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->set_gid2node(int(chkarg(1, 0, MD)), int(chkarg(2, 0, MD)));
	return 0.;
}

static double gid_exists(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return int(bbs->gid_exists(int(chkarg(1, 0, MD))));
}

static double cell(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->cell();
	return 0.;
}

static double threshold(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->threshold();
}

static double spcompress(void* v) {
	int nspike = -1;
	boolean gid_compress = true;
	int xchng_meth = 0;
	if (ifarg(1)) {
		nspike = (int)chkarg(1, -1, MD);
	}
	if (ifarg(2)) {
		gid_compress = (chkarg(2, 0, 1) ? true : false);
	}
	if (ifarg(3)) {
		xchng_meth = (int)chkarg(3, 0, 1);
	}
	return (double)nrnmpi_spike_compress(nspike, gid_compress, xchng_meth);
}

static double splitcell_connect(void* v) {
	int that_host = (int)chkarg(1, 0, nrnmpi_numprocs-1);
	// also needs a currently accessed section that is the root of this_tree
	nrnmpi_splitcell_connect(that_host);
	return 0.;
}

static double multisplit(void* v) {
	double x = -1.;
	int sid = -1;
	int backbone_style = 2;
	int reducedtree_host = 0;
	if (ifarg(1)) {
		x = chkarg(1, 0, 1);
		sid = (int)chkarg(2, 0, (double)(0x7fffffff));
	}
	if (ifarg(3)) {
		backbone_style = (int)chkarg(3, 0, 2);
	}
	// also needs a currently accessed section
	nrnmpi_multisplit(x, sid, backbone_style);
	return 0.;
}

static double gid_clear(void* v) {
	int arg = 0;
	if (ifarg(1)){
		arg = int(chkarg(1, 0, 3));
	}
	nrnmpi_gid_clear(arg);
	return 0.;
}

static double outputcell(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	int gid = int(chkarg(1, 0., MD));
	bbs->outputcell(gid);
	return 0.;
}

static double spike_record(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	int gid = int(chkarg(1, 0., MD));
	IvocVect* spikevec = vector_arg(2);
	IvocVect* gidvec = vector_arg(3);	
	bbs->spike_record(gid, spikevec, gidvec);
	return 0.;
}

static double psolve(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->netpar_solve(chkarg(1, t, 1e9));
	return 0.;
}

static double set_maxstep(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->netpar_mindelay(chkarg(1, 1e-6, 1e9));
}

static double spike_stat(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	int nsend, nsendmax, nrecv, nrecv_useful;
	nsend = nsendmax = nrecv = nrecv_useful = 0;
	bbs->netpar_spanning_statistics(&nsend, &nsendmax, &nrecv, &nrecv_useful);
	if (ifarg(1)) { *hoc_pgetarg(1) = nsend; }
	if (ifarg(2)) { *hoc_pgetarg(2) = nrecv; }
	if (ifarg(3)) { *hoc_pgetarg(3) = nrecv_useful; }
	return double(nsendmax);
}

static double maxhist(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	IvocVect* vec = ifarg(1) ? vector_arg(1) : nil;
	if (vec) { hoc_obj_ref(vec->obj_); }
	vec = bbs->netpar_max_histogram(vec);
	if (vec) { hoc_obj_unref(vec->obj_); }
	return 0.;
}

static double source_var(void*) { // &source_variable, source_global_index
	// At BEFORE BREAKPOINT, the value of variable is sent to the
	// target machine(s).  This can only be executed on the
	// source machine (where source_variable exists).
	nrnmpi_source_var();
	return 0.;
}

static double target_var(void*) { // &target_variable, source_global_index
	// At BEFORE BREAKPOINT, the value of the target_variable is set
	// to the value of the source variable associated
	// with the source_global_index.  This can only be executed on the
	// target machine (where target_variable exists).
	nrnmpi_target_var();
	return 0.;
}

static double setup_transfer(void*) { // after all source/target and before init and run
	nrnmpi_setup_transfer();
	return 0.;
}

static double barrier(void*) {
	// return wait time
	double t = 0.;
#if NRNMPI
	if (nrnmpi_numprocs > 1) {
		t = nrnmpi_wtime();
		nrnmpi_barrier();
		t = nrnmpi_wtime() - t;
	}
	errno = 0;
#endif
	return t;
}

static double allreduce(void*) {
	// type 1,2,3 sum, max, min
	double val = *getarg(1);
#if NRNMPI
	if (nrnmpi_numprocs > 1) {
		int type = (int)chkarg(2, 1, 3);
		val = nrnmpi_dbl_allreduce(val, type);
	}
	errno = 0;
#endif
	return val;
}

static double allgather(void*) {
	double val = *getarg(1);
	Vect* vec = vector_arg(2);
	vector_resize(vec, nrnmpi_numprocs);
	double* px = vector_vec(vec);

#if NRNMPI
	if (nrnmpi_numprocs > 1) {
		nrnmpi_dbl_allgather(&val, px, 1);
		errno = 0;
	}else{
		px[0] = val;
	}
#else
	px[0] = val;
#endif
	return 0.;
}

static double alltoall(void*) {
	int i, ns, np = nrnmpi_numprocs;
	Vect* vsrc = vector_arg(1);
	Vect* vscnt = vector_arg(2);
	ns = vector_capacity(vsrc);
	double* s = vector_vec(vsrc);
	if (vector_capacity(vscnt) != np) {
		hoc_execerror("size of source counts vector is not nhost", 0);
	}
	double* x = vector_vec(vscnt);
	int* scnt = new int[np];
	int* sdispl = new int[np+1];
	sdispl[0] = 0;
	for (i=0; i < np; ++i) {
		scnt[i] = int(x[i]);
		sdispl[i+1] = sdispl[i] + scnt[i];
	}
	if (ns != sdispl[np]) {
		hoc_execerror("sum of source counts is not the size of the src vector", 0);
	}
	Vect* vdest = vector_arg(3);
#if NRNMPI
	int* rcnt = new int[np];
	int* rdispl = new int[np + 1];
	int* c = new int[np];
	rdispl[0] = 0;
	for (i=0; i < np; ++i) {
		c[i] = 1;
		rdispl[i+1] = i+1;
	}
	nrnmpi_int_alltoallv(scnt, c, rdispl, rcnt, c, rdispl);
	delete [] c;
	for (i=0; i < np; ++i) {
		rdispl[i+1] = rdispl[i] + rcnt[i];
	}
	vector_resize(vdest, rdispl[np]);
	double* r = vector_vec(vdest);
	nrnmpi_dbl_alltoallv(s, scnt, sdispl, r, rcnt, rdispl);
	delete [] rcnt;
	delete [] rdispl;
#else
	vector_resize(vdest, ns);
	double* r = vector_vec(vdest);
	for (i=0; i < ns; ++i) {
		r[i] = s[i];
	}
#endif
	delete [] scnt;
	delete [] sdispl;
	return 0.;
}

static double broadcast(void*) {
	int srcid = int(chkarg(2, 0, nrnmpi_numprocs - 1));
	int cnt = 0;
#if NRNMPI
    if (nrnmpi_numprocs > 1) {
	if (hoc_is_str_arg(1)) {
		char* s;
		if (srcid == nrnmpi_myid) {
			s = gargstr(1);
			cnt = strlen(s);
		}
		nrnmpi_int_broadcast(&cnt, 1, srcid);
		if (srcid != nrnmpi_myid) {
			s = new char[cnt];
		}
		nrnmpi_char_broadcast(s, cnt, srcid);
		if (srcid != nrnmpi_myid) {
			hoc_assign_str(hoc_pgargstr(1), s);
			delete [] s;
		}
	}else{
		Vect* vec = vector_arg(1);
		if (srcid == nrnmpi_myid) {
			cnt = vec->capacity();
		}
		nrnmpi_int_broadcast(&cnt, 1, srcid);
		if (srcid != nrnmpi_myid) {
			vec->resize(cnt);
		}
		nrnmpi_dbl_broadcast(vector_vec(vec), cnt, srcid);
	}
    }else{
#else
    {
#endif
	if (hoc_is_str_arg(1)) {
		cnt = strlen(gargstr(1));
	}else{
		cnt = vector_arg(1)->capacity();
	}
    }
	return double(cnt);
}

static double checkpoint(void*) {
#if BLUEGENE_CHECKPOINT
	int i = BGLCheckpoint();
	return double(i);
#else
	return 0.;
#endif
}

static double nthrd(void*) {
	int ip = 1;
	if (ifarg(1)) {
		if (ifarg(2)) { ip = int(chkarg(2, 0, 1)); }
		nrn_threads_create(int(chkarg(1, 1, 1e5)), ip);
	}
	return double(nrn_nthread);
}

static double partition(void*) {
	Object* ob = 0;
	int it;
	if (ifarg(2)) {
		ob = *hoc_objgetarg(2);
		if (ob) {
			check_obj_type(ob, "SectionList");
		}
	}
	if (ifarg(1)) {
		it = (int)chkarg(1, 0, nrn_nthread - 1);
		nrn_thread_partition(it, ob);
	}else{
		for (it = 0; it < nrn_nthread; ++it) {
			nrn_thread_partition(it, ob);
		}
	}
	return 0.0;
}

static double thread_stat(void*) {
	nrn_thread_stat();
	return 0.0;
}

static double thread_busywait(void*) {
	int old = nrn_allow_busywait(int(chkarg(1,0,1)));
	return double(old);
}

static double thread_how_many_proc(void*) {
	int i = nrn_how_many_processors();
	return double(i);
}

static double sec_in_thread(void*) {
	Section* sec = chk_access();
	return double(sec->pnode[0]->_nt->id);
}

static double thread_ctime(void*) {
	int i;
#if 1
	if (ifarg(1)) {
		i = int(chkarg(1, 0, nrn_nthread));
		return nrn_threads[i]._ctime;
	}else{
		for (i=0; i < nrn_nthread; ++i) {
			nrn_threads[i]._ctime = 0.0;
		}
	}
#endif
	return 0.0;
}

static Object** gid2obj(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->gid2obj(int(chkarg(1, 0, MD)));
}

static Object** gid2cell(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->gid2cell(int(chkarg(1, 0, MD)));
}

static Object** gid_connect(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	return bbs->gid_connect(int(chkarg(1, 0, MD)));
}

static Member_func members[] = {
	"submit", submit,
	"working", working,
	"retval", retval,
	"userid", userid,
	"pack", pack,
	"post", post,
	"unpack", unpack,
	"upkscalar", upkscalar,
	"take", take,
	"look", look,
	"look_take", look_take,
	"runworker", worker,
	"done", done,
	"id", ihost,
	"nhost", nhost,
	"context", context,

	"time", pctime,
	"wait_time", wait_time,
	"step_time", step_time,
	"send_time", send_time,
	"event_time", event_time,
	"integ_time", integ_time,
	"vtransfer_time", vtransfer_time,
	"mech_time", mech_time,

	"set_gid2node", set_gid2node,
	"gid_exists", gid_exists,
	"outputcell", outputcell,
	"cell", cell,
	"threshold", threshold,
	"spike_record", spike_record,
	"psolve", psolve,
	"set_maxstep", set_maxstep,
	"spike_statistics", spike_stat,
	"max_histogram", maxhist,
	"checkpoint", checkpoint,
	"spike_compress", spcompress,
	"gid_clear", gid_clear,

	"source_var", source_var,
	"target_var", target_var,
	"setup_transfer", setup_transfer,
	"splitcell_connect", splitcell_connect,
	"multisplit", multisplit,

	"barrier", barrier,
	"allreduce", allreduce,
	"allgather", allgather,
	"alltoall", alltoall,
	"broadcast", broadcast,

	"nthread", nthrd,
	"partition", partition,
	"thread_stat", thread_stat,
	"thread_busywait", thread_busywait,
	"thread_how_many_proc", thread_how_many_proc,
	"sec_in_thread", sec_in_thread,
	"thread_ctime", thread_ctime,

	0,0
};

static Member_ret_str_func retstr_members[] = {
	"upkstr", upkstr,
	0,0
};

static Member_ret_obj_func retobj_members[] = {
	"upkvec", upkvec,
	"gid2obj", gid2obj,
	"gid2cell", gid2cell,
	"gid_connect", gid_connect,
	"upkpyobj", upkpyobj,
	"pyret", pyret,
	0,0
};

static void* cons(Object*) {
	// not clear at moment what is best way to handle nested context
	int i = -1;
	if (ifarg(1)) {
		i = int(chkarg(1, 0, 10000));
	}
	OcBBS* bbs = new OcBBS(i);
	bbs->ref();
	return bbs;
}

static void destruct(void* v) {
	OcBBS* bbs = (OcBBS*)v;
	bbs->unref();
}

void ParallelContext_reg() {
	class2oc("ParallelContext", cons, destruct, members, nil,
		retobj_members, retstr_members);
}

char* BBSImpl::execute_helper(size_t* size) {
	char* s;
	int style = upkint();
	char* rs = 0;
	*size = 0;
	switch (style) {
	case 0:
		s = upkstr();
		hoc_obj_run(s, nil);
		delete [] s;
		break;
	default: {
#if 1
		int i, j;
		size_t npickle;
		Symbol* fname = 0;
		Object* ob = nil;
		char* sarg[20]; // upto 20 argument may be strings
		int ns = 0; // number of args that are strings
		int narg = 0; // total number of args
		if (style == 2) { // object first
			s = upkstr(); // template name
			i = upkint(); // object index
//printf("template |%s| index=%d\n", s, i);
			Symbol* sym = hoc_lookup(s);
			if (sym) {
				sym = hoc_which_template(sym);
			}
			if (!sym) {
				hoc_execerror(s, "is not a template");
			}
			hoc_Item* q, *ql;
			ql = sym->u.ctemplate->olist;
			ITERATE(q, ql) {
				ob = OBJ(q);
				if (ob->index == i) {
					break;
				}
				ob = nil;
			}
			if (!ob) {
fprintf(stderr, "%s[%d] is not an Object in this process\n", s, i);
hoc_execerror("ParallelContext execution error", 0);
			}
			delete [] s;
			s = upkstr();
			fname = hoc_table_lookup(s, sym->u.ctemplate->symtable);
		}else if (style == 3) { // Python callable
			s = upkpickle(&npickle);
		}else{
			s = upkstr();
			fname = hoc_lookup(s);
		}
//printf("execute helper style %d fname=%s obj=%s\n", style, fname->name, hoc_object_name(ob));
		if (style != 3 && !fname) {
fprintf(stderr, "%s not a function in %s\n", s, hoc_object_name(ob));
hoc_execerror("ParallelContext execution error", 0);
		}
		int argtypes = upkint(); // first is least signif
		for (j = argtypes; (i = j%5) != 0; j /= 5) {
			++narg;
			if (i == 1) {
				double x = upkdouble();
//printf("arg %d scalar %g\n", narg, x);
				hoc_pushx(x);
			}else if (i == 2) {
				sarg[ns] = upkstr();
//printf("arg %d string |%s|\n", narg, sarg[ns]);
				hoc_pushstr(sarg+ns);
				ns++;
			}else if (i == 3) {
				int n;
				n = upkint();
				Vect* vec = new Vect(n);
//printf("arg %d vector size=%d\n", narg, n);
				upkvec(n, vec->vec());
				hoc_pushobj(vec->temp_objvar());
			}else{ //PythonObject
				size_t n;
				char* s = upkpickle(&n);
				assert(nrnpy_pickle2po);
				Object* po = nrnpy_pickle2po(s, n);
				delete [] s;
				hoc_pushobj(hoc_temp_objptr(po));
			}
		}
		if (style == 3) {
			assert(nrnpy_callpicklef);
			if (pickle_ret_) {
				delete [] pickle_ret_;
				pickle_ret_ = 0;
				pickle_ret_size_ = 0;
			}
			rs = (*nrnpy_callpicklef)(s, npickle, narg, size);
			hoc_ac_ = 0.;
		}else{
			hoc_ac_ = hoc_call_objfunc(fname, narg, ob);
		}
		delete [] s;
		for (i=0; i < ns; ++i) {
			delete [] sarg[i];
		}
#endif
	    }
		break;
	}
	return rs;
}

void BBSImpl::return_args(int id) {
	// the message has been set up by the subclass
	// perhaps it would be better to do this directly
	// and avoid the meaningless create and delete.
	// but then they all would have to know this format
	int i;
	char* s;
//printf("BBSImpl::return_args(%d):\n", id);
	i = upkint(); // userid
	int style = upkint();
//printf("message userid=%d style=%d\n", i, style);
	switch (style) {
	case 0:
		s = upkstr(); // the statement
//printf("statement |%s|\n", s);
		delete [] s;
		break;
	case 2: // obj first
		s = upkstr(); // template name
		i = upkint();	// instance index
//printf("object %s[%d]\n", s, i);
		delete [] s;
		//fall through
	case 1:
		s = upkstr(); //fname
		i = upkint(); // arg manifest
//printf("fname=|%s| manifest=%o\n", s, i);
		delete [] s;
		break;
	case 3:
		size_t n;
		s = upkpickle(&n); //pickled callable
		i = upkint(); // arg manifest
		delete [] s;
		break;
	}
	// now only args are left and ready to unpack.
}
