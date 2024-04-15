// RUN: %clang_cc1 %s -triple x86_64 --embed-dir=%S/Inputs -emit-llvm -o - | FileCheck %s

// CHECK: @__const._Z3fooi.ca = private unnamed_addr constant [3 x i32] [i32 0, i32 106, i32 107], align 4
// CHECK: @__const._Z3fooi.sc = private unnamed_addr constant %struct.S1 { i32 106, i32 107, i32 0 }, align 4
// CHECK: @__const._Z3fooi.t = private unnamed_addr constant [3 x %struct.T] [%struct.T { i32 48, i32 49, %struct.S1 { i32 50, i32 51, i32 52 } }, %struct.T { i32 53, i32 54, %struct.S1 { i32 55, i32 56, i32 57 } }, %struct.T { i32 10, i32 0, %struct.S1 zeroinitializer }], align 16
void foo(int a) {
// CHECK: %a.addr = alloca i32, align 4
// CHECK: store i32 %a, ptr %a.addr, align 4
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %ca, ptr align 4 @__const._Z3fooi.ca, i64 12, i1 false)
int ca[] = {
0
#embed <jk.txt> prefix(,)
};

// CHECK: %arrayinit.begin = getelementptr inbounds [3 x i32], ptr %notca, i64 0, i64 0
// CHECK: %0 = load i32, ptr %a.addr, align 4
// CHECK: store i32 %0, ptr %arrayinit.begin, align 4
// CHECK: %arrayinit.element = getelementptr inbounds i32, ptr %arrayinit.begin, i64 1
// CHECK: store i8 106, ptr %arrayinit.element, align 4
// CHECK: %arrayinit.element1 = getelementptr inbounds i32, ptr %arrayinit.element, i64 1
// CHECK: store i8 107, ptr %arrayinit.element1, align 4
int notca[] = {
a
#embed <jk.txt> prefix(,)
};

struct S1 {
  int x, y, z;
};

// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %sc, ptr align 4 @__const._Z3fooi.sc, i64 12, i1 false)
S1 sc = {
#embed <jk.txt> suffix(,)
0
};

// CHECK: %x = getelementptr inbounds %struct.S1, ptr %s, i32 0, i32 0
// CHECK: store i32 106, ptr %x, align 4
// CHECK: %y = getelementptr inbounds %struct.S1, ptr %s, i32 0, i32 1
// CHECK: store i32 107, ptr %y, align 4
// CHECK: %z = getelementptr inbounds %struct.S1, ptr %s, i32 0, i32 2
// CHECK: %1 = load i32, ptr %a.addr, align 4
S1 s = {
#embed <jk.txt> suffix(,)
a
};

// CHECK: store i32 107, ptr %b, align 4
int b =
#embed<jk.txt>
;


struct T {
  int arr[2];
  struct S1 s;
};

// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 16 %t, ptr align 16 @__const._Z3fooi.t, i64 60, i1 false)
constexpr struct T t[] = {
#embed <numbers.txt>
};

struct T tnonc[] = {
#embed <numbers.txt> suffix(,)
  a, 300
};

}
