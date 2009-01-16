// included by netpar.cpp

/*
Overall exchange strategy

When a cell spikes, it immediately does a DCMF_Multicast of
(int gid, double spiketime) to all the target machines that have
cells that need to receive this spike by spiketime + delay
I'd like to cycle through a list of mconfig.nconnections so that
I don't have to wait for my single connection to complete the previous
broadcast when there is a high density of generated spikes but I need
to take care of my bus error issues first.

In order to minimize the number of nrnmpi_bgp_conserve tests
(and potentially abandon them altogether if I can ever guarantee
that exchange time is less than half the computation time), I divide the
minimum delay integration intervals into two equal subintervals.
So if a spike is generated in an even subinterval, I do not have
to include it in the conservation check until the end of the next even
subinterval.

When a spike is received (DMA interrupt) it is placed in even or odd
buffers (depending on whether the coded gid is positive or negative)

At the end of a computation subinterval the even or odd buffer spikes
are enqueued in the priority queue after checking that the number
of spikes sent is equal to the number of spikes sent.
*/

extern "C" {
extern void nrnmpi_int_allgatherv(int*, int*, int*, int*);
extern void nrnmpi_int_alltoallv(int*, int*, int*, int*, int*, int*);
extern void nrnmpi_int_gather(int*, int*, int, int);
extern void nrnmpi_int_gatherv(int*, int, int*, int*, int*, int);
extern void nrnmpi_barrier();

extern IvocVect* vector_arg(int);
extern void vector_resize(IvocVect*, int);
}

static unsigned long long dmasend_time_;
static int n_xtra_cons_check_;
#define MAXNCONS 10
#if MAXNCONS
static int xtra_cons_hist_[MAXNCONS+1];
#endif

#if BGPDMA == 2

#define TBUFSIZE (1<<15)
#else
#define TBUFSIZE 0
#endif

#if TBUFSIZE
static unsigned long tbuf_[TBUFSIZE];
static int itbuf_;
#define TBUF tbuf_[itbuf_++] = (unsigned long)DCMF_Timebase();
#else
#define TBUF /**/
#endif

#include <structpool.h>

declareStructPool(SpkPool, NRNMPI_Spike)
implementStructPool(SpkPool, NRNMPI_Spike)

#define BGP_RECEIVEBUFFER_SIZE 10000
class BGP_ReceiveBuffer {
public:
	BGP_ReceiveBuffer();
	virtual ~BGP_ReceiveBuffer();
	void init();
	void incoming(int gid, double spiketime);
	void enqueue();
	int size_;
	int count_;
	int maxcount_;
	int busy_;
	int nsend_, nrecv_; // for checking conservation
	unsigned long long timebase_;
	NRNMPI_Spike** buffer_;
	SpkPool* pool_;

	void enqueue1();
	void enqueue2();
	PreSyn** psbuf_;
};
#define ENQUEUE 1 // 0 use psbuf_
static BGP_ReceiveBuffer* bgp_receive_buffer[BGP_INTERVAL];
static int current_rbuf, next_rbuf;
#if BGP_INTERVAL == 2
// note that if a spike is supposed to be received by bgp_receive_buffer[1]
// then during transmission its gid is complemented.
#endif

