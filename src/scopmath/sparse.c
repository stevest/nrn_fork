#include <../../nrnconf.h>

/******************************************************************************
 *
 * File: sparse.c
 *
 * Copyright (c) 1989, 1990
 *   Duke University
 *
 ******************************************************************************/

#ifndef LINT
static char RCSid[] = "sparse.c,v 1.7 1998/03/12 13:17:17 hines Exp";
#endif

#include <stdlib.h>
#include "errcodes.h"

/* 4/23/93 converted to object so many models can use it */

/*-----------------------------------------------------------------------------
 *
 *  sparse()
 *
 *  Abstract: 
 *  This is an experimental numerical method for SCoP-3 which integrates kinetic
 *  rate equations.  It is intended to be used only by models generated by MODL,
 *  and its identity is meant to be concealed from the user.
 *
 *
 *  Calling sequence:
 *	sparse(n, s, d, t, dt, fun, prhs, linflag)
 *
 *  Arguments:
 * 	n		number of state variables
 * 	s		array of pointers to the state variables
 * 	d		array of pointers to the derivatives of states
 * 	t		pointer to the independent variable
 * 	dt		the time step
 * 	fun		pointer to the function corresponding to the
 *			kinetic block equations
 * 	prhs		pointer to right hand side vector (answer on return)
 *			does not have to be allocated by caller.
 * 	linflag		solve as linear equations
 *			when nonlinear, all states are forced >= 0
 * 
 *		
 *  Returns:	nothing
 *
 *  Functions called: IGNORE(), printf(), create_coef_list(), fabs()
 *
 *  Files accessed:  none
 *
*/

#if LINT
#define IGNORE(arg)	{if (arg);}
#else
#define IGNORE(arg)	arg
#endif

#if __TURBOC__ || VMS
#define Free(arg)	free((void *)arg)
#else
#define Free(arg)	free((char *)arg)
#endif

# define	rowst	spar_rowst
# define	diag	spar_diag
# define	neqn	spar_neqn
# define	varord	spar_varord
# define	matsol	spar_matsol
# define	getelm	spar_getelm
# define	bksub	spar_bksub
# define	prmat	spar_prmat
# define	subrow	spar_subrow
# define	remelm	spar_remelm

#include <stdio.h>
#include <math.h>
#include <assert.h>

typedef struct Elm {
	unsigned row;		/* Row location */
	unsigned col;		/* Column location */
	double value;		/* The value */
	struct Elm *r_up;	/* Link to element in same column */
	struct Elm *r_down;	/* 	in solution order */
	struct Elm *c_left;	/* Link to left element in same row */
	struct Elm *c_right;	/*	in solution order (see getelm) */
} Elm;
#define ELM0	(Elm *)0

typedef struct Item {
	Elm 		*elm;
	unsigned	norder; /* order of a row */
	struct Item	*next;
	struct Item	*prev;
} Item;
#define ITEM0	(Item *)0

typedef Item List;	/* list of mixed items */

typedef struct SparseObj { /* all the state information */
	Elm**	rowst;
	Elm**	diag;
	unsigned neqn;
	unsigned* varord;
	int (*oldfun)();
	unsigned ngetcall;
	int phase;
	double** coef_list;
	/* don't really need the rest */
	int nroworder;
	Item** roworder;
	List* orderlist;
	int do_flag;
} SparseObj;

static SparseObj* old_sparseobj;
static SparseObj* create_sparseobj();
static sparseobj2local();
static local2sparseobj();

static Elm **rowst;		/* link to first element in row (solution order)*/
static Elm **diag;		/* link to pivot element in row (solution order)*/
static unsigned neqn;			/* number of equations */
static unsigned *varord;		/* row and column order for pivots */
static double *rhs;		/* initially- right hand side	finally - answer */
extern int numop;

static unsigned ngetcall; /* counter for number of calls to _getelm */
static int phase; /* 0-solution phase; 1-count phase; 2-build list phase */
static double **coef_list; /* pointer to value in _getelm order */

static int nroworder; /* just for freeing */
static Item **roworder; /* roworder[i] is pointer to order item for row i.
			Does not have to be in orderlist */
static List *orderlist; /* list of rows sorted by norder
			that haven't been used */

static int do_flag;

