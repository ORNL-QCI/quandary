#include "gate.hpp"

Gate::Gate(){
  dim_v   = 0;
  dim_vec = 0;
}

Gate::Gate(int dim_v_) {
  dim_v = dim_v_; 
  dim_vec = (int) pow(dim_v,2);      // vectorized version squares dimensions.

  MPI_Comm_rank(PETSC_COMM_WORLD, &mpirank_petsc);

  // /* Set the frequencies */
  // assert(freq_.size() >= noscillators);
  // for (int i=0; i<noscillators; i++) {
  //   omega.push_back(2.*M_PI * freq_[i]);
  // }

  /* Allocate Va, Vb */
  MatCreate(PETSC_COMM_WORLD, &Va);
  MatCreate(PETSC_COMM_WORLD, &Vb);
  MatSetSizes(Va, PETSC_DECIDE, PETSC_DECIDE, dim_v, dim_v);
  MatSetSizes(Vb, PETSC_DECIDE, PETSC_DECIDE, dim_v, dim_v);
  MatSetUp(Va);
  MatSetUp(Vb);
  MatAssemblyBegin(Va, MAT_FINAL_ASSEMBLY);
  MatAssemblyBegin(Vb, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Va, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Vb, MAT_FINAL_ASSEMBLY);

  /* Allocate ReG = Re(\bar V \kron V), ImG = Im(\bar V \kron V) */
  MatCreate(PETSC_COMM_WORLD, &ReG);
  MatCreate(PETSC_COMM_WORLD, &ImG);
  MatSetSizes(ReG, PETSC_DECIDE, PETSC_DECIDE, dim_vec, dim_vec);
  MatSetSizes(ImG, PETSC_DECIDE, PETSC_DECIDE, dim_vec, dim_vec);
  MatSetUp(ReG);
  MatSetUp(ImG);
  MatAssemblyBegin(ReG, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(ReG, MAT_FINAL_ASSEMBLY);
  MatAssemblyBegin(ImG, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(ImG, MAT_FINAL_ASSEMBLY);

  /* Create auxiliary vectors */
  MatCreateVecs(ReG, &Re0, NULL);
  MatCreateVecs(ReG, &Im0, NULL);

}

Gate::~Gate(){
  if (dim_vec == 0) return;
  MatDestroy(&ReG);
  MatDestroy(&ImG);
  MatDestroy(&Va);
  MatDestroy(&Vb);
  VecDestroy(&Re0);
  VecDestroy(&Im0);
}


void Gate::assembleGate(){
  /* Compute ReG = Re(\bar V \kron V) = A\kron A + B\kron B  */
  AkronB(dim_v, Va, Va,  1.0, &ReG, ADD_VALUES);
  AkronB(dim_v, Vb, Vb,  1.0, &ReG, ADD_VALUES);
  /* Compute ImG = Im(\bar V\kron V) = A\kron B - B\kron A */
  AkronB(dim_v, Va, Vb,  1.0, &ImG, ADD_VALUES);
  AkronB(dim_v, Vb, Va, -1.0, &ImG, ADD_VALUES);

  MatAssemblyBegin(ReG, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(ReG, MAT_FINAL_ASSEMBLY);
  MatAssemblyBegin(ImG, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(ImG, MAT_FINAL_ASSEMBLY);

  // MatView(ReG, PETSC_VIEWER_STDOUT_WORLD);
  // MatView(ImG, PETSC_VIEWER_STDOUT_WORLD);
  // exit(1);
}


void Gate::compare(Vec finalstate, Vec rho0, double& frob){
  frob = 0.0;

  /* Exit, if this is a dummy gate */
  if (dim_vec == 0) {
    return;
  }

  /* Create vector strides for accessing real and imaginary part of co-located x */
  int ilow, iupp;
  VecGetOwnershipRange(finalstate, &ilow, &iupp);
  int dimis = (iupp - ilow)/2;
  IS isu, isv;
  ISCreateStride(PETSC_COMM_WORLD, dimis, ilow, 2, &isu);
  ISCreateStride(PETSC_COMM_WORLD, dimis, ilow+1, 2, &isv);

  /* Get real and imag part of final state, x = [u,v] */
  Vec ufinal, vfinal, u0, v0;
  VecGetSubVector(finalstate, isu, &ufinal);
  VecGetSubVector(finalstate, isv, &vfinal);
  VecGetSubVector(rho0, isu, &u0);
  VecGetSubVector(rho0, isv, &v0);


  const PetscScalar *ufinalptr, *vfinalptr, *Re0ptr, *Im0ptr;
  VecGetArrayRead(ufinal, &ufinalptr);
  VecGetArrayRead(vfinal, &vfinalptr);

  /* Make sure that state dimensions match the gate dimension */
  int dimstate, dimG;
  VecGetSize(finalstate, &dimstate); 
  VecGetSize(Re0, &dimG);
  if (dimstate/2 != dimG) {
    printf("\n ERROR: Target gate dimension %d doesn't match system dimension %u\n", dimG, dimstate/2);
    exit(1);
  }

  /* Add real part of frobenius norm || u - ReG*u0 + ImG*v0 ||^2 */
  MatMult(ReG, u0, Re0);
  MatMult(ImG, v0, Im0);
  VecGetArrayRead(Re0, &Re0ptr);
  VecGetArrayRead(Im0, &Im0ptr);
  for (int j=0; j<dim_vec; j++) {
    frob += pow(ufinalptr[j] - Re0ptr[j] + Im0ptr[j],2);
  }
  VecRestoreArrayRead(Re0, &Re0ptr);
  VecRestoreArrayRead(Im0, &Im0ptr);

  /* Add imaginary part of frobenius norm || v - ReG*v0 - ImG*u0 ||^2 */
  MatMult(ReG, v0, Re0);
  MatMult(ImG, u0, Im0);
  VecGetArrayRead(Re0, &Re0ptr);
  VecGetArrayRead(Im0, &Im0ptr);
  for (int j=0; j<dim_vec; j++) {
    frob += pow(vfinalptr[j] - Re0ptr[j] - Im0ptr[j],2);
  }
  VecRestoreArrayRead(Re0, &Re0ptr);
  VecRestoreArrayRead(Im0, &Im0ptr);

  /* obj = 1/2 * || finalstate - gate*rho(0) ||^2 */
  frob *= 1./2.;

 /* Restore */
  VecRestoreArrayRead(ufinal, &ufinalptr);
  VecRestoreArrayRead(vfinal, &vfinalptr);
  VecRestoreSubVector(finalstate, isu, &ufinal);
  VecRestoreSubVector(finalstate, isv, &vfinal);

  /* Free vindex strides */
  ISDestroy(&isu);
  ISDestroy(&isv);
}


void Gate::compare_diff(const Vec finalstate, const Vec rho0, Vec rho0_bar, const double frob_bar){

  /* Exit, if this is a dummy gate */
  if (dim_vec == 0) {
    return;
  }

  Vec ufinal, vfinal, u0_bar, v0_bar, u0, v0;
  const PetscScalar *ufinalptr, *vfinalptr, *Re0ptr, *Im0ptr;
  PetscScalar *u0_barptr, *v0_barptr;

  /* Create vector strides for accessing real and imaginary part of co-located x */
  int ilow, iupp;
  VecGetOwnershipRange(finalstate, &ilow, &iupp);
  int dimis = (iupp - ilow)/2;
  IS isu, isv;
  ISCreateStride(PETSC_COMM_WORLD, dimis, ilow, 2, &isu);
  ISCreateStride(PETSC_COMM_WORLD, dimis, ilow+1, 2, &isv);


  /* Get real and imag part of final state, initial state, and adjoint */
  VecGetSubVector(finalstate, isu, &ufinal);
  VecGetSubVector(finalstate, isv, &vfinal);
  VecGetSubVector(rho0, isu, &u0);
  VecGetSubVector(rho0, isv, &v0);
  VecGetSubVector(rho0_bar, isu, &u0_bar);
  VecGetSubVector(rho0_bar, isv, &v0_bar);

  /* Get pointers  */
  VecGetArrayRead(ufinal, &ufinalptr);
  VecGetArrayRead(vfinal, &vfinalptr);
  VecGetArray(u0_bar, &u0_barptr);
  VecGetArray(v0_bar, &v0_barptr);

  /* Derivative of 1/2 * J */
  double dfb = 1./2. * frob_bar;

  /* Derivative of read part of frobenius norm: 2 * (u - ReG*u0 + ImG*v0) * dfb*/
  MatMult(ReG, u0, Re0);
  MatMult(ImG, v0, Im0);
  VecGetArrayRead(Re0, &Re0ptr);
  VecGetArrayRead(Im0, &Im0ptr);
  for (int j=0; j<dim_vec; j++) {
    u0_barptr[j] = 2. * ( ufinalptr[j] - Re0ptr[j] + Im0ptr[j] ) * dfb;
  }
  VecRestoreArrayRead(Re0, &Re0ptr);
  VecRestoreArrayRead(Im0, &Im0ptr);

  /* Derivative of imaginary part of frobenius norm 2 * (v - ReG*v0 - ImG*u0) * dfb */
  MatMult(ReG, v0, Re0);
  MatMult(ImG, u0, Im0);
  VecGetArrayRead(Re0, &Re0ptr);
  VecGetArrayRead(Im0, &Im0ptr);
  for (int j=0; j<dim_vec; j++) {
    v0_barptr[j] = 2. * ( vfinalptr[j] - Re0ptr[j] - Im0ptr[j] ) * dfb;
  }
  VecRestoreArrayRead(Re0, &Re0ptr);
  VecRestoreArrayRead(Im0, &Im0ptr);

 /* Restore pointers */
  VecRestoreArrayRead(ufinal, &ufinalptr);
  VecRestoreArrayRead(vfinal, &vfinalptr);
  VecRestoreArray(u0_bar, &u0_barptr);
  VecRestoreArray(v0_bar, &v0_barptr);

  /* Restore final, initial and adjoint state */
  VecRestoreSubVector(finalstate, isu, &ufinal);
  VecRestoreSubVector(finalstate, isv, &vfinal);
  VecRestoreSubVector(rho0, isu, &u0);
  VecRestoreSubVector(rho0, isv, &v0);
  VecRestoreSubVector(rho0_bar, isu, &u0_bar);
  VecRestoreSubVector(rho0_bar, isv, &v0_bar);

  /* Free vindex strides */
  ISDestroy(&isu);
  ISDestroy(&isv);

}

XGate::XGate() : Gate(2) {

  /* Fill Va = Re(V) and Vb = Im(V), V = Va + iVb */
  /* Va = 0 1    Vb = 0
   *      1 0 
   */
  MatSetValue(Va, 0, 1, 1.0, INSERT_VALUES);
  MatSetValue(Va, 1, 0, 1.0, INSERT_VALUES);
  MatAssemblyBegin(Va, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Va, MAT_FINAL_ASSEMBLY);

  /* Assemble vectorized target gate \bar V \kron V from  V = Va + i Vb*/
  assembleGate();
}

XGate::~XGate() {}

YGate::YGate() : Gate(2) { 
  
  /* Fill A = Re(V) and B = Im(V), V = A + iB */
  /* A = 0     B = 0 -1
   *               1  0
   */
  MatSetValue(Vb, 0, 1, -1.0, INSERT_VALUES);
  MatSetValue(Vb, 1, 0,  1.0, INSERT_VALUES);
  MatAssemblyBegin(Vb, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Vb, MAT_FINAL_ASSEMBLY);

  /* Assemble vectorized target gate \bar V \kron V from  V = Va + i Vb*/
  assembleGate();
}
YGate::~YGate() {}

ZGate::ZGate() : Gate(2) { 

  /* Fill A = Re(V) and B = Im(V), V = A + iB */
  /* A =  1  0     B = 0
   *      0 -1
   */
  MatSetValue(Vb, 0, 0,  1.0, INSERT_VALUES);
  MatSetValue(Vb, 1, 1, -1.0, INSERT_VALUES);
  MatAssemblyBegin(Vb, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Vb, MAT_FINAL_ASSEMBLY);

  /* Assemble target gate \bar V \kron V from  V = Va + i Vb*/
  assembleGate();
}

ZGate::~ZGate() {}

HadamardGate::HadamardGate() : Gate(2) { 

  /* Fill A = Re(V) and B = Im(V), V = A + iB */
  /* A =  1  0     B = 0
   *      0 -1
   */
  double val = 1./sqrt(2);
  MatSetValue(Va, 0, 0,  val, INSERT_VALUES);
  MatSetValue(Va, 0, 1,  val, INSERT_VALUES);
  MatSetValue(Va, 1, 0,  val, INSERT_VALUES);
  MatSetValue(Va, 1, 1, -val, INSERT_VALUES);
  MatAssemblyBegin(Va, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Va, MAT_FINAL_ASSEMBLY);

  /* Assemble target gate \bar V \kron V from  V = Va + i Vb*/
  assembleGate();
}
HadamardGate::~HadamardGate() {}



CNOT::CNOT() : Gate(4) {

  /* Fill Va = Re(V) = V, Vb = Im(V) = 0 */
  MatSetValue(Va, 0, 0, 1.0, INSERT_VALUES);
  MatSetValue(Va, 1, 1, 1.0, INSERT_VALUES);
  MatSetValue(Va, 2, 3, 1.0, INSERT_VALUES);
  MatSetValue(Va, 3, 2, 1.0, INSERT_VALUES);
  MatAssemblyBegin(Va, MAT_FINAL_ASSEMBLY);
  MatAssemblyEnd(Va, MAT_FINAL_ASSEMBLY);

  /* assemble V = \bar V \kron V */
  assembleGate();
}

CNOT::~CNOT(){}

