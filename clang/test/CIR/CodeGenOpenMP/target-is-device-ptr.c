// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fopenmp-targets=amdgpu-amd-amdhsa -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

// The pointer is already a device address: the region gets a variable that
// holds it, in front of its other arguments.
void is_device_ptr(int *p, int q) {
  // CHECK-LABEL: cir.func{{.*}}@is_device_ptr
  // CHECK: %[[P:.*]] = cir.alloca "p"
  // CHECK: %[[QMAP:.*]] = omp.map.info {{.*}} map_clauses(tofrom) {{.*}} name("q")
  // CHECK: %[[PMAP:.*]] = omp.map.info var_ptr(%[[P]] {{.*}} map_clauses(to, is_device_ptr) {{.*}} name("p")
  // CHECK: %[[PMAP2:.*]] = omp.map.info var_ptr(%[[P]] {{.*}} map_clauses(to, is_device_ptr) {{.*}} name("p")
  // CHECK: omp.target kernel_type(generic) is_device_ptr(%[[PMAP2]] : {{.*}}) has_device_addr(%[[PMAP]] -> %[[PARG:.*]] : {{.*}}) map_entries(%[[QMAP]] -> %[[QARG:.*]] : {{.*}}) {
  // CHECK: cir.load{{.*}} %[[PARG]]
  // CHECK: cir.store{{.*}} %[[QARG]]
#pragma omp target is_device_ptr(p) map(tofrom : q)
  {
    p[0] = 1;
    q = 2;
  }
}

#ifdef NYI
void array() {
  int arr[4];
  // nyi-error@+2 {{ClangIR code gen Not Yet Implemented: OpenMP is_device_ptr of something other than a local pointer variable}}
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP target implicit by-ref capture}}
#pragma omp target is_device_ptr(arr)
  { arr[0] = 1; }
}
#endif
