#ifndef nonvintblock_h
#define nonvintblock_h

#if defined(__cplusplus)
extern "C" {
#endif

/*
Interface for adding blocks of equations setup and solved in python
analogous to the LONGITUDINAL_DIFFUSION at nonvint time for fixed and
variable step methods. Of course, limited to single thread.

The substantive idea is embodied in the last 7 definitions below that look
like functions with distinct prototypes. The obscurity is due to having
a single function pointer in nrniv/nrnoc library space which gets dynamically
filled in when the Python nonvint_block_supervisor module is imported.
When that function is called, the supervisor interprets the remaining arguments
according to the first "method" argument. The tid (thread id) argument
should always be 0. Only one of the methods has a meaningful int return value.
The other uses can merely return 0.
*/

#if defined(nrnoc_fadvance_c)
/* define only in fadvance.c */
#define nonvintblock_extern /**/
#else
/* declare everywhere else */
#define nonvintblock_extern extern
#endif

nonvintblock_extern int (*nrn_nonvint_block)(int method, double* pd1, double* pd2, int tid);

#define nonvint_block(method, pd1, pd2, tid) \
  nrn_nonvint_block ? (*nrn_nonvint_block)(method, pd1, pd2, tid) : 0

/* called near end of nrnoc/treeset.c:v_setup_vectors after structure_change_cnt is incremented. */
#define nrn_nonvint_block_setup() nonvint_block(0, 0, 0, 0)

/* called in nrnoc/fadvance.c:nrn_finitialize before mod file INITIAL blocks */
#define nrn_nonvint_block_init(d, rhs, tid) nonvint_block(1, d, rhs, tid)

/* called at end of nrnoc/treeset.c:setup_tree_matrix */
#define nrn_nonvint_block_fixed_step_vmatrix(tid) nonvint_block(2, 0, 0, tid)
  /*if any ionic membrane currents are generated, they increment the
    NrnThread._actual_rhs and di/dv increments _actual_d */

/* called at end of nrnoc/fadvance.c:nonvint */
#define nrn_nonvint_block_fixed_step_solve(tid) nonvint_block(3, 0, 0, tid)

/* returns the number of extra equations solved by cvode or ida */
/* in Python the Method will be converted to 4. Here we are encoding the offset  */
#define nrn_nonvint_block_ode_count(offset, tid) nonvint_block(10+offset, 0, 0, tid)

/* fill in the double* y with the initial values */
#define nrn_nonvint_block_ode_reinit(y, tid) nonvint_block(5, y, 0, tid)

/* using the values in double* y, fill in double* ydot so that ydot = f(y) */
#define nrn_nonvint_block_ode_fun(y, ydot, tid) nonvint_block(6, y, ydot, tid)

/* Solve (1 + dt*jacobian)*x = b replacing b values with the x values.
   Note that y (state values) are available for constructing the jacobian
   (if the problem is non-linear) */
#define nrn_nonvint_block_ode_solve(b, y, tid) nonvint_block(7, b, y, tid)

#if defined(__cplusplus)
}
#endif

#endif
