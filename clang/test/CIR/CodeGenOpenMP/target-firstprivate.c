// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -verify=nyi -DNYI %s -emit-cir -o /dev/null

void use(int);

// The implicit firstprivate of a variable used in the region: decomposition
// replaces the clause with one synthesized for the 'target' leaf.
void implicit_firstprivate() {
  // CHECK: omp.private {type = firstprivate} @[[RECIPE:[A-Za-z0-9_.]+]] : !s32i copy {
  // CHECK: ^bb0(%[[ORIG:.*]]: !cir.ptr<!s32i>{{.*}}, %[[COPY:.*]]: !cir.ptr<!s32i>{{.*}}):
  // CHECK-NEXT: %[[V:.*]] = cir.load %[[ORIG]]
  // CHECK-NEXT: cir.store %[[V]], %[[COPY]]
  // CHECK-NEXT: omp.yield(%[[COPY]] : !cir.ptr<!s32i>)
  // CHECK: cir.func{{.*}}@implicit_firstprivate
  int i = 1;
  // CHECK: %[[I:.*]] = cir.alloca "i"
  // CHECK: %[[MAP:.*]] = omp.map.info var_ptr(%[[I]] : !cir.ptr<!s32i>, !s32i) map_clauses(to) {{.*}} name("i")
  // CHECK: omp.target kernel_type(generic) map_entries(%[[MAP]] -> %{{.*}} : !cir.ptr<!s32i>) private(@[[RECIPE]] %[[I]] -> %[[PRIV:.*]] [map_idx=0] : !cir.ptr<!s32i>) {
  // The region only sees the private copy.
  // CHECK: cir.store{{.*}} %{{.*}}, %[[PRIV]]
#pragma omp target
  { i = 2; }
}

// Explicit and implicit clauses together with a map clause. The firstprivate
// variables follow the explicitly mapped one in the map operands.
void mixed(int *p, int a, int b) {
  // CHECK: cir.func{{.*}}@mixed
  // CHECK: omp.map.info {{.*}} map_clauses(tofrom) {{.*}} name("a")
  // CHECK: omp.map.info {{.*}} map_clauses(to) {{.*}} name("b")
  // CHECK: omp.map.info {{.*}} map_clauses(to) {{.*}} name("p")
  // CHECK: omp.target {{.*}}private(@{{.*}} %{{.*}} -> %{{.*}} [map_idx=1], @{{.*}} %{{.*}} -> %{{.*}} [map_idx=2] : !cir.ptr<!s32i>, !cir.ptr<!cir.ptr<!s32i>>)
#pragma omp target map(tofrom : a) firstprivate(b)
  {
    a = b;
    use(*p);
  }
}

#ifdef NYI
struct S { int x, y; };
void aggregate() {
  // nyi-error@+1 {{ClangIR code gen Not Yet Implemented: OpenMP target firstprivate of a variable that is not a local scalar}}
  struct S s = {1, 2};
#pragma omp target firstprivate(s)
  { use(s.x); }
}
#endif
