#include "optimizer.hpp"

OptimProblem::OptimProblem() {
    primalbraidapp  = NULL;
    adjointbraidapp = NULL;
    objective = 0.0;
    fidelity = 0.0;
    regul = 0.0;
    mpirank_braid = 0;
    mpisize_braid = 0;
    mpirank_space = 0;
    mpisize_space = 0;
    mpirank_optim = 0;
    mpisize_optim = 0;
    mpirank_world = 0;
    mpisize_world = 0;
    printlevel = 0;
    ninit = 0;
    ninit_local = 0;
    targetgate = NULL;
}

OptimProblem::OptimProblem(MapParam config, myBraidApp* primalbraidapp_, myAdjointBraidApp* adjointbraidapp_, MPI_Comm comm_hiop_, MPI_Comm comm_init_, std::vector<int> obj_oscilIDs_, InitialConditionType initcondtype_, int ninit_) {
    primalbraidapp  = primalbraidapp_;
    adjointbraidapp = adjointbraidapp_;
    comm_hiop = comm_hiop_;
    comm_init = comm_init_;
    obj_oscilIDs = obj_oscilIDs_;
    initcond_type = initcondtype_;
    ninit = ninit_;

    /* Store ranks and sizes of communicators */
    MPI_Comm_rank(primalbraidapp->comm_braid, &mpirank_braid);
    MPI_Comm_size(primalbraidapp->comm_braid, &mpisize_braid);
    MPI_Comm_rank(PETSC_COMM_WORLD, &mpirank_space);
    MPI_Comm_size(PETSC_COMM_WORLD, &mpisize_space);
    MPI_Comm_rank(comm_hiop, &mpirank_optim);
    MPI_Comm_size(comm_hiop, &mpisize_optim);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpirank_world);
    MPI_Comm_size(MPI_COMM_WORLD, &mpisize_world);
    MPI_Comm_rank(comm_init, &mpirank_init);
    MPI_Comm_size(comm_init, &mpisize_init);

    /* Store number of initial conditions per init-processor group */
    ninit_local = ninit / mpisize_init; 

    /* Read config options */
    regul = config.GetDoubleParam("optim_regul", 1e-4);
    optiminit_type = config.GetStrParam("optim_init", "zero");
    datadir = config.GetStrParam("datadir", "./data_out");
    printlevel = config.GetIntParam("optim_printlevel", 1);
    config.GetVecDoubleParam("optim_bounds", bounds, 1e20);
    assert (bounds.size() >= primalbraidapp->mastereq->getNOscillators());
    /* If constant initialization, read in amplitudes */
    if (optiminit_type.compare("constant") == 0 ){ // set constant controls. 
      config.GetVecDoubleParam("optim_init_const", init_ampl, 0.0);
    }
    /* Get type of objective function */
    std::vector<std::string> objective_str;
    config.GetVecStrParam("optim_objective", objective_str);
    if ( objective_str[0].compare("gate") ==0 ) {
      objective_type = GATE;
      /* Read and initialize the targetgate */
      assert ( objective_str.size() >=2 );
      if      (objective_str[1].compare("none") == 0) targetgate = new Gate(); // dummy gate. do nothing
      else if (objective_str[1].compare("xgate") == 0) targetgate = new XGate(); 
      else if (objective_str[1].compare("ygate") == 0) targetgate = new YGate(); 
      else if (objective_str[1].compare("zgate") == 0) targetgate = new ZGate();
      else if (objective_str[1].compare("hadamard") == 0) targetgate = new HadamardGate();
      else if (objective_str[1].compare("cnot") == 0) targetgate = new CNOT(); 
      else {
        printf("\n\n ERROR: Unnown gate type: %s.\n", objective_str[1].c_str());
        printf(" Available gates are 'none', 'xgate', 'ygate', 'zgate', 'hadamard', 'cnot'\n");
        exit(1);
      }
    } 
    else if (objective_str[0].compare("expectedEnergy")==0) objective_type = EXPECTEDENERGY;
    else if (objective_str[0].compare("groundstate")   ==0) objective_type = GROUNDSTATE;
    else {
        printf("\n\n ERROR: Unknown objective function: %s\n", objective_str[0].c_str());
        exit(1);
    }

    /* Prepare primal and adjoint initial conditions */
    VecCreate(PETSC_COMM_WORLD, &initcond_re); 
    VecSetSizes(initcond_re,PETSC_DECIDE,primalbraidapp->mastereq->getDim());
    VecSetFromOptions(initcond_re);
    VecDuplicate(initcond_re, &initcond_im);
    VecDuplicate(initcond_re, &initcond_re_bar);
    VecDuplicate(initcond_re, &initcond_im_bar);


    /* Initialize with tensor product of unit vectors. */
    if (initcond_type == PURE) { 

      std::vector<std::string> initcondstr;
      config.GetVecStrParam("optim_initialcondition", initcondstr);
      std::vector<int> unitids;
      for (int i=1; i<initcondstr.size(); i++) unitids.push_back(atoi(initcondstr[i].c_str()));
      assert (unitids.size() == primalbraidapp->mastereq->getNOscillators());
      // Compute index of diagonal elements that is one.
      int diag_id = 0.0;
      for (int k=0; k < unitids.size(); k++) {
        assert (unitids[k] < primalbraidapp->mastereq->getOscillator(k)->getNLevels());
        int dim_postkron = 1;
        for (int m=k+1; m < unitids.size(); m++) {
          dim_postkron *= primalbraidapp->mastereq->getOscillator(m)->getNLevels();
        }
        diag_id += unitids[k] * dim_postkron;
      }
      int vec_id = diag_id * (int)sqrt(primalbraidapp->mastereq->getDim()) + diag_id;
      VecSetValue(initcond_re, vec_id, 1.0, INSERT_VALUES);
    }

    /* Read initial condition from file */
    if (initcond_type == FROMFILE) { 

      int dim = primalbraidapp->mastereq->getDim();
      double * vec = new double[2*dim];

      std::vector<std::string> initcondstr;
      config.GetVecStrParam("optim_initialcondition", initcondstr);
      if (mpirank_world == 0) {
        assert (initcondstr.size()==2);
        std::string filename = initcondstr[1];
        read_vector(filename.c_str(), vec, 2*dim);
      }
      MPI_Bcast(vec, 2*dim, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      for (int i = 0; i < dim; i++) {
        if (vec[i]     != 0.0) VecSetValue(initcond_re, i, vec[i],     INSERT_VALUES);
        if (vec[i+dim] != 0.0) VecSetValue(initcond_im, i, vec[i+dim], INSERT_VALUES);
      }
      delete [] vec;
    }
    
    VecAssemblyBegin(initcond_re); VecAssemblyEnd(initcond_re);
    VecAssemblyBegin(initcond_im); VecAssemblyEnd(initcond_im);
   
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
  // if (targetgate != NULL) delete targetgate;
}



