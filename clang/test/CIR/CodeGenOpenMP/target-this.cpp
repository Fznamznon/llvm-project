// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fopenmp-targets=amdgpu-amd-amdhsa -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

struct A {
  int x, y;
  void members();
  void explicit_map();
  void call();
  void g();
};

// Members used in the region are mapped through the object, which the region
// then reaches by its device address.
void A::members() {
  // CHECK-LABEL: cir.func{{.*}}@_ZN1A7membersEv
  // CHECK: %[[THIS:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
  // CHECK: %[[MAP:.*]] = omp.map.info var_ptr(%[[THIS]] : !cir.ptr<!rec_A>, !rec_A) map_clauses(implicit, tofrom) capture(ByRef) name("this")
  // The object is mapped only once.
  // CHECK-NOT: omp.map.info
  // CHECK: omp.target kernel_type(generic) map_entries(%[[MAP]] -> %[[ARG:.*]] : !cir.ptr<!rec_A>) {
  // CHECK: cir.get_member %[[ARG]][0] {name = "x"}
  // CHECK: cir.get_member %[[ARG]][1] {name = "y"}
#pragma omp target
  {
    x = 1;
    y = x;
  }
}

void A::explicit_map() {
  // CHECK-LABEL: cir.func{{.*}}@_ZN1A12explicit_mapEv
  // CHECK: omp.map.info {{.*}} map_clauses(to) {{.*}} name("this")
  // CHECK: omp.target
#pragma omp target map(to : x)
  { y = x; }
}

#ifdef NYI
// Without a member to map there is nothing that says what 'this' stands for.
void A::call() {
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP target capture of 'this' pointer}}
#pragma omp target
  { g(); }
}
#endif