BGP_ReceiveBuffer::BGP_ReceiveBuffer() {
	busy_ = 0;
	count_ = 0;
	size_ = BGP_RECEIVEBUFFER_SIZE;
	buffer_ = new NRNMPI_Spike*[size_];
	pool_ = new SpkPool(BGP_RECEIVEBUFFER_SIZE);
#if ENQUEUE
	psbuf_ = new PreSyn*[size_];
#else
	psbuf_ = 0;
#endif
}
BGP_ReceiveBuffer::~BGP_ReceiveBuffer() {
	assert(busy_ == 0);
	for (int i = 0; i < count_; ++i) {
		pool_->hpfree(buffer_[i]);
	}
	delete [] buffer_;
	delete pool_;
	if (psbuf_) delete [] psbuf_;
}
void BGP_ReceiveBuffer::init() {
	timebase_ = 0;
	nsend_ = nrecv_ = busy_ = 0;
	for (int i = 0; i < count_; ++i) {
		pool_->hpfree(buffer_[i]);
	}
	count_ = 0;
}
void BGP_ReceiveBuffer::incoming(int gid, double spiketime) {
//printf("%d %lx.incoming %g %g %d\n", nrnmpi_myid, (long)this, t, spk->spiketime, spk->gid);
	assert(busy_ == 0);
	busy_ = 1;
#if 1
	if (count_ >= size_) {
		size_ *= 2;
		NRNMPI_Spike** newbuf = new NRNMPI_Spike*[size_];
		for (int i = 0; i < count_; ++i) {
			newbuf[i] = buffer_[i];
		}
		delete [] buffer_;
		buffer_ = newbuf;
		if (psbuf_) {
			delete [] psbuf_;
			psbuf_ = new PreSyn*[size_];
		}
	}
	NRNMPI_Spike* spk = pool_->alloc();
	spk->gid = gid;
	spk->spiketime = spiketime;
	buffer_[count_++] = spk;
	if (maxcount_ < count_) { maxcount_ = count_; }
#endif
	++nrecv_;
	busy_ = 0;	
}
void BGP_ReceiveBuffer::enqueue() {
//printf("%d %lx.enqueue count=%d t=%g nrecv=%d nsend=%d\n", nrnmpi_myid, (long)this, t, count_, nrecv_, nsend_);
	assert(busy_ == 0);
	busy_ = 1;
#if 1
	for (int i=0; i < count_; ++i) {
		NRNMPI_Spike* spk = buffer_[i];
		PreSyn* ps;
		assert(gid2in_->find(spk->gid, ps));
		ps->send(spk->spiketime, net_cvode_instance, nrn_threads);
		pool_->hpfree(spk);
	}
#endif
	count_ = 0;
	nrecv_ = 0;
	nsend_ = 0;
	busy_ = 0;
}

void BGP_ReceiveBuffer::enqueue1() {
//printf("%d %lx.enqueue count=%d t=%g nrecv=%d nsend=%d\n", nrnmpi_myid, (long)this, t, count_, nrecv_, nsend_);
	assert(busy_ == 0);
	busy_ = 1;
#if 1
	for (int i=0; i < count_; ++i) {
		NRNMPI_Spike* spk = buffer_[i];
		PreSyn* ps;
		assert(gid2in_->find(spk->gid, ps));
		psbuf_[i] = ps;
	}
#endif
	busy_ = 0;
}

void BGP_ReceiveBuffer::enqueue2() {
//printf("%d %lx.enqueue count=%d t=%g nrecv=%d nsend=%d\n", nrnmpi_myid, (long)this, t, count_, nrecv_, nsend_);
	assert(busy_ == 0);
	busy_ = 1;
#if 1
	for (int i=0; i < count_; ++i) {
		NRNMPI_Spike* spk = buffer_[i];
		PreSyn* ps = psbuf_[i];
		ps->send(spk->spiketime, net_cvode_instance, nrn_threads);
		pool_->hpfree(spk);
	}
#endif
	count_ = 0;
	nrecv_ = 0;
	nsend_ = 0;
	busy_ = 0;
}

// number of DCMF_Multicast_t to cycle through when not using recordreplay
#define NSEND 10

#if BGPDMA == 2

#include <dcmf_multisend.h>
#include <dcmf.h>

#define PIPEWIDTH 16

static DCMF_Opcode_t* hints_;
static DCMF_Protocol_t protocol __attribute__((__aligned__(16)));
static DCMF_Multicast_Configuration_t mconfig;

struct MyMulticastInfo {
	DCMF_Request_t request __attribute__((__aligned__(16)));
        DCQuad msginfo __attribute__((__aligned__(16)));
	DCMF_Multicast_t msend;
	DCMF_Callback_t cb_done;
	boolean req_in_use;
	boolean record; // when recordreplay, first time Multicast, thereafter  Restart
}__attribute__((__aligned__(16)));
// NSEND of them for cycling, or, if recordreplay, max_persist_ids of them
static int n_mymulticast_; // NSEND or max_persist_ids
static struct MyMulticastInfo* mci_;


// maybe we will use this when a rank sends to itself.
static void spk_ready (int gid, double spiketime) {
	assert(0);
//printf("%d spk_ready %d %g\n", nrnmpi_myid, gid, spiketime);
	// to avoid a race condition (since we may be in the middle of
	// retrieving an item from the event queue) store the incoming
	// events in a receive buffer
	int i = 0;
#if BGP_INTERVAL == 2
	if (gid < 0) {
		gid = ~gid;
		i = 1;
	}
#endif
	bgp_receive_buffer[i]->incoming(gid, spiketime);
	++nrecv_;
}

