// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fopenmp-targets=amdgpu-amd-amdhsa -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

// The teams have to be what is directly inside of the target region. What the
// loop needs is evaluated in the region, from the variables it has been given.
void target_teams_distribute_parallel_for(int *a, int n) {
  // CHECK-LABEL: cir.func{{.*}}@target_teams_distribute_parallel_for
  // CHECK: omp.map.info {{.*}} bounds({{.*}}) name("a")
  // CHECK: omp.map.info {{.*}} map_clauses(to) {{.*}} name("n")
  // CHECK: omp.target kernel_type(generic) map_entries({{.*}}) private(@{{.*}} %{{.*}} -> %[[N:.*]] [map_idx=1] : {{.*}}) {
  // CHECK: omp.teams {
  // CHECK: cir.scope {
  // CHECK: cir.load{{.*}} %[[N]]
  // CHECK: cir.if
  // CHECK: omp.parallel {
  // CHECK: omp.distribute {
  // CHECK: omp.wsloop {
  // CHECK: omp.loop_nest
  // CHECK: } {omp.composite}
  // CHECK: } {omp.composite}
  // CHECK: } {omp.composite}
  // CHECK: omp.terminator
  // CHECK: omp.terminator
  // CHECK: } {omp.combined}
  // LLVM: __tgt_target_kernel
#pragma omp target teams distribute parallel for map(tofrom : a[0 : n])
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

#ifdef NYI
void num_teams(int *a, int n) {
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP TEAMS 'num_teams' clause}}
#pragma omp target teams distribute parallel for num_teams(2) map(tofrom : a[0 : n])
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
#endif
