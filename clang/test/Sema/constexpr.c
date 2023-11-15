// RUN: %clang_cc1 -std=c2x -verify -triple x86_64 -pedantic -Wno-conversion -Wno-constant-conversion %s

// Check that constexpr only applies to variables.
constexpr void f0() {}
constexpr const int f1() { return 0; }

constexpr struct S1 { int f; };
constexpr struct S2 ;
constexpr union U1;
constexpr union U2 {int a; float b;};
constexpr enum E1 {A = 1, B = 2} ;
struct S3 {
  static constexpr int f = 0;
  constexpr int f1 = 0;
};

constexpr;
constexpr int V1 = 3;
constexpr float V2 = 7.0;
int V3 = (constexpr)3;

// Check how constexpr works with other storage-class specifiers.
constexpr auto V4 = 1;
constexpr static auto V5 = 1;
constexpr static const auto V6 = 1;
constexpr static const int V7 = 1;
constexpr static int V8 = 1;

void f2(constexpr register int P1) {
  constexpr register int V9 = 0;
  constexpr register auto V10 = 0.0;
}

constexpr thread_local int V11 = 38;
constexpr static thread_local double V12 = 38;
constexpr extern thread_local char V13;
constexpr thread_local short V14 = 38;

// Check how constexpr works with qualifiers.
constexpr _Atomic int V15 = 0;
constexpr _Atomic(int) V16 = 0;
constexpr volatile int V17 = 0;
constexpr int * restrict V18 = 0;

typedef _Atomic(int) TheA;
typedef volatile short TheV;
typedef float * restrict TheR;

constexpr TheA V19[3] = {};
constexpr TheV V20[3] = {};
constexpr TheR V21[3] = {};

struct HasA {
  TheA f;
  int b;
};

struct HasV {
  float b;
  TheV f;
};

struct HasR {
  short b;
  int a;
  TheR f;
};

constexpr struct HasA V22[2] = {};
constexpr struct HasV V23[2] = {};
constexpr struct HasR V24[2] = {};

union U3 {
  float a;
  union {
    struct HasA f;
    struct HasR f1;
  };
};

constexpr union U3 V25 = {};
constexpr union U3 V26[8] = {};

struct S4 {
  union U3 f[3];
};

constexpr struct S4 V27 = {};

