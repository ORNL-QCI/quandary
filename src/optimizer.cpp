#include "optimizer.hpp"

OptimProblem::OptimProblem() {
    primalbraidapp  = NULL;
    adjointbraidapp = NULL;
    objective = 0.0;
    fidelity = 0.0;
    trace_Re = 0.0;
    trace_Im = 0.0;
    regul = 0.0;
    x0filename = "none";
    mpirank_braid = 0;
    mpisize_braid = 0;
    mpirank_space = 0;
    mpisize_space = 0;
    mpirank_optim = 0;
    mpisize_optim = 0;
    mpirank_world = 0;
    mpisize_world = 0;
    printlevel = 0;
}

OptimProblem::OptimProblem(myBraidApp* primalbraidapp_, myAdjointBraidApp* adjointbraidapp_, Gate* targate_, MPI_Comm comm_hiop_, const std::vector<double> optim_bounds_, double optim_regul_, std::string x0filename_, std::string datadir_, int optim_printlevel_){
    primalbraidapp  = primalbraidapp_;
    adjointbraidapp = adjointbraidapp_;
    targetgate = targate_;
    comm_hiop = comm_hiop_;
    regul = optim_regul_;
    x0filename = x0filename_;
    bounds = optim_bounds_;
    datadir = datadir_;
    printlevel = optim_printlevel_;

    MPI_Comm_rank(primalbraidapp->comm_braid, &mpirank_braid);
    MPI_Comm_size(primalbraidapp->comm_braid, &mpisize_braid);
    MPI_Comm_rank(PETSC_COMM_WORLD, &mpirank_space);
    MPI_Comm_size(PETSC_COMM_WORLD, &mpisize_space);
    MPI_Comm_rank(comm_hiop, &mpirank_optim);
    MPI_Comm_size(comm_hiop, &mpisize_optim);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);
    MPI_Comm_size(MPI_COMM_WORLD, &mpisize_world);

    /* Open optim file */
    if (mpirank_world == 0 && printlevel > 0) {
      char filename[255];
      sprintf(filename, "%s/optim.dat", datadir.c_str());
      optimfile = fopen(filename, "w");
      fprintf(optimfile, "#iter    obj_value           fidelity              ||grad||              inf_du               ls trials \n");
    }
}

OptimProblem::~OptimProblem() {
  /* Close optim file */
  if (mpirank_world == 0 && printlevel > 0) fclose(optimfile);
}



