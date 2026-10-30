// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fopenmp-targets=amdgpu-amd-amdhsa -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

// A section of a local array: only it is mapped.
void array_section() {
  // CHECK-LABEL: cir.func{{.*}}@array_section
  int a[10];
  // CHECK: %[[A:.*]] = cir.alloca "a"
  // CHECK: %[[BOUNDS:.*]] = omp.map.bounds lower_bound({{.*}} : i64) upper_bound({{.*}} : i64) extent({{.*}} : i64) stride({{.*}} : i64) start_idx({{.*}} : i64)
  // CHECK: %[[MAP:.*]] = omp.map.info var_ptr(%[[A]] : !cir.ptr<!cir.array<!s32i x 10>>, !cir.array<!s32i x 10>) map_clauses(tofrom) capture(ByRef) bounds(%[[BOUNDS]]) name("a")
  // CHECK: omp.target kernel_type(generic) map_entries(%[[MAP]] -> %[[ARG:.*]] : !cir.ptr<!cir.array<!s32i x 10>>) {
  // CHECK: cir.get_element %[[ARG]]
  // LLVM: @.offload_sizes = {{.*}} [i64 16
#pragma omp target map(tofrom : a[2:4])
  { a[2] = 1; }
}

// A single element is a section of length one.
void element(int i) {
  // CHECK-LABEL: cir.func{{.*}}@element
  int a[10];
  // CHECK: omp.map.bounds
  // CHECK: omp.map.info {{.*}} map_clauses(to) {{.*}} bounds({{.*}}) name("a")
#pragma omp target map(to : a[i])
  { (void)a[i]; }
}

// A section of what a pointer points to: the region gets a variable of its own
// for the pointer, holding the device address.
void pointer_section(int *p, int n) {
  // CHECK-LABEL: cir.func{{.*}}@pointer_section
  // CHECK: %[[P:.*]] = cir.alloca "p"
  // CHECK: %[[PTR:.*]] = cir.load %[[P]]
  // CHECK: %[[MAP:.*]] = omp.map.info var_ptr(%[[PTR]] : !cir.ptr<!s32i>, !s32i) map_clauses(tofrom) capture(ByRef) bounds({{.*}}) name("p")
  // CHECK: omp.target kernel_type(generic) map_entries(%[[MAP]] -> %[[ARG:.*]] : !cir.ptr<!s32i>) {
  // CHECK: %[[VAR:.*]] = cir.alloca "p"
  // CHECK: cir.store %[[ARG]], %[[VAR]]
#pragma omp target map(tofrom : p[0:n])
  { p[0] = 1; }
}

// Without a lower bound or a length, from the start or up to the end.
void open_ended() {
  // CHECK-LABEL: cir.func{{.*}}@open_ended
  int a[10];
  // CHECK: omp.map.info {{.*}} bounds({{.*}}) name("a")
#pragma omp target map(tofrom : a[3:])
  { a[3] = 1; }
}

#ifdef NYI
struct S { int x, y; };
void member() {
  struct S s;
  // nyi-error@+2 {{ClangIR code gen Not Yet Implemented: OpenMP map clause with this kind of variable; only variables, and one-dimensional sections and elements of local arrays and pointers are supported}}
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP target implicit by-ref capture}}
#pragma omp target map(tofrom : s.x)
  { s.x = 1; }
}
#endif