/* note: solution order refers to the following
	diag[varord[row]]->row = row = diag[varord[row]]->col
	rowst[varord[row]]->row = row
	varord[el->row] < varord[el->c_right->row]
	varord[el->col] < varord[el->r_down->col]
*/
	
extern void *emalloc();
static int matsol();
static Elm *getelm();

static subrow();
static bksub();
static int free_elm();
static create_coef_list();
static init_coef_list();
static increase_order();
static reduce_order();
static spar_minorder();
static get_next_pivot();
static freelist();
static check_assert();
static re_link();
static delete();


/* sparse matrix dynamic allocation:
create_coef_list makes a list for fast setup, does minimum ordering and
ensures all elements needed are present */
/* this could easily be made recursive but it isn't right now */

sparse(v, n, s, d, p, t, dt, fun, prhs, linflag)
	void** v;
	int n, linflag;  /* linflag was not explicitly declared */
	int (*fun)();
	double *t, dt, **prhs, *p;
	int *s, *d;
#define s_(arg) p[s[arg]]
#define d_(arg) p[d[arg]]
{
	int i, j, ierr;
	double err;
	SparseObj* so;
	
#if LINT	/* unused args */
	if (t);
	if (pcoef);
#endif
	if (!*prhs) {
		*prhs = (double *)emalloc((n + 1)*sizeof(double));
	}
	rhs = *prhs;
	so = (SparseObj*)(*v);
	if (!so) {
		so = create_sparseobj();
		*v = (void*)so;
	}
	if (so != old_sparseobj) {
		sparseobj2local(so);
	}
	if (so->oldfun != fun) {
		so->oldfun = fun;
		create_coef_list(n, fun); /* calls fun twice */
		local2sparseobj(so);
	}
	for (i=0; i<n; i++) { /*save old state*/
		d_(i) = s_(i);
	}
	for (err=1, j=0; err > CONVERGE; j++) {
		init_coef_list();
		(*fun)();
		if((ierr = spar_matsol())) {
			return ierr;
		}
		for (err=0.,i=1; i<=n; i++) {/* why oh why did I write it from 1 */
			s_(i-1) += rhs[i];
#if 1 /* stability of nonlinear kinetic schemes sometimes requires this */
if (!linflag && s_(i-1) < 0.) { s_(i-1) = 0.; }
#endif
			err += fabs(rhs[i]);
		}
		if (j > MAXSTEPS) {
			return EXCEED_ITERS;
		}
		if (linflag) break;
	}
	init_coef_list();
	(*fun)();
	for (i=0; i<n; i++) { /*restore Dstate at t+dt*/
		d_(i) = (s_(i) - d_(i))/dt;
	}
	return SUCCESS;
}

/* for solving ax=b */
_cvode_sparse(v, n, x, p, fun, prhs)
	void** v;
	int n;
	int (*fun)();
	double **prhs, *p;
	int *x;
#define x_(arg) p[x[arg]]
{
	int i, j, ierr;
	SparseObj* so;
	
	if (!*prhs) {
		*prhs = (double *)emalloc((n + 1)*sizeof(double));
	}
	rhs = *prhs;
	so = (SparseObj*)(*v);
	if (!so) {
		so = create_sparseobj();
		*v = (void*)so;
	}
	if (so != old_sparseobj) {
		sparseobj2local(so);
	}
	if (so->oldfun != fun) {
		so->oldfun = fun;
		create_coef_list(n, fun); /* calls fun twice */
		local2sparseobj(so);
	}
		init_coef_list();
		(*fun)();
		if((ierr = spar_matsol())) {
			return ierr;
		}
		for (i=1; i<=n; i++) {/* why oh why did I write it from 1 */
			x_(i-1) = rhs[i];
		}
	return SUCCESS;
}

int numop;

static int
matsol()
{
	register Elm *pivot, *el;
	unsigned i;

	/* Upper triangularization */
	numop = 0;
	for (i=1 ; i <= neqn ; i++)
	{
		if (fabs((pivot = diag[i])->value) <= ROUNDOFF)
		{
			return SINGULAR;
		}
		/* Eliminate all elements in pivot column */
		for (el = pivot->r_down ; el ; el = el->r_down)
		{
			subrow(pivot, el);
		}
	}
	bksub();
	return(SUCCESS);
}


