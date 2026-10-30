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

void simd_reduction(int *a, float *b, int n) {
  int sum = 0, mx = 0;
  float prod = 1.0f;
  // CHECK-LABEL: cir.func{{.*}}@_Z14simd_reductionPiPfi
  // CHECK: %[[SUM:.*]] = cir.alloca "sum"
  // CHECK: %[[MX:.*]] = cir.alloca "mx"
  // CHECK: %[[PROD:.*]] = cir.alloca "prod"
#pragma omp simd reduction(+ : sum) reduction(max : mx) reduction(* : prod)
  for (int i = 0; i < n; ++i) {
    sum += a[i];
    if (a[i] > mx)
      mx = a[i];
    prod *= b[i];
  }
  // Each reduction variable has a private copy starting at the identity of its
  // operator, which the loop works on.
  // CHECK: cir.if
  // CHECK: %[[PSUM:.*]] = cir.alloca "sum"
  // CHECK: %[[PMX:.*]] = cir.alloca "mx"
  // CHECK: %[[PPROD:.*]] = cir.alloca "prod"
  // CHECK: cir.const #cir.int<0>
  // CHECK: cir.store{{.*}} %{{.*}}, %[[PSUM]]
  // CHECK: cir.const #cir.int<-2147483648>
  // CHECK: cir.store{{.*}} %{{.*}}, %[[PMX]]
  // CHECK: cir.const #cir.int<1>
  // CHECK: cir.store{{.*}} %{{.*}}, %[[PPROD]]
  // CHECK: cir.for
  // CHECK: cir.load{{.*}} %[[PSUM]]
  // CHECK: loop_hints
  // After the loop the copies are combined into the originals.
  // CHECK: cir.load{{.*}} %[[SUM]]
  // CHECK: cir.load{{.*}} %[[PSUM]]
  // CHECK: cir.add
  // CHECK: cir.store{{.*}} %{{.*}}, %[[SUM]]
  // CHECK: cir.ternary
  // CHECK: cir.store{{.*}} %{{.*}}, %[[MX]]
  // CHECK: cir.fmul
  // CHECK: cir.store{{.*}} %{{.*}}, %[[PROD]]
}

#ifdef NYI
void simd_safelen(int *a, int n) {
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP SIMD 'safelen' clause}}
#pragma omp simd safelen(4)
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
#endif
