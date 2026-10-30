// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

void use(int);

// The loop is divided between the teams, and then between the threads of each.
void teams_distribute_parallel_for(int *a, int n) {
  // CHECK-LABEL: cir.func{{.*}}@teams_distribute_parallel_for
  // CHECK: omp.teams {
  // CHECK: cir.scope {
  // CHECK: cir.if
  // CHECK: omp.parallel {
  // The counter and the iteration variable are per thread.
  // CHECK: cir.alloca "i"
  // CHECK: cir.alloca ".omp.iv"
  // CHECK: omp.distribute {
  // CHECK: omp.wsloop {
  // CHECK: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {
  // CHECK: omp.yield
  // CHECK: } {omp.composite}
  // CHECK: } {omp.composite}
  // CHECK: omp.terminator
  // CHECK: } {omp.composite}
  // CHECK: omp.terminator
  // LLVM: __kmpc_fork_teams
  // LLVM: __kmpc_dist_for_static_init_4u
#pragma omp teams distribute parallel for
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

// Clauses go to the leaf they are for.
void clauses(int *a, int n, int m) {
  // CHECK-LABEL: cir.func{{.*}}@clauses
  // CHECK: omp.teams {
  // CHECK: omp.parallel num_threads({{.*}} : i32) {
  // CHECK: omp.wsloop schedule(dynamic = %{{.*}} : i32) {
#pragma omp teams distribute parallel for num_threads(4) schedule(dynamic, 2) collapse(2)
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j)
      a[i * m + j] = i + j;
}

#ifdef NYI
void num_teams(int *a, int n) {
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP TEAMS 'num_teams' clause}}
#pragma omp teams distribute parallel for num_teams(2)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}

void dist_schedule(int *a, int n) {
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP DISTRIBUTE 'dist_schedule' clause}}
#pragma omp teams distribute parallel for dist_schedule(static, 2)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
#endif