static subrow(pivot, rowsub)
 Elm *pivot, *rowsub;
{
	double r;
	register Elm *el;

	r = rowsub->value / pivot->value;
	rhs[rowsub->row] -= rhs[pivot->row] * r;
	numop++;
	for (el = pivot->c_right ; el ; el = el->c_right) {
		for (rowsub = rowsub->c_right; rowsub->col != el->col;
		  rowsub = rowsub->c_right) {
		  	;
		}
		rowsub->value -= el->value * r;
		numop++;
	}
}

static bksub()
{
	unsigned i;
	Elm *el;

	for (i = neqn ; i >= 1 ; i--)
	{
		for (el = diag[i]->c_right ; el ; el = el->c_right)
		{
			rhs[el->row] -= el->value * rhs[el->col];
			numop++;
		}
		rhs[diag[i]->row] /= diag[i]->value;
		numop++;
	}
}


static prmat()
{
	unsigned i, j;
	Elm *el;

	IGNORE(printf("\n        "));
	for (i=10 ; i <= neqn ; i += 10)
		IGNORE(printf("         %1d", (i%100)/10));
	IGNORE(printf("\n        "));
	for (i=1 ; i <= neqn; i++)
		IGNORE(printf("%1d", i%10));
	IGNORE(printf("\n\n"));
	for (i=1 ; i <= neqn ; i++)
	{
		IGNORE(printf("%3d %3d ", diag[i]->row, i));
		j = 0;
		for (el = rowst[i] ;el ; el = el->c_right)
		{
			for ( j++ ; j < varord[el->col] ; j++)
				IGNORE(printf(" "));
			IGNORE(printf("*"));
		}
		IGNORE(printf("\n"));
	}
	IGNORE(fflush(stdin));
}

static initeqn(maxeqn)	/* reallocate space for matrix */
	unsigned maxeqn;
{
	register unsigned i;

	if (maxeqn == neqn) return;
	free_elm();
	if (rowst)
		Free(rowst);
	if (diag)
		Free(diag);
	if (varord)
		Free(varord);
	rowst = diag = (Elm **)0;
	varord = (unsigned *)0;
	rowst = (Elm **)emalloc((maxeqn + 1)*sizeof(Elm *));
	diag = (Elm **)emalloc((maxeqn + 1)*sizeof(Elm *));
	varord = (unsigned *)emalloc((maxeqn + 1)*sizeof(unsigned));
	for (i=1 ; i<= maxeqn ; i++)
	{
		varord[i] = i;
		diag[i] = (Elm *)emalloc(sizeof(Elm));
		rowst[i] = diag[i];
		diag[i]->row = i;
		diag[i]->col = i;
		diag[i]->r_down = diag[i]->r_up = ELM0;
		diag[i]->c_right = diag[i]->c_left = ELM0;
		diag[i]->value = 0.;
		rhs[i] = 0.;
	}
	neqn = maxeqn;
}

static int
free_elm() {
	unsigned i;
	Elm *el;
	
	/* free all elements */
	for (i=1; i <= neqn; i++)
	{
		for (el = rowst[i]; el; el=el->c_right)
			Free(el);
		rowst[i] = ELM0;
		diag[i] = ELM0;
	}
}


/* see check_assert in minorder for info about how this matrix is supposed
to look.  In new is nonzero and an element would otherwise be created, new
is used instead. This is because linking an element is highly nontrivial
The biggest difference is that elements are no longer removed and this
saves much time allocating and freeing during the solve phase
*/

