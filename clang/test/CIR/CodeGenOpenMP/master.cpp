// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -emit-cir -fclangir %s -o - | FileCheck %s

void before(int);
void during(int);
void after(int);

void emit_master(int i) {
  // CHECK-LABEL: cir.func{{.*}}@_Z11emit_masteri
#pragma omp parallel
  {
    before(i);
#pragma omp master
    {
      during(i);
    }
    after(i);
  }
  // CHECK: omp.parallel {
  // CHECK: cir.call @_Z6beforei
  // CHECK: omp.master {
  // CHECK: cir.call @_Z6duringi
  // CHECK: omp.terminator
  // CHECK-NEXT: }
  // CHECK: cir.call @_Z5afteri
}

void master_single_stmt(int i) {
  // CHECK-LABEL: cir.func{{.*}}@_Z18master_single_stmti
#pragma omp master
  during(i);
  // CHECK: omp.master {
  // CHECK: cir.call @_Z6duringi
  // CHECK: omp.terminator
}
