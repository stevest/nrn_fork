#include <../../nrnconf.h>
#include <assert.h>
#include <nrnmpi.h>

int nrnmpi_numprocs = 1; /* size */
int nrnmpi_myid = 0; /* rank */
int nrnmpi_numprocs_world = 1;
int nrnmpi_myid_world = 0;
int nrnmpi_numprocs_bbs = 1;
int nrnmpi_myid_bbs = 0;

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
extern double nrn_timeus();

#if NRNMPI
#include <mpi.h>
#define asrt(arg) assert(arg == MPI_SUCCESS)
#define USE_HPM 0
#if USE_HPM
#include <libhpm.h>
#endif

int nrnmusic;
#if NRN_MUSIC
MPI_Comm nrnmusic_comm;
extern void nrnmusic_init(int* parg, char*** pargv);
extern void nrnmusic_terminate();
#endif

int nrnmpi_use; /* NEURON does MPI init and terminate?*/
MPI_Comm nrnmpi_world_comm;
MPI_Comm nrnmpi_comm;
MPI_Comm nrn_bbs_comm;
static MPI_Group grp_bbs;
static MPI_Group grp_net;

extern void nrnmpi_spike_initialize();

#define nrnmpidebugleak 0
#if nrnmpidebugleak
extern void nrnmpi_checkbufleak();
#endif

static int nrnmpi_under_nrncontrol_;
#endif