static Elm *
getelm(row, col, new)
   register Elm *new;
   unsigned row, col;
   /* return pointer to row col element maintaining order in rows */
{
	register Elm *el, *elnext;
	unsigned vrow, vcol;
	
	vrow = varord[row];
	vcol = varord[col];
	
	if (vrow == vcol) {
		return diag[vrow]; /* a common case */
	}
	if (vrow > vcol) { /* in the lower triangle */
		/* search downward from diag[vcol] */
		for (el=diag[vcol]; ; el = elnext) {
			elnext = el->r_down;
			if (!elnext) {
				break;
			}else if (elnext->row == row) { /* found it */
				return elnext;
			}else if (varord[elnext->row] > vrow) {
				break;
			}
		}
		/* insert below el */
		if (!new) {
			new = (Elm *)emalloc(sizeof(Elm));
			new->value = 0.;
			increase_order(row);
		}
		new->r_down = el->r_down;
		el->r_down = new;
		new->r_up = el;
		if (new->r_down) {
			new->r_down->r_up = new;
		}
		/* search leftward from diag[vrow] */
		for (el=diag[vrow]; ; el = elnext) {
			elnext = el->c_left;
			if (!elnext) {
				break;
			} else if (varord[elnext->col] < vcol) {
				break;
			}
		}
		/* insert to left of el */
		new->c_left = el->c_left;
		el->c_left = new;
		new->c_right = el;
		if (new->c_left) {
			new->c_left->c_right = new;
		}else{
			rowst[vrow] = new;
		}
	} else { /* in the upper triangle */
		/* search upward from diag[vcol] */
		for (el=diag[vcol]; ; el = elnext) {
			elnext = el->r_up;
			if (!elnext) {
				break;
			}else if (elnext->row == row) { /* found it */
				return elnext;
			}else if (varord[elnext->row] < vrow) {
				break;
			}
		}
		/* insert above el */
		if (!new) {
			new = (Elm *)emalloc(sizeof(Elm));
			new->value = 0.;
			increase_order(row);
		}
		new->r_up = el->r_up;
		el->r_up = new;
		new->r_down = el;
		if (new->r_up) {
			new->r_up->r_down = new;
		}
		/* search right from diag[vrow] */
		for (el=diag[vrow]; ; el = elnext) {
			elnext = el->c_right;
			if (!elnext) {
				break;
			}else if (varord[elnext->col] > vcol) {
				break;
			}
		}
		/* insert to right of el */
		new->c_right = el->c_right;
		el->c_right = new;
		new->c_left = el;
		if (new->c_right) {
			new->c_right->c_left = new;
		}
	}
	new->row = row;
	new->col = col;
	return new;
}

double *
_getelm(row, col) int row, col; {
	Elm *el;
	if (!phase) {
		return 	coef_list[ngetcall++];
	}
	el = getelm((unsigned)row, (unsigned)col, ELM0);
	if (phase ==1) {
		ngetcall++;
	}else{
		coef_list[ngetcall++] = &el->value;
	}
	return &el->value;
}

static create_coef_list(n, fun)
	int n;
	int (*fun)();
{
	initeqn((unsigned)n);
	phase = 1;
	ngetcall = 0;
	(*fun)();
	if (coef_list) {
		free(coef_list);
	}
	coef_list = (double **)emalloc(ngetcall * sizeof(double *));
	spar_minorder();
	phase = 2;
	ngetcall = 0;
	(*fun)();
	phase = 0;
}

static init_coef_list() {
	unsigned i;
	Elm *el;
	
	ngetcall = 0;
	for (i=1; i<=neqn; i++) {
		for (el = rowst[i]; el; el = el->c_right) {
			el->value = 0.;
		}
	}
}


static Item *newitem();
static List *newlist();

static insert();

static init_minorder() {
	/* matrix has been set up. Construct the orderlist and orderfind
	   vector.
	*/
	unsigned i, j;
	Elm *el;
	
	do_flag = 1;
	if (roworder) {
		for (i=1; i <= nroworder; ++i) {
			Free(roworder[i]);
		}
		Free(roworder);
	}
	roworder = (Item **)emalloc((neqn+1)*sizeof(Item *));
	nroworder = neqn;
	if (orderlist) freelist(orderlist);
	orderlist = newlist();
	for (i=1; i<=neqn; i++) {
		roworder[i] = newitem();
	}
	for (i=1; i<=neqn; i++) {
		for (j=0, el = rowst[i]; el; el = el->c_right) {
			j++;
		}
		roworder[diag[i]->row]->elm = diag[i];
		roworder[diag[i]->row]->norder = j;
		insert(roworder[diag[i]->row]);
	}
}

static increase_order(row) unsigned row; {
	/* order of row increases by 1. Maintain the orderlist. */
	Item *order;

	if(!do_flag) return;
	order = roworder[row];
	delete(order);
	order->norder++;
	insert(order);
}

