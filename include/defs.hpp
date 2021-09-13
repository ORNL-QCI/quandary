#pragma once

/* Available lindblad types */
enum LindbladType {NONE, DECAY, DEPHASE, BOTH};

/* Available types of initial conditions */
enum InitialConditionType {FROMFILE, PURE, ENSEMBLE, DIAGONAL, BASIS, THREESTATES, NPLUSONE};

/* Types of optimization targets: Either gate optimization or pure state preparation */
enum TargetType {GATE,     // \rho_target = V\rho(0) V^\dagger
                 PUREM};    // \rho_target = e_m e_m^\dagger for some integer m

/* Typye of objective functions */
enum ObjectiveType {JFROBENIUS,    // weighted Frobenius norm: 1/2 * ||\rho_target - rho(T)||^2_F / w, where w = purity of \rho_target
                    JHS,           // weighted Hilber-Schmidt overlap: 1 - Tr(\rho_target^\dagger rho(T)) / w, where w = purity of \rho_target
                    JMEASURE};     // Measure a pure state: Tr(O_m \rho(T)) for observable O_m

/* Type of control fucntion evaluation: Rotating frame Real p(t), rotating frame imaginary q(t), or Lab frame f(t) */
enum ControlType {RE, IM, LAB};   

/* Linear solver */
enum LinearSolverType{
  GMRES,   // uses Petsc's GMRES solver
  NEUMANN   // uses Neuman power iterations 
};

/* Solver run type */
enum RunType {
  SIMULATION,        // Runs one simulation to compute the objective function (forward)
  GRADIENT,          // Runs a simulation followed by the adjoint for gradient computation (forward & backward)
  OPTIMIZATION,      // Runs optimization iterations
  NOTHING            // Don't run anything.
};