// async callback for receiver's side of multisend  
static DCMF_Request_t * msend_recv(const DCQuad  * msginfo,
			    unsigned          nquads,
			    unsigned          senderID,
			    const unsigned    sndlen,
			    unsigned          connid,
			    void            * arg,
			    unsigned        * rcvlen,
			    char           ** rcvbuf,
			    unsigned        * pipewidth,
			    DCMF_Callback_t * cb_done)
{
  unsigned long long tb = DCMF_Timebase();
  *rcvlen = 0;
  *rcvbuf = 0;
  * pipewidth       = PIPEWIDTH;
  cb_done->clientdata = 0;
  cb_done->function = 0;

  double t = *((double*)&msginfo->w0);
  int gid = *((int*)&msginfo->w2);
//  printf("%d msend_recv %d %g\n", nrnmpi_myid, spk->gid, spk->spiketime);
	int i = 0;
#if BGP_INTERVAL == 2
	if (gid < 0) {
		gid = ~gid;
		i = 1;
	}
#endif
	bgp_receive_buffer[i]->incoming(gid, t);
	++nrecv_;
  bgp_receive_buffer[i]->timebase_ += DCMF_Timebase() - tb;
  return NULL;
}

double nrn_bgp_receive_time(int type) { // and others
	double rt = 0.;
	switch(type) {
	case 2: //in msend_recv
		if (!use_bgpdma_) { return rt; }
		for (int i = 0; i < n_bgp_interval; ++i) {
			rt += bgp_receive_buffer[i]->timebase_ * DCMF_Tick();
		}
		break;
	case 3: // in BGP_DMAsend::send
		if (!use_bgpdma_) { return rt; }
		rt = dmasend_time_ * DCMF_Tick();
		break;
	case 4: // number of extra conservation checks
		rt = double(n_xtra_cons_check_);
		// and if there is second vector arg then also return the histogram
#if MAXNCONS
		if (ifarg(2) && use_bgpdma_) {
			IvocVect* vec = vector_arg(2);
			vector_resize(vec, MAXNCONS+1);
			for (int i=0; i <= MAXNCONS; ++i) {
				vector_vec(vec)[i] = double(xtra_cons_hist_[i]);
			}
		}
#endif // MAXNCONS
#if TBUFSIZE
		if (ifarg(3)) {
			IvocVect* vec = vector_arg(3);
			vector_resize(vec, itbuf_+1);
			for (int i=0; i <= itbuf_; ++i) {
				vector_vec(vec)[i] = double(tbuf_[i]);
			}
			vector_vec(vec)[itbuf_] = DCMF_Tick();
		}
#endif
		break;
#if ALTHASH
	case 5:
		rt = double(gid2in_->max_chain_length());
		break;
	case 6:
		rt = double(gid2in_->nclash());
		break;
	case 7:
		rt = double(gid2in_->nfind());
		break;
#endif
	}
	return rt;
}

#endif //BGPDMA == 2

extern "C" {
extern void nrnmpi_bgp_comm();
extern void nrnmpi_bgp_multisend(NRNMPI_Spike*, int, int*);
extern int nrnmpi_bgp_single_advance(NRNMPI_Spike*);
extern int nrnmpi_bgp_conserve(int nsend, int nrecv);
}

class BGP_DMASend {
public:
	BGP_DMASend();
	virtual ~BGP_DMASend();
	void send(int gid, double t);
	int ntarget_hosts_;
	int* target_hosts_;
	NRNMPI_Spike spk_;
	int send2self_; // if 1 then send spikes to this host also
#if HAVE_DCMF_RECORD_REPLAY
	unsigned persist_id_;
#endif
};

static int max_ntarget_host;