static reduce_order(row) unsigned row; {
	/* order of row decreases by 1. Maintain the orderlist. */
	Item *order;

	if(!do_flag) return;
	order = roworder[row];
	delete(order);
	order->norder--;
	insert(order);
}

static spar_minorder() { /* Minimum ordering algorithm to determine the order
			that the matrix should be solved. Also make sure
			all needed elements are present.
			This does not mess up the matrix
		*/
	unsigned i;

	check_assert();
	init_minorder();
	for (i=1; i<=neqn; i++) {
		get_next_pivot(i);
	}
	do_flag = 0;
	check_assert();
}

static get_next_pivot(i) unsigned i; {
	/* get varord[i], etc. from the head of the orderlist. */
	Item *order;
	Elm *pivot, *el;
	unsigned j;

	order = orderlist->next;
	assert(order != orderlist);
	
	if ((j=varord[order->elm->row]) != i) {
		/* push order lists down by 1 and put new diag in empty slot */
		assert(j > i);
		el = rowst[j];
		for (; j > i; j--) {
			diag[j] = diag[j-1];
			rowst[j] = rowst[j-1];
			varord[diag[j]->row] = j;
		}
		diag[i] = order->elm;
		rowst[i] = el;
		varord[diag[i]->row] = i;
		/* at this point row links are out of order for diag[i]->col
		   and col links are out of order for diag[i]->row */
		re_link(i);
	}


	/* now make sure all needed elements exist */
	for (el = diag[i]->r_down; el; el = el->r_down) {
		for (pivot = diag[i]->c_right; pivot; pivot = pivot->c_right) {
			IGNORE(getelm(el->row, pivot->col, ELM0));
		}
		reduce_order(el->row);
	}

#if 0
{int j; Item *or;
	printf("%d  ", i);
	for (or = orderlist->next, j=0; j<5 && or != orderlist; j++, or=or->next) {
		printf("(%d, %d)  ", or->elm->row, or->norder);
	}
	printf("\n");
}
#endif
	delete(order);
}

/* The following routines support the concept of a list.
modified from modl
*/

/* Implementation
  The list is a doubly linked list. A special item with element 0 is
  always at the tail of the list and is denoted as the List pointer itself.
  list->next point to the first item in the list and
  list->prev points to the last item in the list.
	i.e. the list is circular
  Note that in an empty list next and prev points to itself.

It is intended that this implementation be hidden from the user via the
following function calls.
*/

static Item *
newitem()
{
	Item *i;
	i = (Item *)emalloc(sizeof(Item));
	i->prev = ITEM0;
	i->next = ITEM0;
	i->norder = 0;
	i->elm = (Elm *)0;
	return i;
}

static List *
newlist()
{
	Item *i;
	i = newitem();
	i->prev = i;
	i->next = i;
	return (List *)i;
}

static freelist(list)	/*free the list but not the elements*/
	List *list;
{
	Item *i1, *i2;
	for (i1 = list->next; i1 != list; i1 = i2) {
		i2 = i1->next;
		Free(i1);
	}
	Free(list);
}

static linkitem(item, i)	/*link i before item*/
	Item *item;
	Item *i;
{
	i->prev = item->prev;
	i->next = item;
	item->prev = i;
	i->prev->next = i;
}


static insert(item)
	Item *item;
{
	Item *i;

	for (i = orderlist->next; i != orderlist; i = i->next) {
		if (i->norder >= item->norder) {
			break;
		}
	}
	linkitem(i, item);
}

static delete(item)
	Item *item;
{
		
	item->next->prev = item->prev;
	item->prev->next = item->next;
	item->prev = ITEM0;
	item->next = ITEM0;
}

void *emalloc(n) unsigned n; { /* check return from malloc */
#if __TURBOC__ || VMS
	void *malloc();
	void *p;
	p = (void *)malloc(n);
	if (p == (void *)0) {
		abort_run(LOWMEM);
	}
	return p;
#else
	char *p;
	p = (char *)malloc(n);
	if (p == (char *)0) {
		abort_run(LOWMEM);
	}
	return (void *)p;
#endif
}

