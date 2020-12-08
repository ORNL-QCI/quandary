#pragma once

/* Available lindblad types */
enum LindbladType {NONE, DECAY, DEPHASE, BOTH};

/* Available types of initial conditions */
enum InitialConditionType {FROMFILE, PURE, DIAGONAL, BASIS, THREESTATES};

/* Typye of objective functions */
enum ObjectiveType {GATE_FROBENIUS,    // Compare final state to linear gate transformation of initial cond. Frobenius norm: 1/2 || Vrho(0)V - rho(T)||^2
                    GATE_TRACE,        // Trace overlap: Tr(Vrho(0)V^d rho(T))
                    EXPECTEDENERGY,   // Expected energy levels of full system
                    EXPECTEDENERGYa,   // Square average of expected energy levels of each oscillator
                    EXPECTEDENERGYb,   // Average of square expected energy levels of each oscillator
                    EXPECTEDENERGYc,   // Average of expected energy levels of each oscillator
                    ZEROTOONE,           // Experimental: 0->1 transition for one oscillator
                    GROUNDSTATE};     // Compares final state to groundstate (full matrix)

/* Type of control fucntion evaluation: Rotating frame Real p(t), rotating frame imaginary q(t), or Lab frame f(t) */
enum ControlType {RE, IM, LAB};   


/* Linear solver */
enum LinearSolverType{
  GMRES,   // uses Petsc's GMRES solver
  NEUMANN   // uses Neuman power iterations 
};

/* Solver run type */
enum RunType {
  primal,            // Runs one objective function evaluation (forward)
  adjoint,           // Runs one gradient computation (forward & backward)
  optimization,      // Run optimization 
  none               // Don't run anything.
};