void OptimProblem::setDesign(int n, const double* x) {

  MasterEq* mastereq = primalbraidapp->mastereq;

  /* Pass design vector x to oscillator */
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {

      /* Design storage: x = (params_oscil0, params_oscil2, ... ) */
      int nparam = mastereq->getOscillator(ioscil)->getNParams();
      int j = ioscil * nparam;
      const double* ptr_shift = x + j;
      mastereq->getOscillator(ioscil)->setParams(ptr_shift);
  }
}


void OptimProblem::getDesign(int n, double* x){

  double *paramRe, *paramIm;
  int nparam;
  int j = 0;
  /* Iterate over oscillators */
  MasterEq* mastereq = primalbraidapp->mastereq;
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {

      nparam = mastereq->getOscillator(ioscil)->getNParams();
      int j = ioscil * nparam;
      double* ptr_shift = x + j;
      mastereq->getOscillator(ioscil)->getParams(ptr_shift);
  }
}

bool OptimProblem::get_prob_sizes(long long& n, long long& m) {

  // n - number of design variables 
  n = 0;
  MasterEq* mastereq = primalbraidapp->mastereq;
  for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
      n += mastereq->getOscillator(ioscil)->getNParams(); 
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
      int nparam = mastereq->getOscillator(ioscil)->getNParams();
      /* Iterate over real and imaginary part */
      for (int i = 0; i < nparam; i++) {
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

  if (mpirank_world == 0) printf(" EVAL F... \n");
  Vec finalstate = NULL;
  /* Run simulation, only if x_in is new. Otherwise, f(x_in) has been computed already and stored in fidelity. */
  // this is fishy. check if fidelity is computed correctly in grad_f
  // if (new_x) { 

    /* Pass design vector x to oscillator */
    setDesign(n, x_in);

    /*  Iterate over initial condition */
    objective = 0.0;
    for (int iinit = 0; iinit < ninit_local; iinit++) {
      
      // printf("FWD %d/%d\n", iinit, ninit_local);
      /* Prepare the initial condition */
      int initid = assembleInitialCondition(iinit);
      // if (mpirank_braid == 0) printf("%d: %d FWD. \n", mpirank_init, initid);

      /* Run forward with initial condition initid*/
      primalbraidapp->PreProcess(initid);
      primalbraidapp->setInitialCondition(initcond_re, initcond_im);
      primalbraidapp->Drive();
      finalstate = primalbraidapp->PostProcess(); // this return NULL for all but the last time processor

      /* Add to objective function */
      objective += objFunc(finalstate);
      // if (mpirank_braid == 0) printf("%d: local objective: %1.14e\n", mpirank_init, obj_local);
    }
  // }

  /* Broadcast objective from last to all time processors */
  MPI_Bcast(&objective, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);

  /* Sum up objective from all initial conditions */
  double myobj = objective;
  MPI_Allreduce(&myobj, &objective, 1, MPI_DOUBLE, MPI_SUM, comm_init);

  // if (mpirank_init == 0) printf("%d: global sum objective: %1.14e\n\n", mpirank_init, objective);

  /* Compute average objective */
  objective = 1. / ninit * objective;

  /* Compute fidelity 1. - objective */
  fidelity = 1. - objective; 

  /* Add regularization objective += gamma/(2n) * ||x||^2*/
  for (int i=0; i<n; i++) {
    objective += regul / (2.0*n) * pow(x_in[i], 2.0);
  }

  /* Return objective value */
  obj_value = objective;

  return true;
}


bool OptimProblem::eval_grad_f(const long long& n, const double* x_in, bool new_x, double* gradf){
  if (mpirank_world == 0) printf(" EVAL GRAD F...\n");

  Vec finalstate = NULL;

  /* Pass x to Oscillator */
  setDesign(n, x_in);

  /* Derivative of regularization gamma * ||x||^2 (ADD ON ONE PROC ONLY!) */
  for (int i=0; i<n; i++) {
    if (mpirank_init == 0 && mpirank_braid == 0) gradf[i] = regul / n * x_in[i];
    else gradf[i] = 0.0;
  }

  /* Derivative objective 1/(2N^2) J */
  double obj_bar = 1. / ninit;

  /* Iterate over initial conditions */
  objective = 0.0;
  for (int iinit = 0; iinit < ninit_local; iinit++) {

    /* Prepare the initial condition */
    int initid = assembleInitialCondition(iinit);

    /* --- Solve primal --- */
    // if (mpirank_braid == 0) printf("%d: %d FWD. ", mpirank_init, initid);
    
    primalbraidapp->PreProcess(initid);
    primalbraidapp->setInitialCondition(initcond_re, initcond_im);
    primalbraidapp->Drive();
    finalstate = primalbraidapp->PostProcess(); // returns NULL if not stored on this proc

    /* Add to objective function */
    objective += objFunc(finalstate);
    // if (mpirank_braid == 0) printf("%d: local objective: %1.14e\n", mpirank_init, obj_local);

    /* --- Solve adjoint --- */
    // if (mpirank_braid == 0) printf("%d: %d BWD.", mpirank_init, initid);
    
    /* Derivative of objective function */
    objFunc_diff(finalstate, obj_bar);

    adjointbraidapp->PreProcess(initid);
    adjointbraidapp->setInitialCondition(initcond_re_bar, initcond_im_bar);
    adjointbraidapp->Drive();
    adjointbraidapp->PostProcess();

    /* Add to Ipopt's gradient */
    const double* grad_ptr = adjointbraidapp->getReducedGradientPtr();
    for (int i=0; i<n; i++) {
        gradf[i] += grad_ptr[i]; 
    }
  }
  
  /* Broadcast objective from last to all processors */
  MPI_Bcast(&objective, 1, MPI_DOUBLE, mpisize_braid-1, primalbraidapp->comm_braid);

  /* Sum up objective from all initial conditions */
  double myobj = objective;
  MPI_Allreduce(&myobj, &objective, 1, MPI_DOUBLE, MPI_SUM, comm_init);
  // if (mpirank_init == 0) printf("%d: global sum objective: %1.14e\n\n", mpirank_init, objective);

  /* Compute objective 1/(2*N^2) ||W-G||_F^2 */
  objective = 1. / ninit * objective;

  /* Compute fidelity 1/N^2 |trace|^2 */
  fidelity = 1. - objective;

  /* Add regularization: objective += gamma/(2n)*||x||^2 */
  for (int i=0; i<n; i++) {
    objective += regul / (2.0*n) * pow(x_in[i], 2.0);
  }

  /* Sum up the gradient from all braid processors */
  double* mygrad = new double[n];
  for (int i=0; i<n; i++) {
    mygrad[i] = gradf[i];
  }
  MPI_Allreduce(mygrad, gradf, n, MPI_DOUBLE, MPI_SUM, primalbraidapp->comm_braid);

  /* Sum up the gradient from all initial condition processors */
  for (int i=0; i<n; i++) {
    mygrad[i] = gradf[i];
  }
  MPI_Allreduce(mygrad, gradf, n, MPI_DOUBLE, MPI_SUM, comm_init);

  /* Compute gradient norm */
  double gradnorm = 0.0;
  for (int i=0; i<n; i++) {
    gradnorm += pow(gradf[i], 2.0);
  }
  // if (mpirank_world == 0) printf("%d: ||grad|| = %1.14e\n", mpirank_init, gradnorm);

  delete [] mygrad;
    
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
    if (optiminit_type.compare("constant") == 0 ){ // set constant controls. 
      int j = 0;
      MasterEq* mastereq = primalbraidapp->mastereq;
      assert(init_ampl.size() == mastereq->getNOscillators());
      for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
          int nparam = mastereq->getOscillator(ioscil)->getNParams();
          for (int i = 0; i < nparam; i++) {
              x0[j] = init_ampl[ioscil];
              j++;
          }
      }
    } else if (optiminit_type.compare("zero") == 0)  { // init with zero
      for (int i=0; i<global_n; i++) {
        x0[i] = 0.0;
      }
    } else if ( optiminit_type.compare("random") == 0 || optiminit_type.compare("random_seed") == 0)  { // init random

      /* Set the random seed */
      if ( optiminit_type.compare("random") == 0) srand(1);  // fixed seed
      else srand(time(0)); // random seed

      /* Set to random initial guess. between [-1:1] */
      for (int i=0; i<global_n; i++) {
        x0[i] = (double) rand() / ((double)RAND_MAX);
        x0[i] = 2.*x0[i] - 1.;
      }
      /* Trimm back to the box constraints */
      int j = 0;
      MasterEq* mastereq = primalbraidapp->mastereq;
      for (int ioscil = 0; ioscil < mastereq->getNOscillators(); ioscil++) {
          if (bounds[ioscil] > 1.0) continue;
          int nparam = mastereq->getOscillator(ioscil)->getNParams();
          for (int i = 0; i < nparam; i++) {
              x0[j] = x0[j] * bounds[ioscil];
              j++;
          }
      }
    } else {
      /* read from file */
      read_vector(optiminit_type.c_str(), x0, global_n); 
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
    if (printlevel > 1 || iter % 10 == 0 ) {
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


int OptimProblem::assembleInitialCondition(int iinit_local){
  int dim_post;

  int initID = -1;    // Output: ID for this initial condition */
  int dim_rho = (int) sqrt(primalbraidapp->mastereq->getDim()); // N

  /* Translate local iinit to this processor's domain [rank * ninit_local, (rank+1) * ninit_local - 1] */
  int iinit = mpirank_init * ninit_local + iinit_local;

  /* Switch over type of initial condition */
  switch (initcond_type) {

    case FROMFILE:
      /* Do nothing. Init cond is already stored in initcond_re, initcond_im */
      break;

    case PURE:
      /* Do nothing. Init cond is already stored in initcond_re, initcond_im */
      break;

    case DIAGONAL:
      int row, diagelem;

      /* Reset the initial conditions */
      VecZeroEntries(initcond_re); 
      VecZeroEntries(initcond_im); 

      /* Get dimension of partial system behind last oscillator ID */
      dim_post = 1;
      for (int k = obj_oscilIDs[obj_oscilIDs.size()-1] + 1; k < primalbraidapp->mastereq->getNOscillators(); k++) {
        dim_post *= primalbraidapp->mastereq->getOscillator(k)->getNLevels();
      }

      /* Compute index of the nonzero element in rho_m(0) = E_pre \otimes |m><m| \otimes E_post */
      diagelem = iinit * dim_post;
      // /* Position in vectorized q(0) */
      row = diagelem * dim_rho + diagelem;

      /* Assemble */
      VecSetValue(initcond_re, row, 1.0, INSERT_VALUES);
      VecAssemblyBegin(initcond_re);
      VecAssemblyEnd(initcond_re);

      /* Set initial conditon ID */
      initID = iinit * ( (int) sqrt(ninit) ) + iinit;

      break;

    case BASIS:

      /* Reset the initial conditions */
      VecZeroEntries(initcond_re); 
      VecZeroEntries(initcond_im); 

      /* Get dimension of partial system behind last oscillator ID */
      dim_post = 1;
      for (int k = obj_oscilIDs[obj_oscilIDs.size()-1] + 1; k < primalbraidapp->mastereq->getNOscillators(); k++) {
        dim_post *= primalbraidapp->mastereq->getOscillator(k)->getNLevels();
      }

      // /* Get index (k,j) of basis element B_{k,j} for this initial condition index iinit */
      int k, j;
      k = iinit % ( (int) sqrt(ninit) );
      j = (int) iinit / ( (int) sqrt(ninit) );   

      if (k == j) {
        /* B_{kk} = E_{kk} -> set only one element at (k,k) */
        int elemID = j * dim_post * dim_rho + k * dim_post;
        double val = 1.0;
        VecSetValues(initcond_re, 1, &elemID, &val, INSERT_VALUES);
        VecAssemblyBegin(initcond_re);
        VecAssemblyEnd(initcond_re);
      } else {
      //   /* B_{kj} contains four non-zeros, two per row */
        int* rows = new int[4];
        double* vals = new double[4];

        rows[0] = k * dim_post * dim_rho + k * dim_post; // (k,k)
        rows[1] = j * dim_post * dim_rho + j * dim_post; // (j,j)
        rows[2] = j * dim_post * dim_rho + k * dim_post; // (k,j)
        rows[3] = k * dim_post * dim_rho + j * dim_post; // (j,k)

        if (k < j) { // B_{kj} = 1/2(E_kk + E_jj) + 1/2(E_kj + E_jk)
          vals[0] = 0.5;
          vals[1] = 0.5;
          vals[2] = 0.5;
          vals[3] = 0.5;
          VecSetValues(initcond_re, 4, rows, vals, INSERT_VALUES);
          VecAssemblyBegin(initcond_re);
          VecAssemblyEnd(initcond_re);
        } else {  // B_{kj} = 1/2(E_kk + E_jj) + i/2(E_jk - E_kj)
          vals[0] = 0.5;
          vals[1] = 0.5;
          VecSetValues(initcond_re, 2, rows, vals, INSERT_VALUES); // diagonal, real
          VecAssemblyBegin(initcond_re);
          VecAssemblyEnd(initcond_re);
          vals[0] = -0.5;
          vals[1] = 0.5;
          VecSetValues(initcond_im, 2, rows+2, vals, INSERT_VALUES); // off-diagonals, imaginary
          VecAssemblyBegin(initcond_im);
          VecAssemblyEnd(initcond_im);
        }

        delete [] rows; 
        delete [] vals;
      }

      /* Set initial condition ID */
      initID = j * ( (int) sqrt(ninit)) + k;

      break;

    default:
      printf("ERROR! Wrong initial condition type: %d\n This should never happen!\n", initcond_type);
      exit(1);
  }

  // printf("InitCond %d \n", iinit);
  // VecView(initcond_re, PETSC_VIEWER_STDOUT_WORLD);
  // VecView(initcond_im, PETSC_VIEWER_STDOUT_WORLD);
  
  return initID;
}



double OptimProblem::objFunc(Vec finalstate) {
  double obj_local = 0.0;

  if (finalstate != NULL) {

    switch (objective_type) {
      case GATE:
        /* compare state to linear transformation of initial conditions */
        targetgate->compare(finalstate, initcond_re, initcond_im, obj_local);
        break;

      case EXPECTEDENERGY:
        /* compute the expected value of energy levels for each oscillator */
        obj_local = 0.0;
        for (int i=0; i<obj_oscilIDs.size(); i++) {
          obj_local += primalbraidapp->mastereq->getOscillator(obj_oscilIDs[i])->expectedEnergy(finalstate);
        }
        break;

      case GROUNDSTATE:
        /* compare full or pariatl state to groundstate */
        MasterEq *meq= primalbraidapp->mastereq;
        Vec state;

        /* If sub-system is requested, compute reduced density operator first */
        if (obj_oscilIDs.size() < primalbraidapp->mastereq->getNOscillators()) { 
          
          /* Get dimensions of preceding and following subsystem */
          int dim_pre  = 1; 
          int dim_post = 1;
          for (int iosc = 0; iosc < meq->getNOscillators(); iosc++) {
            if ( iosc < obj_oscilIDs[0])                      
              dim_pre  *= meq->getOscillator(iosc)->getNLevels();
            if ( iosc > obj_oscilIDs[obj_oscilIDs.size()-1])   
              dim_post *= meq->getOscillator(iosc)->getNLevels();
          }

          /* Create reduced density matrix */
          int dim_reduced = 1;
          for (int i = 0; i < obj_oscilIDs.size();i++) {
            dim_reduced *= meq->getOscillator(obj_oscilIDs[i])->getNLevels();
          }
          VecCreate(PETSC_COMM_WORLD, &state);
          VecSetSizes(state, PETSC_DECIDE, 2*dim_reduced*dim_reduced);
          VecSetFromOptions(state);

          /* Fill reduced density matrix */
          meq->reducedDensity(finalstate, &state, dim_pre, dim_post, dim_reduced);

        } else { // full density matrix system 

           state = finalstate; 

        }

        // PetscViewerSetFormat(PETSC_VIEWER_STDOUT_WORLD, 	PETSC_VIEWER_ASCII_MATLAB );
        // VecView(state, PETSC_VIEWER_STDOUT_WORLD);

        /* Compute frobenius norm: frob = || q(T) - e_1 ||^2 */
        int dimstate;
        const PetscScalar *stateptr;
        VecGetSize(state, &dimstate);
        VecGetArrayRead(state, &stateptr);
        obj_local = 0.0;
        obj_local += pow(stateptr[0] - 1.0, 2); 
        for (int i = 1; i < dimstate; i++){
           obj_local += pow(stateptr[i], 2);
        }
        VecRestoreArrayRead(state, &stateptr);

        /* obj = 1/2 * J */
        obj_local *= 1./2.;

        /* Destroy reduced density matrix, if it has been created */
        if (obj_oscilIDs.size() < primalbraidapp->mastereq->getNOscillators()) { 
          VecDestroy(&state);
        }

        break;
    }

  }

  return obj_local;
}


void OptimProblem::objFunc_diff(Vec finalstate, double obj_bar) {

  /* Reset adjoints */
  VecZeroEntries(initcond_re_bar);
  VecZeroEntries(initcond_im_bar);

  if (finalstate != NULL) {
    switch (objective_type) {
      case GATE:
        targetgate->compare_diff(finalstate, initcond_re, initcond_im, initcond_re_bar, initcond_im_bar, obj_bar);
        break;

      case EXPECTEDENERGY:
        for (int i=0; i<obj_oscilIDs.size(); i++) {
          primalbraidapp->mastereq->getOscillator(obj_oscilIDs[i])->expectedEnergy_diff(finalstate, initcond_re_bar, initcond_im_bar, obj_bar);
        }
        break;

    case GROUNDSTATE:

        MasterEq *meq= primalbraidapp->mastereq;
        Vec state;
        int dim_pre = 1;
        int dim_post = 1;
        int dim_reduced = 1;

        /* If sub-system is requested, compute reduced density operator first */
        if (obj_oscilIDs.size() < primalbraidapp->mastereq->getNOscillators()) { 
          
          /* Get dimensions of preceding and following subsystem */
          for (int iosc = 0; iosc < meq->getNOscillators(); iosc++) {
            if ( iosc < obj_oscilIDs[0])                      
              dim_pre  *= meq->getOscillator(iosc)->getNLevels();
            if ( iosc > obj_oscilIDs[obj_oscilIDs.size()-1])   
              dim_post *= meq->getOscillator(iosc)->getNLevels();
          }

          /* Create reduced density matrix */
          for (int i = 0; i < obj_oscilIDs.size();i++) {
            dim_reduced *= meq->getOscillator(obj_oscilIDs[i])->getNLevels();
          }
          VecCreate(PETSC_COMM_WORLD, &state);
          VecSetSizes(state, PETSC_DECIDE, 2*dim_reduced*dim_reduced);
          VecSetFromOptions(state);

          /* Fill reduced density matrix */
          meq->reducedDensity(finalstate, &state, dim_pre, dim_post, dim_reduced);

        } else { // full density matrix system 

           state = finalstate; 

        }

       /* Get real and imag part of final and initial primal and adjoint states, x = [u,v] */
      const PetscScalar *stateptr;
      PetscScalar *statebarptr;
      Vec statebar;
      VecDuplicate(state, &statebar);
      VecGetArrayRead(state, &stateptr);
      VecGetArray(statebar, &statebarptr);

      /* Derivative of 1/2 * J */
      double dfb = 1./2. * obj_bar;

      /* Derivative of frobenius norm: 2 * (q(T) - e_1) * frob_bar */
      int dimstate;
      VecGetSize(state, &dimstate);
      statebarptr[0] += 2. * ( stateptr[0] - 1.0 ) * dfb;
      for (int i=1; i<dimstate; i++) {
        statebarptr[i] += 2. * stateptr[i] * dfb;
      }
      VecRestoreArrayRead(state, &stateptr);

      /* Derivative of partial trace */
      if (obj_oscilIDs.size() < meq->getNOscillators()) {
        meq->reducedDensity_diff(statebar, initcond_re_bar, initcond_im_bar, dim_pre, dim_post, dim_reduced);
        VecDestroy(&state);
      } else {
        /* Split statebar into initcond_rebar, initcond_im_bar */
        PetscScalar *u0_barptr, *v0_barptr;
        VecGetArray(initcond_re_bar, &u0_barptr);
        VecGetArray(initcond_im_bar, &v0_barptr);
        const PetscScalar *statebarptr;
        VecGetArrayRead(statebar, &statebarptr);
        for (int i=0; i<dimstate/2; i++){
          u0_barptr[i] += statebarptr[i];
          v0_barptr[i] += statebarptr[i + dimstate/2];
        }
        VecRestoreArrayRead(statebar, &statebarptr);
        VecRestoreArray(initcond_re_bar, &u0_barptr);
        VecRestoreArray(initcond_im_bar, &v0_barptr);
      }

      VecDestroy(&statebar);
    }
  }
}