static
check_assert() {
	/* check that all links are consistent */
	unsigned i;
	Elm *el;
	
	for (i=1; i<=neqn; i++) {
		assert(diag[i]);
		assert(diag[i]->row == diag[i]->col);
		assert(varord[diag[i]->row] == i);
		assert(rowst[i]->row == diag[i]->row);
		for (el = rowst[i]; el; el = el->c_right) {
			if (el == rowst[i]) {
				assert(el->c_left == ELM0);
			}else{
			   assert(el->c_left->c_right == el);
			   assert(varord[el->c_left->col] < varord[el->col]);
			}
		}
		for (el = diag[i]->r_down; el; el = el->r_down) {
			assert(el->r_up->r_down == el);
			assert(varord[el->r_up->row] < varord[el->row]);
		}
		for (el = diag[i]->r_up; el; el = el->r_up) {
			assert(el->r_down->r_up == el);
			assert(varord[el->r_down->row] > varord[el->row]);
		}
	}
}

	/* at this point row links are out of order for diag[i]->col
	   and col links are out of order for diag[i]->row */
static re_link(i) unsigned i; {

	Elm *el, *dright, *dleft, *dup, *ddown, *elnext;
	
	for (el=rowst[i]; el; el = el->c_right) {
		/* repair hole */
		if (el->r_up) el->r_up->r_down = el->r_down;
		if (el->r_down) el->r_down->r_up = el->r_up;
	}

	for (el=diag[i]->r_down; el; el = el->r_down) {
		/* repair hole */
		if (el->c_right) el->c_right->c_left = el->c_left;
		if (el->c_left) el->c_left->c_right = el->c_right;
		else rowst[varord[el->row]] = el->c_right;
	}

	for (el=diag[i]->r_up; el; el = el->r_up) {
		/* repair hole */
		if (el->c_right) el->c_right->c_left = el->c_left;
		if (el->c_left) el->c_left->c_right = el->c_right;
		else rowst[varord[el->row]] = el->c_right;
	}

   /* matrix is consistent except that diagonal row elements are unlinked from
   their columns and the diagonal column elements are unlinked from their
   rows.
   For simplicity discard all knowledge of links and use getelm to relink
   */
	rowst[i] = diag[i];
	dright = diag[i]->c_right;
	dleft = diag[i]->c_left;
	dup = diag[i]->r_up;
	ddown = diag[i]->r_down;
	diag[i]->c_right = diag[i]->c_left = ELM0;
	diag[i]->r_up = diag[i]->r_down = ELM0;
	for (el=dright; el; el = elnext) {
		elnext = el->c_right;
		IGNORE(getelm(el->row, el->col, el));
	}
	for (el=dleft; el; el = elnext) {
		elnext = el->c_left;
		IGNORE(getelm(el->row, el->col, el));
	}
	for (el=dup; el; el = elnext) {
		elnext = el->r_up;
		IGNORE(getelm(el->row, el->col, el));
	}
	for (el=ddown; el; el = elnext){
		elnext = el->r_down;
		IGNORE(getelm(el->row, el->col, el));
	}
}

static SparseObj* create_sparseobj() {

	SparseObj* so;

	so = emalloc(sizeof(SparseObj));
	so->rowst = 0;
	so->diag = 0;
	so->neqn = 0;
	so->varord = 0;
	so->oldfun = 0;
	so->ngetcall = 0;
	so->phase = 0;
	so->coef_list = 0;
	so->roworder = 0;
	so->nroworder = 0;
	so->orderlist = 0;
	so->do_flag = 0;

	return so;
}

static sparseobj2local(so)
	SparseObj* so;
{
	rowst = so->rowst;
	diag = so->diag;
	neqn = so->neqn;
	varord = so->varord;
	ngetcall = so->ngetcall;
	phase = so->phase;
	coef_list = so->coef_list;
	roworder = so->roworder;
	nroworder = so->nroworder;
	orderlist = so->orderlist;
	do_flag = so->do_flag;
}

static local2sparseobj(so)
	SparseObj* so;
{
	so->rowst = rowst;
	so->diag = diag;
	so->neqn = neqn;
	so->varord = varord;
	so->ngetcall = ngetcall;
	so->phase = phase;
	so->coef_list = coef_list;
	so->roworder = roworder;
	so->nroworder = nroworder;
	so->orderlist = orderlist;
	so->do_flag = do_flag;
}

