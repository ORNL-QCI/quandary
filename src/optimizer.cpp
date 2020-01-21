#include "optimizer.hpp"

OptimProblem::OptimProblem() {
    primalbraidapp  = NULL;
    adjointbraidapp = NULL;
    objective_curr = 0.0;
}

OptimProblem::OptimProblem(myBraidApp* primalbraidapp_, myAdjointBraidApp* adjointbraidapp_){
    primalbraidapp  = primalbraidapp_;
    adjointbraidapp = adjointbraidapp_;
}

OptimProblem::~OptimProblem() {}



void OptimProblem::setDesign(int n, const double* x) {

  Hamiltonian* hamil = primalbraidapp->hamiltonian;

  /* Pass design vector x to oscillator */
  int nparam;
  double *paramRe, *paramIm;
  int j = 0;
  /* Iterate over oscillators */
  for (int ioscil = 0; ioscil < hamil->getNOscillators(); ioscil++) {
      /* Get number of parameters of oscillator i */
      nparam = hamil->getOscillator(ioscil)->getNParam();
      /* Get pointers to parameters of oscillator i */
      paramRe = hamil->getOscillator(ioscil)->getParamsRe();
      paramIm = hamil->getOscillator(ioscil)->getParamsIm();
      /* Design storage: x = (ReParams, ImParams)_iOscil
      /* Set Re parameters */
      for (int iparam=0; iparam<nparam; iparam++) {
          paramRe[iparam] = x[j]; j++;
      }
      /* Set Im parameters */
      for (int iparam=0; iparam<nparam; iparam++) {
          paramIm[iparam] = x[j]; j++;
      }
  }
}


void OptimProblem::getDesign(int n, double* x){

  double *paramRe, *paramIm;
  int nparam;
  int j = 0;
  /* Iterate over oscillators */
  Hamiltonian* hamil = primalbraidapp->hamiltonian;
  for (int ioscil = 0; ioscil < hamil->getNOscillators(); ioscil++) {
      /* Get number of parameters of oscillator i */
      nparam = hamil->getOscillator(ioscil)->getNParam();
      /* Get pointers to parameters of oscillator i */
      paramRe = hamil->getOscillator(ioscil)->getParamsRe();
      paramIm = hamil->getOscillator(ioscil)->getParamsIm();
      /* Set Re params */
      for (int iparam=0; iparam<nparam; iparam++) {
          x[j] = paramRe[iparam]; j++;
      }
      /* Set Im params */
      for (int iparam=0; iparam<nparam; iparam++) {
          x[j] = paramIm[iparam]; j++;
      }
  }
}

bool OptimProblem::get_prob_sizes(long long& n, long long& m) {

  // n - number of design variables 
  n = 0;
  Hamiltonian* hamil = primalbraidapp->hamiltonian;
  for (int ioscil = 0; ioscil < hamil->getNOscillators(); ioscil++) {
      n += 2 * hamil->getOscillator(ioscil)->getNParam(); // Re and Im params for the i-th oscillator
  }
  
  // m - number of constraints 
  m = 0;          

  return true;
}


bool OptimProblem::get_vars_info(const long long& n, double *xlow, double* xupp, NonlinearityType* type) {

  /* no lower bound or upper bounds. Set bounds very big. */
  double bound = 2e19;

  for (int i=0; i<n; i++) {
    xlow[i] = -bound;
    xupp[i] =  bound;
    type[i] =  hiopNonlinear;
  }

  return true;
}

bool OptimProblem::get_cons_info(const long long& m, double* clow, double* cupp, NonlinearityType* type){
  assert(m==0);
  return true;
}


bool OptimProblem::eval_f(const long long& n, const double* x_in, bool new_x, double& obj_value){
// bool OptimProblem::eval_f(Index n, const Number* x, bool new_x, Number& obj_value){

  double obj_local;
  Hamiltonian* hamil = primalbraidapp->hamiltonian;
  int dim = hamil->getDim();

  /* TODO: if (new_x) ... */

  /* Pass design vector x to oscillator */
  setDesign(n, x_in);

  /* Reset objective function value */
  objective_curr = 0.0;

  /*  Iterate over initial condition */
  // dim = 1;
  for (int iinit = 0; iinit < dim; iinit++) {
    /* Set initial condition for index iinit */
    primalbraidapp->PreProcess(iinit);
    /* Solve forward problem */
    primalbraidapp->Drive();
    /* Eval objective function for initial condition i */
    primalbraidapp->PostProcess(iinit, &obj_local);

    /* Add to global objective value */
    objective_curr += obj_local;
  }

  /* Return objective value */
  obj_value = objective_curr;

  return true;
}