// Multisend_multicast callback
#if DCMF_VERSION_MAJOR >= 2
static void  multicast_done(void* arg, DCMF_Error_t *) {
#else
static void  multicast_done(void* arg) {
#endif
	boolean* a = (boolean*)arg;
	*a = false;
}

static void bgp_dma_init() {
	for (int i = 0; i < n_bgp_interval; ++i) {
		bgp_receive_buffer[i]->init();
	}
	current_rbuf = 0;
	next_rbuf = n_bgp_interval - 1;
#if BGPDMA == 2
	for (int i=0; i < n_mymulticast_; ++i) {
		mci_[i].req_in_use = false;
	}
#endif
	dmasend_time_ = 0;
	n_xtra_cons_check_ = 0;
#if MAXNCONS
	for (int i=0; i <= MAXNCONS; ++i) {
		xtra_cons_hist_[i] = 0;
	}
#endif // MAXNCONS
}

static int bgp_advance() {
	NRNMPI_Spike spk;
	PreSyn* ps;
	int i = 0;
	while(nrnmpi_bgp_single_advance(&spk)) {
		i += 1;
		int j = 0;
#if BGP_INTERVAL == 2
		if (spk.gid < 0) {
			spk.gid = ~spk.gid;
			j = 1;
		}
#endif
		bgp_receive_buffer[j]->incoming(spk.gid, spk.spiketime);
	}
	nrecv_ += i;
	return i;
}

BGP_DMASend::BGP_DMASend() {
	ntarget_hosts_ = 0;
	target_hosts_ = nil;
	send2self_ = 0;
}

BGP_DMASend::~BGP_DMASend() {
	if (target_hosts_) {
		delete [] target_hosts_;
	}
}

static	int isend;

void BGP_DMASend::send(int gid, double t) {
  if (ntarget_hosts_) {
	spk_.gid = gid;
	spk_.spiketime = t;
#if BGP_INTERVAL == 2
	bgp_receive_buffer[next_rbuf]->nsend_ += ntarget_hosts_;
	if (next_rbuf == 1) {
		spk_.gid = ~spk_.gid;
	}
#else
	bgp_receive_buffer[0]->nsend_ += ntarget_hosts_;
#endif
	nsend_ += 1;
#if BGPDMA == 2
	unsigned long long tb = DCMF_Timebase();

	MyMulticastInfo* mci;
#if HAVE_DCMF_RECORD_REPLAY
	if (use_dcmf_record_replay) {
		mci = mci_ + persist_id_;
	}else{
#else
	{
#endif
		mci = mci_ + isend;
	}

	int acnt = 0;
	while (mci->req_in_use) {
		++acnt;
		DCMF_Messager_advance();
	}
//	if (acnt > 10) { printf("%d multicast %d not done\n", nrnmpi_myid, msend.connection_id);}
	mci->req_in_use = true;
//printf("%d multisend %d %g\n", nrnmpi_myid, gid, t);
	*((double*)&(mci->msginfo.w0)) = spk_.spiketime;
	*((int*)&(mci->msginfo.w2)) = spk_.gid;
	mci->msend.nranks = (unsigned int)ntarget_hosts_;
	mci->msend.ranks = (unsigned int*) target_hosts_;

//printf("%d DCMF_Multicast %d %g %d\n", nrnmpi_myid, msend.connection_id, t, gid);
#if HAVE_DCMF_RECORD_REPLAY
	if (use_dcmf_record_replay) {
		if (mci->record) {
			DCMF_Multicast(&mci->msend);
			mci->record = false;
		}else{
			DCMF_Restart(&mci->request);
		}
	}else{
#else
	{
#endif
		DCMF_Multicast(&mci->msend);
	}
	dmasend_time_ += DCMF_Timebase() - tb;
#else
	nrnmpi_bgp_multisend(&spk_, ntarget_hosts_, target_hosts_);
#endif
    }
	// I am given to understand that multisend cannot send to itself
	if (send2self_) {
		PreSyn* ps;
		assert(gid2in_->find(gid, ps));
		ps->send(t, net_cvode_instance, nrn_threads);
	}
	isend = (++isend)%NSEND;
}




static void determine_source_hosts();
static void determine_targid_count_on_srchost(int* src, int* send);
static void determine_targids_on_srchost(int* s, int* scnt, int* sdispl,
    int* r, int* rcnt, int* rdispl);
static void determine_target_hosts();
static int gathersrcgid(int hostbegin, int totalngid, int* ngid,
	int* thishostgid, int* n, int* displ, int bsize, int* buf);

void bgp_dma_receive() {
//	nrn_spike_exchange();
	TBUF
	double w1, w2;
	int ncons = 0;
	int& s = bgp_receive_buffer[current_rbuf]->nsend_;
	int& r = bgp_receive_buffer[current_rbuf]->nrecv_;
	w1 = nrnmpi_wtime();
#if BGPDMA == 2
	DCMF_Messager_advance();
	TBUF
	// demonstrates that most of the time here is due to load imbalance
#if TBUFSIZE
	nrnmpi_barrier();
#endif
	TBUF
	while (nrnmpi_bgp_conserve(s, r) != 0) {
		DCMF_Messager_advance();
		++ncons;
	}
	TBUF
	n_xtra_cons_check_ += ncons;
#else
	bgp_advance();
	while (nrnmpi_bgp_conserve(s, r) != 0) {
		bgp_advance();
	}
#endif
	w1 = nrnmpi_wtime() - w1;
	w2 = nrnmpi_wtime();
#if TBUFSIZE
	tbuf_[itbuf_++] = (unsigned long)ncons;
	tbuf_[itbuf_++] = (unsigned long)s;
	tbuf_[itbuf_++] = (unsigned long)r;
#endif
#if BGPMDA == 2 && MAXNCONS
	if (ncons > MAXNCONS) { ncons = MAXNCONS; }
	++xtra_cons_hist_[ncons];
#endif // MAXNCONS
#if ENQUEUE
	bgp_receive_buffer[current_rbuf]->enqueue();
#else
	bgp_receive_buffer[current_rbuf]->enqueue1();
	TBUF
	bgp_receive_buffer[current_rbuf]->enqueue2();
#endif
	wt1_ = nrnmpi_wtime() - w2;
	wt_ = w1;
#if BGP_INTERVAL == 2
//printf("%d reverse buffers %g\n", nrnmpi_myid, t);
	if (n_bgp_interval == 2) {
		current_rbuf = next_rbuf;
		next_rbuf = ((next_rbuf + 1)&1);
	}
#endif
	TBUF
}

void bgp_dma_send(PreSyn* ps, double t) {
#if 0
	if (nrn_use_localgid_) {
		nrn_outputevent(ps->localgid_, t);
	}else{
		nrn2ncs_outputevent(ps->output_index_, t);
	}
#endif
	ps->bgp.dma_send_->send(ps->output_index_, t);
}

void bgpdma_send_init(PreSyn* ps) {
}

void bgpdma_cleanup_presyn(PreSyn* ps) {
	if (ps->output_index_ >= 0 && ps->bgp.dma_send_) {
		delete ps->bgp.dma_send_;
		ps->bgp.dma_send_ = 0;
	}
}

void bgp_dma_setup() {
	static int once = 0;
	double wt = nrnmpi_wtime();
	nrnmpi_bgp_comm();

	// although we only care about the set of hosts that gid2out_
	// sends spikes to (source centric). We do not want to send
	// the entire list of gid2in (which may be 10000 times larger
	// than gid2out) from every machine to every machine.
	// so we accomplish the task in two phases the first of which
	// involves allgather with a total receive buffer size of number
	// of cells (even that is too large and we will split it up
	// into chunks). And the second, an
	// allreduce with receive buffer size of number of hosts.

	// gid2in_ gets spikes from which hosts.
	determine_source_hosts();

	// gid2out_ sends spikes to which hosts
	determine_target_hosts();

	if (!bgp_receive_buffer[0]) {
		bgp_receive_buffer[0] = new BGP_ReceiveBuffer();
	}
#if BGP_INTERVAL == 2
	if (n_bgp_interval == 2 && !bgp_receive_buffer[1]) {
		bgp_receive_buffer[1] = new BGP_ReceiveBuffer();
	}
#endif
#if BGPDMA == 2
    if (0 || !once) { once = 1;
	//if (max_ntarget_host = 0) { max_ntarget_host = 1; }
	max_ntarget_host = nrnmpi_numprocs;
	// I'm guessing everyone can use the same hints and so they
	// can be allocated according to the maximum ntarget_hosts_.
	if (hints_) {
		delete [] hints_;
		hints_ = 0;
		delete [] mconfig.connectionlist;
	}
	hints_ = new DCMF_Opcode_t[max_ntarget_host];
	for (int i = 0; i < max_ntarget_host; ++i) {
		hints_[i] = DCMF_PT_TO_PT_SEND;
	}
	// I am also guessing everyone can use the same mconfig.
#if HAVE_DCMF_RECORD_REPLAY
	unsigned max_persist_ids = 0;
	if (use_dcmf_record_replay) {
		PreSyn* ps;
		NrnHashIterate(Gid2PreSyn, gid2out_, PreSyn*, ps) {
			if (ps->output_index_ >= 0 && ps->bgp.dma_send_->ntarget_hosts_ > 0) {
				ps->bgp.dma_send_->persist_id_ = max_persist_ids++;
			}
		}}}
	}
	if (max_persist_ids > 0) { // may want to check for too many as well
		mconfig.protocol = DCMF_MEMFIFO_MCAST_RECORD_REPLAY_PROTOCOL;
		mconfig.max_persist_ids = max_persist_ids;
		mconfig.max_msgs = 10000; //NSEND;
		n_mymulticast_ = max_persist_ids;
	}else{
		mconfig.protocol = DCMF_MEMFIFO_DMA_MSEND_PROTOCOL;
		mconfig.max_persist_ids = 0;
		mconfig.max_msgs = 0;		
		n_mymulticast_ = NSEND;
	}
#else
	mconfig.protocol = DCMF_MEMFIFO_DMA_MSEND_PROTOCOL;
	n_mymulticast_ = NSEND;
#endif
	mci_ = new MyMulticastInfo[n_mymulticast_];
	mconfig.cb_recv = msend_recv;
	mconfig.nconnections = n_mymulticast_; //max_ntarget_host;
	mconfig.connectionlist = new void*[n_mymulticast_];
	mconfig.clientdata = NULL;
	assert(DCMF_Multicast_register (&protocol, &mconfig) == DCMF_SUCCESS);

	for (int i = 0; i < n_mymulticast_; ++i) {
		MyMulticastInfo* mci = mci_+ i;
		mci->record = true;
		mci->req_in_use = false;
		mci->cb_done.clientdata = (void*)&mci->req_in_use;
		mci->cb_done.function = multicast_done;
		mci->msend.registration = &protocol;
		mci->msend.request = &mci->request;
		mci->msend.cb_done = mci->cb_done;
		mci->msend.consistency = DCMF_MATCH_CONSISTENCY; //DCMF_RELAXED_CONSISTENCY; 
		mci->msend.connection_id = i;
		mci->msend.bytes = 0;
		mci->msend.src = NULL;
		mci->msend.opcodes = hints_;
		mci->msend.msginfo = &mci->msginfo;
		mci->msend.count = 1;
		mci->msend.op = DCMF_UNDEFINED_OP;
		mci->msend.dt = DCMF_UNDEFINED_DT;
#if HAVE_DCMF_RECORD_REPLAY
		mci->msend.persist_id = i;
#endif
	}
    }
#endif
}

void determine_source_hosts() {
	int i, nsrcgid, ihost, jhost;
	PreSyn* ps;

	// some target PreSyns may not have any input
	// so initialize all to -1
	NrnHashIterate(Gid2PreSyn, gid2in_, PreSyn*, ps) {
		assert(ps->output_index_ < 0);
		ps->bgp.srchost_ = -1;
	}}}

	// how many ngid src gids on this machine (PreSyn generates the
	// spikes) also create the BGP_DMASend instances and attach to PreSyn
	nsrcgid = 0;
	NrnHashIterate(Gid2PreSyn, gid2out_, PreSyn*, ps) {
		if (ps->output_index_ >= 0) {
			++nsrcgid;
			bgpdma_cleanup_presyn(ps);
			ps->bgp.dma_send_ = new BGP_DMASend();
		}
	}}}
	// store source gids in an array for later transfer.
	int* gids = nsrcgid ? new int[nsrcgid] : 0;
	i = 0;
	NrnHashIterate(Gid2PreSyn, gid2out_, PreSyn*, ps) {
		if (ps->output_index_ >= 0) {
			gids[i] = ps->gid_;
			++i;
		}
	}}}
	
	// how many src gids on each machine
	int* host_nsrcgid = new int[nrnmpi_numprocs];
	nrnmpi_int_allgather(&nsrcgid, host_nsrcgid, 1);
