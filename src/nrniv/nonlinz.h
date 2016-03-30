#ifndef nonlinz_h
#define nonlinz_h

class NonLinImpRep;

class NonLinImp {
public:
	NonLinImp();
	virtual ~NonLinImp();
	void compute(double omega, double deltafac);
	double transfer_amp(int vloc); // v_node[arg] is the node
	double transfer_phase(int vloc);
	double input_amp(int curloc);
	double input_phase(int curloc);
	double ratio_amp(int clmploc, int vloc);
	void solve(int curloc);
	
private:
	NonLinImpRep* rep_;	
};
	
#endif
