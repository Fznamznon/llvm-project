// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fopenmp-targets=amdgpu-amd-amdhsa -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// The region is offloaded only if the condition holds.
void target_if(int n, int x) {
  // CHECK-LABEL: cir.func{{.*}}@target_if
  // CHECK: %[[CMP:.*]] = cir.cmp
  // CHECK: %[[BOOL:.*]] = cir.cast bool_to_int %[[CMP]]
  // CHECK: %[[COND:.*]] = cir.builtin_int_cast %[[BOOL]] : {{.*}} -> i1
  // CHECK: omp.target kernel_type(generic) if(%[[COND]]) map_entries({{.*}}) {
  // LLVM: br i1 %{{.*}}, label %omp_if.then, label %omp_if.else
#pragma omp target if(n > 4) map(tofrom : x)
  { x = 1; }
}

// A modifier that names the directive is fine.
void target_if_modifier(int n, int x) {
  // CHECK-LABEL: cir.func{{.*}}@target_if_modifier
  // CHECK: omp.target kernel_type(generic) if({{.*}}) map_entries
#pragma omp target if(target : n) map(tofrom : x)
  { x = 2; }
}