//if (nrnmpi_myid == 0) {
//for (i=0; i < nrnmpi_numprocs; ++i) {
//printf("i=%d host_nsrcgid=%d\n", i, host_nsrcgid[i]);
//}
//}

	// to assess allgatherv requirements, what is the total number
	// of src PreSyn
	long totalngid = 0;
	int maxngid = 0;
	for (i=0; i < nrnmpi_numprocs; ++i) {
		totalngid += host_nsrcgid[i];
		if (maxngid < host_nsrcgid[i]) {
			maxngid = host_nsrcgid[i];
		}
	}
	// get the srcgids from everywhere and fill the src PreSyn host
	// field. Assume there might be more cells than buffer space.
	int bufsize = 10000; // can get at least this
	// since we routinely allocate things of this size it can be at least...
	bufsize = (bufsize < nrnmpi_numprocs) ? nrnmpi_numprocs : bufsize;
	bufsize = (maxngid < bufsize) ? bufsize : maxngid; // guarantee at least enough for any one host
	bufsize = (totalngid < bufsize) ? totalngid : bufsize; // but we certainly do not need more than this
	int* n = new int[nrnmpi_numprocs];
	int* displ = new int[nrnmpi_numprocs+1];
	int* buf = new int[bufsize];
//printf("%d bufsize=%d\n", nrnmpi_myid, bufsize);
	for (ihost = 0; ihost < nrnmpi_numprocs; ) {
		jhost = gathersrcgid(ihost, totalngid, host_nsrcgid, gids,
			n, displ, bufsize, buf);
		for (; ihost < jhost; ++ihost) {
			for (i = displ[ihost]; i < displ[ihost+1]; ++i) {
				int gid = buf[i];
				if (gid2in_ && gid2in_->find(gid, ps)) {
					ps->bgp.srchost_ = ihost;
//printf("%d ihost=%d jhost=%d i=%d gid=%d\n", nrnmpi_myid, ihost, jhost, i, gid);
				}
			}
		}
	}

	delete [] buf;
	delete [] displ;
	delete [] host_nsrcgid;
	if (gids) delete [] gids;