void OptimProblem::setDesign(int n, const double* x) {

  MasterEq* mastereq = primalbraidapp->mastereq;

  /* Pass design vector x to oscillator */
  int nparam;
  double *paramRe, *paramIm;
  int j = 0;
  /* Iterate over oscillators */
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
      /* Get number of parameters of oscillator i */
      nparam = mastereq->getOscillator(ioscil)->getNParam();
      /* Get pointers to parameters of oscillator i */
      paramRe = mastereq->getOscillator(ioscil)->getParamsRe();
      paramIm = mastereq->getOscillator(ioscil)->getParamsIm();
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
  MasterEq* mastereq = primalbraidapp->mastereq;
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
      /* Get number of parameters of oscillator i */
      nparam = mastereq->getOscillator(ioscil)->getNParam();
      /* Get pointers to parameters of oscillator i */
      paramRe = mastereq->getOscillator(ioscil)->getParamsRe();
      paramIm = mastereq->getOscillator(ioscil)->getParamsIm();
      /* Design storage: x = (ReParams, ImParams)_iOscil
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
  MasterEq* mastereq = primalbraidapp->mastereq;
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
      n += 2 * mastereq->getOscillator(ioscil)->getNParam(); // Re and Im params for the i-th oscillator
  }
  
  // m - number of constraints 
  m = 0;          

  return true;
}


bool OptimProblem::get_vars_info(const long long& n, double *xlow, double* xupp, NonlinearityType* type) {

  /* Iterate over oscillators */
  int j = 0;
  MasterEq* mastereq = primalbraidapp->mastereq;
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
      /* Get number of parameters of oscillator i */
      int nparam = mastereq->getOscillator(ioscil)->getNParam();
      /* Iterate over real and imaginary part */
      for (int i = 0; i < 2 * nparam; i++) {
          xlow[j] = - bounds[ioscil];
          xupp[j] =   bounds[ioscil]; 
          j++;
      }
  }

  for (int i=0; i<n; i++) {
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

  if (mpirank_world == 0) printf(" EVAL F... ");
  MasterEq* mastereq = primalbraidapp->mastereq;
  int dim = mastereq->getDim();
  double Re_local = 0.0;
  double Im_local = 0.0;
  double obj_local = 0.0;
  Vec finalstate = NULL;
  Vec initstate = NULL;

  /* Run simulation, only if x_in is new. Otherwise, f(x_in) has been computed already and stored in fidelity. */
  // this is fishy. check if fidelity is computed correctly in grad_f
  // if (new_x) { 

    /* Pass design vector x to oscillator */
    setDesign(n, x_in);

    /*  Iterate over initial condition */
    trace_Re = 0.0;
    trace_Im = 0.0;
    objective = 0.0;
    for (int iinit = 0; iinit < dim; iinit++) {
      

      if (mpirank_world == 0) printf(" %d FWD. ", iinit);
      /* Run forward with initial condition iinit */
      initstate = primalbraidapp->PreProcess(iinit);
      if (initstate != NULL) 
        mastereq->initialCondition(iinit, initstate);
      primalbraidapp->Drive();
      finalstate = primalbraidapp->PostProcess(); // this return NULL for all but the last time processor

      /* Add to objective function */
      if (finalstate != NULL) {
        targetgate->compare(iinit, finalstate, obj_local);
        objective += obj_local;
      }

      /* Add diagonal elements to fidelity trace tr(X^dat G) */
      if (iinit % ((int)sqrt(dim)+1) == 0 ) // this hits the diagonal elements
      {
        if (finalstate != NULL) {
          targetgate->fidelity(iinit, finalstate, Re_local, Im_local);
          // targetgate->apply(iinit, finalstate, Re_local, Im_local);
          trace_Re += Re_local;
          trace_Im += Im_local;
        }
      }
    }
  // }

  /* Broadcast fidelity trace and objective from last to all processors */
  MPI_Bcast(&trace_Re, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);
  MPI_Bcast(&trace_Im, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);
  MPI_Bcast(&objective, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);

  /* Compute fidelity 1/N^2 |trace|^2 */
  fidelity = 1. / dim * (pow(trace_Re, 2.0) + pow(trace_Im, 2.0));

  /* Compute objective 1/(2*N^2) ||W-G||_F^2 */
  objective = 1./(2.*dim) * objective;

  /* Add regularization objective = objective + gamma * ||x||^2*/
  for (int i=0; i<n; i++) {
    objective += regul / 2.0 * pow(x_in[i], 2.0);
  }

  if (mpirank_world == 0) printf("  -->  infidelity: %1.14e, objective: %1.14e\n", 1.0 - fidelity, objective);

  /* Return objective value */
  obj_value = objective;

  return true;
}