void nrnmpi_init(int nrnmpi_under_nrncontrol, int* pargc, char*** pargv) {
#if NRNMPI
	int i, b, flag;
	static int called = 0;
	if (called) { return; }
	called = 1;
	nrnmpi_use = 1;
	nrnmpi_under_nrncontrol_ = nrnmpi_under_nrncontrol;
	if( nrnmpi_under_nrncontrol_ ) {
#if 0
{int i;
printf("nrnmpi_init: argc=%d\n", *pargc);
for (i=0; i < *pargc; ++i) {
	printf("%d |%s|\n", i, (*pargv)[i]);
}
}
#endif

#if NRN_MUSIC
	nrnmusic_init(pargc, pargv); /* see src/nrniv/nrnmusic.cpp */
#endif

#if !ALWAYS_CALL_MPI_INIT
	/* this is not good. depends on mpirun adding at least one
	   arg that starts with -p4 but that probably is dependent
	   on mpich and the use of the ch_p4 device. We are trying to
	   work around the problem that MPI_Init may change the working
	   directory and so when not invoked under mpirun we would like to
	   NOT call MPI_Init.
	*/
		b = 0;
		for (i=0; i < *pargc; ++i) {
			if (strncmp("-p4", (*pargv)[i], 3) == 0) {
				b = 1;
				break;
			}
			if (strcmp("-mpi", (*pargv)[i]) == 0) {
				b = 1;
				break;
			}
		}
		if (nrnmusic) { b = 1; }
		if (!b) {
			nrnmpi_use = 0;
			nrnmpi_under_nrncontrol_ = 0;
			return;
		}
#endif
		MPI_Initialized(&flag);

		if (!flag && MPI_Init(pargc, pargv) != MPI_SUCCESS) {
		  printf("MPI_INIT failed\n");
		}

#if NRN_MUSIC
		if (nrnmusic) {
			asrt(MPI_Comm_dup(nrnmusic_comm, &nrnmpi_world_comm));
		}else{
#else
		{
#endif
			asrt(MPI_Comm_dup(MPI_COMM_WORLD, &nrnmpi_world_comm));
		}
	}
	grp_bbs = MPI_GROUP_NULL;
	grp_net = MPI_GROUP_NULL;
	asrt(MPI_Comm_dup(nrnmpi_world_comm, &nrnmpi_comm));
	asrt(MPI_Comm_dup(nrnmpi_world_comm, &nrn_bbs_comm));
	asrt(MPI_Comm_rank(nrnmpi_world_comm, &nrnmpi_myid_world));
	asrt(MPI_Comm_size(nrnmpi_world_comm, &nrnmpi_numprocs_world));
	nrnmpi_numprocs = nrnmpi_numprocs_bbs = nrnmpi_numprocs_world;
	nrnmpi_myid = nrnmpi_myid_bbs = nrnmpi_myid_world;
	nrnmpi_spike_initialize();
	/*begin instrumentation*/
#if USE_HPM
	hpmInit( nrnmpi_myid_world, "mpineuron" );
#endif
#if 0
{int i;
printf("nrnmpi_init: argc=%d\n", *pargc);
for (i=0; i < *pargc; ++i) {
	printf("%d |%s|\n", i, (*pargv)[i]);
}
}
#endif
#if 1
	if (nrnmpi_myid == 0) {
		printf("numprocs=%d\n", nrnmpi_numprocs_world);
	}
#endif

#endif /* NRNMPI */



}

double nrnmpi_wtime() {
#if NRNMPI
	if (nrnmpi_use) {
		return MPI_Wtime();
	}
#endif
	return nrn_timeus();
}

void nrnmpi_terminate() {
#if NRNMPI
	if (nrnmpi_use) {
#if 0
		printf("%d nrnmpi_terminate\n", nrnmpi_myid_world);
#endif
#if USE_HPM
		hpmTerminate( nrnmpi_myid_world );
#endif
		if( nrnmpi_under_nrncontrol_ ) {
#if NRN_MUSIC
			if (nrnmusic) {
				nrnmusic_terminate();
			}else
#endif
			MPI_Finalize();
		}
		nrnmpi_use = 0;
#if nrnmpidebugleak
		nrnmpi_checkbufleak();
#endif
	}
#endif /*NRNMPI*/
}

void nrnmpi_abort(int errcode) {
#if NRNMPI
	int flag;
	MPI_Initialized(&flag);
	if (flag) {
		MPI_Abort(MPI_COMM_WORLD, errcode);
	}else{
		abort();
	}
#else
	abort();
#endif
}

#if NRNMPI
void nrnmpi_subworlds(int n) {
	if (nrnmpi_use != 1) { return; }
	asrt(MPI_Comm_free(&nrnmpi_comm));
	asrt(MPI_Comm_free(&nrn_bbs_comm));
	if (grp_bbs != MPI_GROUP_NULL) { asrt(MPI_Group_free(&grp_bbs)); }
	if (grp_net != MPI_GROUP_NULL) { asrt(MPI_Group_free(&grp_net)); }
	// special cases
	if (n == 1) {
		MPI_Group wg;
		int r = nrnmpi_myid_world;
		asrt(MPI_Comm_group(nrnmpi_world_comm, &wg));
		asrt(MPI_Group_incl(wg, 1, &r, &grp_net));
		asrt(MPI_Group_free(&wg));
		asrt(MPI_Comm_dup(nrnmpi_world_comm, &nrn_bbs_comm));
		asrt(MPI_Comm_create(nrnmpi_world_comm, grp_net, &nrnmpi_comm));
	}else if (n == nrnmpi_numprocs_world) {
		MPI_Group wg;
		int r = nrnmpi_myid_world;
		asrt(MPI_Comm_group(nrnmpi_world_comm, &wg));
		asrt(MPI_Group_incl(wg, 1, &r, &grp_bbs));
		asrt(MPI_Group_free(&wg));
		asrt(MPI_Comm_dup(nrnmpi_world_comm, &nrnmpi_comm));
		asrt(MPI_Comm_create(nrnmpi_world_comm, grp_bbs, &nrn_bbs_comm));
	}else{
		assert(0);
	}
	asrt(MPI_Comm_rank(nrnmpi_comm, &nrnmpi_myid));
	asrt(MPI_Comm_size(nrnmpi_comm, &nrnmpi_numprocs));
	asrt(MPI_Comm_rank(nrn_bbs_comm, &nrnmpi_myid_bbs));
	asrt(MPI_Comm_size(nrn_bbs_comm, &nrnmpi_numprocs_bbs));
}
#endif
