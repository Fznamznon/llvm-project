// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -emit-cir -fclangir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

// NOTE: written without a built compiler; the CHECK lines are a best guess at
// the output and need to be validated (and the RUN lines too).

void use(int);

void simd_loop(float *a, int n) {
  // CHECK-LABEL: cir.func{{.*}}@_Z9simd_loopPfi
#pragma omp simd
  for (int i = 0; i < n; ++i)
    a[i] += 1.0f;
  // The pre-condition guards the loop, which is vectorized and parallel.
  // CHECK: cir.if
  // CHECK: cir.for
  // CHECK: loop_hints < vectorize_enable parallel>
}

void simd_private(int *a, int n) {
  int t = 0;
  // CHECK-LABEL: cir.func{{.*}}@_Z12simd_privatePii
#pragma omp simd private(t)
  for (int i = 0; i < n; ++i) {
    t = i;
    a[i] = t;
  }
  // CHECK: cir.for
}

void simd_outer_counter(int n) {
  int i;
  // CHECK-LABEL: cir.func{{.*}}@_Z18simd_outer_counteri
#pragma omp simd
  for (i = 0; i < n; ++i)
    use(i);
  // The counter keeps its value after the loop.
  // CHECK: cir.for
  // CHECK: cir.store{{.*}}%[[I:.*]] : !s32i, !cir.ptr<!s32i>
}

#ifdef NYI
void simd_reduction(int *a, int n) {
  int s = 0;
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP SIMD 'reduction' clause}}
#pragma omp simd reduction(+ : s)
  for (int i = 0; i < n; ++i)
    s += a[i];
}
#endif
