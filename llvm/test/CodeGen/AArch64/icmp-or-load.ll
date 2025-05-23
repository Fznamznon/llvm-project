; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc -mtriple=aarch64 %s -o - | FileCheck %s

define i1 @loadzext_i8i8(ptr %p) {
; CHECK-LABEL: loadzext_i8i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #1]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %az = zext i8 %a to i32
  %bz = zext i8 %b to i32
  %c = or i32 %az, %bz
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @loadzext_c_i8i8(ptr %p) {
; CHECK-LABEL: loadzext_c_i8i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #1]
; CHECK-NEXT:    orr w8, w9, w8
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %az = zext i8 %a to i32
  %bz = zext i8 %b to i32
  %c = or i32 %bz, %az
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @load_i8i8_shift(ptr %p) {
; CHECK-LABEL: load_i8i8_shift:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %az = zext i8 %a to i32
  %bz = zext i8 %b to i32
  %bs = shl i32 %bz, 8
  %c = or i32 %az, %bs
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @loadzext_i8i8i8(ptr %p) {
; CHECK-LABEL: loadzext_i8i8i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #1]
; CHECK-NEXT:    ldrb w10, [x0, #2]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    orr w8, w8, w10
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %r = getelementptr i8, ptr %p, i64 2
  %b2 = load i8, ptr %r
  %az = zext i8 %a to i32
  %bz = zext i8 %b to i32
  %b2z = zext i8 %b2 to i32
  %c = or i32 %az, %bz
  %c2 = or i32 %c, %b2z
  %d = icmp eq i32 %c2, 0
  ret i1 %d
}

define i1 @loadzext_i8i8i8i8(ptr %p) {
; CHECK-LABEL: loadzext_i8i8i8i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #1]
; CHECK-NEXT:    ldrb w10, [x0, #2]
; CHECK-NEXT:    ldrb w11, [x0, #3]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    orr w9, w10, w11
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %r = getelementptr i8, ptr %p, i64 2
  %b2 = load i8, ptr %r
  %s = getelementptr i8, ptr %p, i64 3
  %b3 = load i8, ptr %s
  %az = zext i8 %a to i32
  %bz = zext i8 %b to i32
  %b2z = zext i8 %b2 to i32
  %b3z = zext i8 %b3 to i32
  %c = or i32 %az, %bz
  %c2 = or i32 %c, %b2z
  %c3 = or i32 %c2, %b3z
  %d = icmp eq i32 %c3, 0
  ret i1 %d
}

define i1 @load_i8i8(ptr %p) {
; CHECK-LABEL: load_i8i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #1]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i8, ptr %q
  %c = or i8 %a, %b
  %d = icmp eq i8 %c, 0
  ret i1 %d
}

define i1 @load_i16i16(ptr %p) {
; CHECK-LABEL: load_i16i16:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    ldrh w9, [x0, #2]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i16, ptr %p
  %q = getelementptr i8, ptr %p, i64 2
  %b = load i16, ptr %q
  %c = or i16 %a, %b
  %d = icmp eq i16 %c, 0
  ret i1 %d
}

define i1 @load_i32i32(ptr %p) {
; CHECK-LABEL: load_i32i32:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp w8, w9, [x0]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i32, ptr %p
  %q = getelementptr i8, ptr %p, i64 4
  %b = load i32, ptr %q
  %c = or i32 %a, %b
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @load_i64i64(ptr %p) {
; CHECK-LABEL: load_i64i64:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldp x8, x9, [x0]
; CHECK-NEXT:    orr x8, x8, x9
; CHECK-NEXT:    cmp x8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i64, ptr %p
  %q = getelementptr i8, ptr %p, i64 8
  %b = load i64, ptr %q
  %c = or i64 %a, %b
  %d = icmp eq i64 %c, 0
  ret i1 %d
}

define i1 @load_i8i16i8(ptr %p) {
; CHECK-LABEL: load_i8i16i8:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #3]
; CHECK-NEXT:    ldurh w10, [x0, #1]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    orr w8, w8, w10
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %l0 = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %l1 = load i16, ptr %q
  %r = getelementptr i8, ptr %p, i64 3
  %l3 = load i8, ptr %r
  %c = or i8 %l0, %l3
  %d = zext i8 %c to i32
  %e = zext i16 %l1 to i32
  %f = or i32 %d, %e
  %g = icmp eq i32 %f, 0
  ret i1 %g
}

define i1 @loadzext_i8i8_wrongoff(ptr %p) {
; CHECK-LABEL: loadzext_i8i8_wrongoff:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrb w8, [x0]
; CHECK-NEXT:    ldrb w9, [x0, #2]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 2
  %b = load i8, ptr %q
  %az = zext i8 %a to i32
  %bz = zext i8 %b to i32
  %c = or i32 %az, %bz
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @loadzext_i16i16_wrongoff(ptr %p) {
; CHECK-LABEL: loadzext_i16i16_wrongoff:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    ldurh w9, [x0, #1]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i16, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %b = load i16, ptr %q
  %az = zext i16 %a to i32
  %bz = zext i16 %b to i32
  %c = or i32 %az, %bz
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @loadzext_i16i16_store(ptr %p, ptr %q) {
; CHECK-LABEL: loadzext_i16i16_store:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    strb wzr, [x1]
; CHECK-NEXT:    ldrh w9, [x0, #2]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i16, ptr %p
  store i8 0, ptr %q
  %p2 = getelementptr i8, ptr %p, i64 2
  %b = load i16, ptr %p2
  %az = zext i16 %a to i32
  %bz = zext i16 %b to i32
  %c = or i32 %az, %bz
  %d = icmp eq i32 %c, 0
  ret i1 %d
}

define i1 @loadzext_i16i16_bases(ptr %p, ptr %q) {
; CHECK-LABEL: loadzext_i16i16_bases:
; CHECK:       // %bb.0:
; CHECK-NEXT:    ldrh w8, [x0]
; CHECK-NEXT:    ldrh w9, [x1, #2]
; CHECK-NEXT:    orr w8, w8, w9
; CHECK-NEXT:    cmp w8, #0
; CHECK-NEXT:    cset w0, eq
; CHECK-NEXT:    ret
  %a = load i16, ptr %p
  %p2 = getelementptr i8, ptr %q, i64 2
  %b = load i16, ptr %p2
  %az = zext i16 %a to i32
  %bz = zext i16 %b to i32
  %c = or i32 %az, %bz
  %d = icmp eq i32 %c, 0
  ret i1 %d
}