bool OptimProblem::eval_grad_f(const long long& n, const double* x_in, bool new_x, double* gradf){
// bool OptimProblem::eval_grad_f(Index n, const Number* x, bool new_x, Number* grad_f){

  Hamiltonian* hamil = primalbraidapp->hamiltonian;
  double obj_local;
  int dim = hamil->getDim();

  /* Make sure that grad_f is zero when it comes in. */
  for (int i=0; i<n; i++) {
    gradf[i] = 0.0;
  }

  /* Pass x to Oscillator */
  setDesign(n, x_in);

  /* Reset objective function value */
  objective_curr = 0.0;

  /* Iterate over initial conditions */
    // dim = 1;
  for (int iinit = 0; iinit < dim; iinit++) {

    /* --- Solve primal --- */
    primalbraidapp->PreProcess(iinit);
    primalbraidapp->Drive();
    primalbraidapp->PostProcess(iinit, &obj_local);
    /* Add to global objective value */
    objective_curr += obj_local;

    /* --- Solve adjoint --- */
    adjointbraidapp->PreProcess(iinit);
    adjointbraidapp->Drive();
    adjointbraidapp->PostProcess(iinit, NULL);

    /* Add to Ipopt's gradient */
    const double* grad_ptr = adjointbraidapp->getReducedGradientPtr();
    for (int i=0; i<n; i++) {
        gradf[i] += grad_ptr[i]; 
    }
  }
    
  return true;
}


bool OptimProblem::eval_cons(const long long& n, const long long& m, const long long& num_cons, const long long* idx_cons, const double* x_in, bool new_x, double* cons) {
    assert(m==0);
    /* No constraints. Nothing to be done. */
    return true;
}


bool OptimProblem::eval_Jac_cons(const long long& n, const long long& m, const long long& num_cons, const long long* idx_cons, const double* x_in, bool new_x, double** Jac){
    assert(m==0);
    /* No constraints. Nothing to be done. */
    return true;
}

bool OptimProblem::get_starting_point(const long long &global_n, double* x0) {
// bool OptimProblem::get_starting_point(Index n, bool init_x, Number* x, bool init_z, Number* z_L, Number* z_U, Index m, bool init_lambda, Number* lambda){

  /* Set initial parameters */
  // srand (time(NULL));  // TODO: initialize the random seed. 
  srand (1.0);            // seed 1.0 only for code debugging!
  for (int i=0; i<global_n; i++) {
    x0[i] = (double) rand() / ((double)RAND_MAX);
  }

  /* Pass to oscillator */
  setDesign(global_n, x0);
  
  /* Flush control functions */
  // if (mpirank == 0 && iolevel > 0) {
  int ntime = primalbraidapp->ntime;
  double dt = primalbraidapp->total_time / ntime;
  char filename[255];
  Hamiltonian* hamil = primalbraidapp->hamiltonian;
  for (int ioscil = 0; ioscil < hamil->getNOscillators(); ioscil++) {
      sprintf(filename, "control_init_oscil%04d.dat", ioscil);
      hamil->getOscillator(ioscil)->flushControl(ntime, dt, filename);
  }
// }

 
  return true;
}

// void OptimProblem::finalize_solution(SolverReturn status, Index n, const Number* x, const Number* z_L, const Number* z_U, Index m, const Number* g, const Number* lambda, Number obj_value, const IpoptData* ip_data,IpoptCalculatedQuantities* ip_cq){
//   /* This is called after Ipopt finished. */

//   /* Print optimized parameters */
//   FILE *optimfile;
//   optimfile = fopen("optimal_param.dat", "w");
//   for (int i=0; i<n; i++){
//     fprintf(optimfile, "%1.14e\n", x[i]);
//   }
//   fclose(optimfile);
// }


