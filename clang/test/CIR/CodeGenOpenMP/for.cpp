// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

void use(int);

void for_loop(float *a, int n) {
  // CHECK-LABEL: cir.func{{.*}}@_Z8for_loopPfi
#pragma omp parallel
  {
#pragma omp for
    for (int i = 0; i < n; ++i)
      a[i] += 1.0f;
  }
  // CHECK: omp.parallel
  // CHECK: cir.if
  // CHECK: omp.wsloop {
  // CHECK: omp.loop_nest (%{{.*}}) : i32 = (%{{.*}}) to (%{{.*}}) inclusive step (%{{.*}}) {
  // CHECK: omp.yield
}

void for_nowait_private(int *a, int n) {
  int t = 0;
  // CHECK-LABEL: cir.func{{.*}}@_Z18for_nowait_privatePii
#pragma omp parallel
#pragma omp for nowait private(t)
  for (int i = 0; i < n; ++i) {
    t = i;
    a[i] = t;
  }
  // CHECK: omp.wsloop nowait {
  // CHECK: omp.loop_nest
}

void for_collapse(int n) {
  // CHECK-LABEL: cir.func{{.*}}@_Z12for_collapsei
#pragma omp parallel
#pragma omp for collapse(2)
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      use(i + j);
  // CHECK: omp.wsloop {
  // CHECK: omp.loop_nest
}

void for_schedule(int *a, int n, int chunk) {
  // CHECK-LABEL: cir.func{{.*}}@_Z12for_schedulePiii
#pragma omp parallel
  {
#pragma omp for schedule(static)
    for (int i = 0; i < n; ++i)
      a[i] = i;
    // CHECK: omp.wsloop schedule(static) {

#pragma omp for schedule(dynamic, 4)
    for (int i = 0; i < n; ++i)
      a[i] = i;
    // CHECK: omp.wsloop schedule(dynamic = %{{.*}} : i32) {

#pragma omp for schedule(nonmonotonic: guided, chunk)
    for (int i = 0; i < n; ++i)
      a[i] = i;
    // CHECK: omp.wsloop schedule(guided = %{{.*}} : i32, nonmonotonic) {

#pragma omp for schedule(runtime)
    for (int i = 0; i < n; ++i)
      a[i] = i;
    // CHECK: omp.wsloop schedule(runtime) {
  }
}

#ifdef NYI
void for_reduction(int *a, int n) {
  int s = 0;
#pragma omp parallel
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP FOR 'reduction' clause}}
#pragma omp for reduction(+ : s)
  for (int i = 0; i < n; ++i)
    s += a[i];
}
#endif