bool OptimProblem::eval_grad_f(const long long& n, const double* x_in, bool new_x, double* gradf){
  if (mpirank_world == 0) printf(" EVAL GRAD F...");

  MasterEq* mastereq = primalbraidapp->mastereq;
  double obj_Re_local, obj_Im_local;
  int dim = mastereq->getDim();
  double Re_local = 0.0;
  double Im_local = 0.0;
  double obj_local = 0.0;
  Vec initstate = NULL;
  Vec finalstate = NULL;
  Vec initadjoint = NULL;

  /* Pass x to Oscillator */
  setDesign(n, x_in);

  /* Derivative of regularization gamma * ||x||^2 */
  for (int i=0; i<n; i++) {
    gradf[i] = regul * x_in[i];
  }

  /* Derivative objective 1/(2N^2) J */
  double obj_bar = 1./(2.*dim);

  /* Iterate over initial conditions */
  trace_Re = 0.0;
  trace_Im = 0.0;
  objective = 0.0;
  for (int iinit = 0; iinit < dim; iinit++) {

    /* --- Solve primal --- */
    if (mpirank_world == 0) printf(" %d FWD -", iinit);
    initstate = primalbraidapp->PreProcess(iinit); // returns NULL if not stored on this proc
    if (initstate != NULL)
      mastereq->initialCondition(iinit, initstate);
    primalbraidapp->Drive();
    finalstate = primalbraidapp->PostProcess(); // returns NULL if not stored on this proc

    /* Add to objective function */
    if (finalstate != NULL) {
      targetgate->compare(iinit, finalstate, obj_local);
      objective += obj_local;
    }

    /* Add diagonal elements to fidelity trace tr(X^dat G) */
    if (iinit % ((int)sqrt(dim)+1) == 0 ) // this hits the diagonal elements
    {
      if (finalstate != NULL) {
        targetgate->fidelity(iinit, finalstate, Re_local, Im_local);
        // targetgate->apply(iinit, finalstate, Re_local, Im_local);
        trace_Re += Re_local;
        trace_Im += Im_local;
      }
    }

    /* --- Solve adjoint --- */
    if (mpirank_world == 0) printf(" BWD. ");
    initadjoint = adjointbraidapp->PreProcess(iinit); // return NULL if not stored on this proc
    if (initadjoint != NULL) 
       targetgate->compare_diff(iinit, finalstate, initadjoint, obj_bar);
    adjointbraidapp->Drive();
    adjointbraidapp->PostProcess();

    /* Add to Ipopt's gradient */
    const double* grad_ptr = adjointbraidapp->getReducedGradientPtr();
    for (int i=0; i<n; i++) {
        gradf[i] += grad_ptr[i]; 
    }
  }
  
  /* Broadcast trace and objective from last to all processors */
  MPI_Bcast(&trace_Re, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);
  MPI_Bcast(&trace_Im, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);
  MPI_Bcast(&objective, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);

  /* Compute fidelity 1/N^2 |trace|^2 */
  fidelity = 1. / dim * (pow(trace_Re, 2.0) + pow(trace_Im, 2.0));

  /* Compute objective 1/(2*N^2) ||W-G||_F^2 */
  objective = 1./(2.*dim) * objective;

  /* Add regularization: objective = fidelity + gamma*||x||^2 */
  for (int i=0; i<n; i++) {
    objective += regul / 2.0 * pow(x_in[i], 2.0);
  }

  /* Sum up the gradient from all braid processors */
  double* mygrad = new double[n];
  for (int i=0; i<n; i++) {
    mygrad[i] = gradf[i];
  }
  MPI_Allreduce(mygrad, gradf, n, MPI_DOUBLE, MPI_SUM, primalbraidapp->comm_braid);

  /* Compute gradient norm */
  double gradnorm = 0.0;
  for (int i=0; i<n; i++) {
    gradnorm += pow(gradf[i], 2.0);
  }

  if (mpirank_world == 0) printf(" -->  infidelity: %1.14e, ||grad|| = %1.14e\n", 1.0 - fidelity, gradnorm);
    
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

  /* Set initial parameters. */
  // Do this on one processor only, then broadcast, to make sure that every processor starts with the same initial guess. 
  if (mpirank_world == 0) {
    if (x0filename.compare("none") != 0)  {
        /* read from file */
        read_vector(x0filename.c_str(), x0, global_n); 
    }
    else {
      /* Set to random initial guess. between [-1:1] */
      srand (1.0);    // seed 1.0 only for code debugging! seed with time(NULL) otherwise!
      for (int i=0; i<global_n; i++) {
        x0[i] = (double) rand() / ((double)RAND_MAX);
        x0[i] = 2.*x0[i] - 1.;
      }
      /* Trimm back to the box constraints */
      int j = 0;
      MasterEq* mastereq = primalbraidapp->mastereq;
      for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
          int nparam = mastereq->getOscillator(ioscil)->getNParam();
          for (int i = 0; i < 2 * nparam; i++) {
              x0[j] = x0[j] * bounds[ioscil];
              j++;
          }
      }
    }
  }

  /* Broadcast the initial guess */
  MPI_Bcast(x0, global_n, MPI_DOUBLE, 0, MPI_COMM_WORLD);


  /* Pass to oscillator */
  setDesign(global_n, x0);
  
  /* Flush initial control functions */
  if (mpirank_world == 0 && printlevel > 0) {
    int ntime = primalbraidapp->ntime;
    double dt = primalbraidapp->total_time / ntime;
    char filename[255];
    MasterEq* mastereq = primalbraidapp->mastereq;
    for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
        sprintf(filename, "%s/control_init_%02d.dat", datadir.c_str(), ioscil+1);
        mastereq->getOscillator(ioscil)->flushControl(ntime, dt, filename);
    }
  }

 
  return true;
}