#if 0
	NrnHashIterate(Gid2PreSyn, gid2in_, PreSyn*, ps) {
printf("%d target gid=%d srchost=%d\n", nrnmpi_myid, ps->gid_, ps->bgp.srchost_);
	}}}
#endif

}

int gathersrcgid(int hostbegin, int totalngid, int* ngid, int* thishostgid,
    int* n, int* displ, int bsize, int* buf) {
	int i, hostend;
	for (i=0; i < hostbegin; ++i) {
		n[i] = 0;
		displ[i] = 0;
	}
	displ[i] = 0;
	for (; i < nrnmpi_numprocs; ++i) {
		if ((displ[i] + ngid[i]) > bsize) {
			break;
		}
		n[i] = ngid[i];
		displ[i+1] = displ[i] + n[i];
		hostend = i+1;
	}
	for (; i < nrnmpi_numprocs; ++i) {
		n[i] = 0;
		displ[i+1] = displ[i];
	}
	int* me = nil;
	if (nrnmpi_myid >= hostbegin && nrnmpi_myid < hostend) {
		me = thishostgid;
	}
#if 0
printf("%d hostbegin=%d hostend=%d totalngid=%d bsize=%d\n",
nrnmpi_myid, hostbegin, hostend, totalngid, bsize);
printf("%d thishostgid=%lx me=%lx\n", nrnmpi_myid, thishostgid, me);
for (i=0; i < nrnmpi_numprocs; ++i) {
printf("%d i=%d n=%d displ=%d\n", nrnmpi_myid, i, n[i], displ[i]);
}
#endif
	nrnmpi_int_allgatherv(me, buf, n, displ);
	return hostend;
}

