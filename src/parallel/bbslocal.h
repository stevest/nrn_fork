#ifndef bbslocal_h
#define bbslocal_h

#include "bbsimpl.h"

class KeepArgs;

class BBSLocal : public BBSImpl {
public:
	BBSLocal();
	virtual ~BBSLocal();

	virtual boolean look(const char*);

	virtual void take(const char*); /* blocks til something to take */
	virtual boolean look_take(const char*); /* returns false if nothing to take */
	// after taking use these
	virtual int upkint();
	virtual double upkdouble();
	virtual void upkvec(int, double*);
	virtual char* upkstr(); // delete [] char* when finished

	// before posting use these
	virtual void pkbegin();
	virtual void pkint(int);
	virtual void pkdouble(double);
	virtual void pkvec(int, double*);
	virtual void pkstr(const char*);
	virtual void post(const char*);

	virtual void post_todo(int parentid);
	virtual void post_result(int id);
	virtual int look_take_result(int pid); // returns id, or 0 if nothing
	virtual int look_take_todo(); // returns id, or 0 if nothing
	virtual int take_todo(); // returns id
	virtual void save_args(int);
	virtual void return_args(int);

	virtual void context();
	
	virtual void start();
	virtual void done();

	virtual void perror(const char*);
private:
	KeepArgs* keepargs_;
};

#endif