/* This is called after HiOp finishes. x is LOCAL to each processor ! */
void OptimProblem::solution_callback(hiop::hiopSolveStatus status, int n, const double* x, const double* z_L, const double* z_U, int m, const double* g, const double* lambda, double obj_value) {
  
  if (mpirank_world == 0 && printlevel > 0) {
    char filename[255];
    FILE *paramfile;

    /* Print optimized parameters */
    sprintf(filename, "%s/param_optimized.dat", datadir.c_str());
    paramfile = fopen(filename, "w");
    for (int i=0; i<n; i++){
      fprintf(paramfile, "%1.14e\n", x[i]);
    }
    fclose(paramfile);

    /* Print out control functions */
    setDesign(n, x);
    int ntime = primalbraidapp->ntime;
    double dt = primalbraidapp->total_time / ntime;
    MasterEq* mastereq = primalbraidapp->mastereq;
    for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
        sprintf(filename, "%s/control_optimized_%02d.dat", datadir.c_str(), ioscil+1);
        mastereq->getOscillator(ioscil)->flushControl(ntime, dt, filename);
    }
  }
}


/* This is called after each iteration. x is LOCAL to each processor ! */
bool OptimProblem::iterate_callback(int iter, double obj_value, int n, const double* x, const double* z_L, const double* z_U, int m, const double* g, const double* lambda, double inf_pr, double inf_du, double mu, double alpha_du, double alpha_pr, int ls_trials) {

  /* Output */
  if (mpirank_world == 0 && printlevel > 0) {

    /* Compute current gradient norm. */
    const double* grad_ptr = adjointbraidapp->getReducedGradientPtr();
    double gnorm = 0.0;
    for (int i=0; i<n; i++) {
      gnorm += pow(grad_ptr[i], 2.0);
    }

    /* Print to optimization file */
    fprintf(optimfile, "%05d  %1.14e  %1.14e  %1.14e  %1.14e  %02d\n", iter, obj_value, fidelity, gnorm, inf_du, ls_trials);
    fflush(optimfile);

    /* Print parameters and controls to file */
    if (printlevel > 1) {
      char filename[255];

      /* Print optimized parameters */
      FILE *paramfile;
      sprintf(filename, "%s/param_iter%04d.dat", datadir.c_str(), iter);
      paramfile = fopen(filename, "w");
      for (int i=0; i<n; i++){
        fprintf(paramfile, "%1.14e\n", x[i]);
      }
      fclose(paramfile);

      /* Print control functions */
      setDesign(n, x);
      int ntime = primalbraidapp->ntime;
      double dt = primalbraidapp->total_time / ntime;
      MasterEq* mastereq = primalbraidapp->mastereq;
      for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
          sprintf(filename, "%s/control_iter%04d_%02d.dat", datadir.c_str(), iter, ioscil+1);
          mastereq->getOscillator(ioscil)->flushControl(ntime, dt, filename);
      }
    }
  }

  return true;
}


bool OptimProblem::get_MPI_comm(MPI_Comm& comm_out){
  comm_out = comm_hiop;
  return true;
}