void determine_target_hosts() {
	PreSyn* ps;
	int i;
	// how many target gids
	int ntargid = 0;
	// how many distinct gids this host needs from each host
	int* srchost_count = new int[nrnmpi_numprocs];
	for (i=0; i < nrnmpi_numprocs; ++i) {
		srchost_count[i] = 0;
	}
	NrnHashIterate(Gid2PreSyn, gid2in_, PreSyn*, ps) {
		assert(ps->output_index_ < 0);
		assert(ps->bgp.srchost_ >= -1 && ps->bgp.srchost_ < nrnmpi_numprocs);
		if (ps->bgp.srchost_ >= 0) {
			++ntargid;
			++srchost_count[ps->bgp.srchost_];
		}
	}}}
	int* srchost_displ = new int[nrnmpi_numprocs + 1];
	srchost_displ[0] = 0;
	for (i=0; i < nrnmpi_numprocs; ++i) {
		srchost_displ[i+1] = srchost_displ[i] + srchost_count[i];
	}
#if 0
printf("%d ntargid=%d  last srchost_displ=%d\n", nrnmpi_myid, ntargid, srchost_displ[nrnmpi_numprocs]);
for (i=0; i < nrnmpi_numprocs; ++i) {
	printf("%d i=%d srchost_count=%d srchost_displ=%d\n",
	nrnmpi_myid, i, srchost_count[i], srchost_displ[i]);
}
#endif
	// recount srchost_count while organizing the
	// list of target gids to be organized in ihost order
	int* targid_on_tar = ntargid ? new int[ntargid] : 0;
	for (i=0; i < nrnmpi_numprocs; ++i) {
		srchost_count[i] = 0;
	}
	NrnHashIterate(Gid2PreSyn, gid2in_, PreSyn*, ps) {
		i = ps->bgp.srchost_;
		if (i >= 0) {
			targid_on_tar[srchost_displ[i] + srchost_count[i]] = ps->gid_;
			++srchost_count[i];
		}
	}}}
	
	// now is a good time to use the DMA transfer capabilities.
	int* tarcounts = new int[nrnmpi_numprocs];
	int* tardispl = new int[nrnmpi_numprocs+1];

	determine_targid_count_on_srchost(srchost_count, tarcounts);
	tardispl[0] = 0;
	for (i=0; i < nrnmpi_numprocs; ++i) {
		tardispl[i+1] = tarcounts[i] + tardispl[i];
	}
	int n = tardispl[nrnmpi_numprocs];
	int* targid_on_src = n ? new int[n] : 0;
	// and here is another opportunity for DMA
	determine_targids_on_srchost(targid_on_tar, srchost_count, srchost_displ,
		targid_on_src, tarcounts, tardispl);
	
	// on a src cell basis, what is the size of DMASend.target_hosts.
	if (gid2out_) for (i=0; i < n; ++i) {
		assert(gid2out_->find(targid_on_src[i], ps));
		++ps->bgp.dma_send_->ntarget_hosts_;
	}
	// allocate and set to 0 for recount
	max_ntarget_host = 0;
	NrnHashIterate(Gid2PreSyn, gid2out_, PreSyn*, ps) {
		BGP_DMASend* s = ps->bgp.dma_send_;
		s->target_hosts_ = new int[s->ntarget_hosts_];
		if (max_ntarget_host < s->ntarget_hosts_) {
			max_ntarget_host = s->ntarget_hosts_;
		}
		s->ntarget_hosts_ = 0;
	}}}
	if (gid2out_) for (i=0; i < nrnmpi_numprocs; ++i) {
		for (int j = tardispl[i] ; j < tardispl[i+1]; ++j) {
			assert(gid2out_->find(targid_on_src[j], ps));
			BGP_DMASend* s = ps->bgp.dma_send_;
			s->target_hosts_[s->ntarget_hosts_++] = i;
			if (i == nrnmpi_myid) {
				--s->ntarget_hosts_;
				s->send2self_ = 1;
			}
		}
	}

	if (targid_on_src) delete [] targid_on_src;
	delete [] tardispl;
	delete [] tarcounts;
	if (targid_on_tar) delete [] targid_on_tar;
	delete [] srchost_displ;
	delete [] srchost_count;
