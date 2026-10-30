// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

void before(int);
void during(int);
void after(int);

void emit_single(int i) {
  // CHECK-LABEL: cir.func{{.*}}@_Z11emit_singlei
#pragma omp parallel
  {
    before(i);
#pragma omp single
    {
      during(i);
    }
    after(i);
  }
  // CHECK: omp.parallel {
  // CHECK: cir.call @_Z6beforei
  // CHECK: omp.single {
  // CHECK: cir.call @_Z6duringi
  // CHECK: omp.terminator
  // CHECK-NEXT: }
  // CHECK: cir.call @_Z5afteri
}

void single_nowait(int i) {
  // CHECK-LABEL: cir.func{{.*}}@_Z13single_nowaiti
#pragma omp parallel
#pragma omp single nowait
  during(i);
  // CHECK: omp.single nowait {
  // CHECK: cir.call @_Z6duringi
  // CHECK: omp.terminator
}

#ifdef NYI
void single_private(int i) {
#pragma omp parallel
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP SINGLE 'private' clause}}
#pragma omp single private(i)
  during(i);
}
#endif
