// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// An array used in a target region, but not named by a clause, is implicitly
// mapped to and from the device.
void implicit_map_array() {
  // CHECK-LABEL: cir.func{{.*}}@implicit_map_array
  int a[10];
  // CHECK: %[[MAP:.*]] = omp.map.info {{.*}} map_clauses(implicit, tofrom) {{.*}} name("a")
  // CHECK: omp.target kernel_type(generic) map_entries(%[[MAP]] -> %[[ARG:.*]] : !cir.ptr<!cir.array<!s32i x 10>>) {
  // CHECK: cir.get_element %[[ARG]]
#pragma omp target
  { a[0] = 1; }
}

// A structure is another variable that can't be passed by copy.
struct S { int x, y; };
void implicit_map_struct() {
  // CHECK-LABEL: cir.func{{.*}}@implicit_map_struct
  struct S s;
  // CHECK: omp.map.info {{.*}} map_clauses(implicit, tofrom) {{.*}} name("s")
#pragma omp target
  { s.x = 1; }
}