#if 0
	NrnHashIterate(Gid2PreSyn, gid2out_, PreSyn*, ps) {
		BGP_DMASend* s = ps->bgp.dma_send_;
		for (i=0; i < s->ntarget_hosts_; ++i) {
printf("%d gid=%d i=%d targethost=%d\n", nrnmpi_myid, ps->gid_, i, s->target_hosts_[i]);
		}
	}}}
#endif
}

void determine_targid_count_on_srchost(int* src, int* tarcounts) {
	int i;
#if 0
for (i=0; i < nrnmpi_numprocs; ++i) {
printf("%d i=%d srchostcnt=%d\n", nrnmpi_myid, i, src[i]);
}
#endif
	for (i=0; i < nrnmpi_numprocs; ++i) {
//printf("%d i=%d src=%d\n", nrnmpi_myid, i, src[i]);
		nrnmpi_int_gather(src+i, tarcounts, 1, i);
#if 0
if (i == nrnmpi_myid) {
for (int j = 0; j < nrnmpi_numprocs; ++j) {
printf("%d gather i=%d j=%d tarcounts=%d\n", nrnmpi_myid, i, j, tarcounts[j]);
}}
#endif
	}
}

void determine_targids_on_srchost(int* s, int* scnt, int* sdispl,
    int* r, int* rcnt, int* rdispl) {
#if 0
	int i;
	for (i=0; i< nrnmpi_numprocs; ++i) {
		nrnmpi_int_gatherv(
			s + sdispl[i], scnt[i],
			r, rcnt, rdispl,
			i
		);
	}
#else
	nrnmpi_int_alltoallv(s, scnt, sdispl, r, rcnt, rdispl);
#endif
}

