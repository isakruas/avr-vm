/*
 * Copyright 2025-present Isak Ruas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test_common.h"

static void test_rjmp_forward(avr_t *c) {
  put_op(c, 0, 0xC000 | 2);
  avr_step(c);
  CHECK_EQ_U(c->pc, 6);
}

static void test_rjmp_backward(avr_t *c) {
  put_op(c, 4, 0xC000 | (0x0FFE));
  c->pc = 4;
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
}

static void test_jmp_22bit(avr_t *c) {
  uint32_t k = 0x10005;
  uint16_t hi = 0x940C | (uint16_t)((k & 0x1F0000) >> 13) |
                (uint16_t)((k & 0x010000) >> 16);
  uint16_t lo = (uint16_t)(k & 0xFFFF);
  put_op(c, 0, hi);
  put_op(c, 2, lo);
  avr_step(c);
  CHECK_EQ_U(c->pc, (uint32_t)(k << 1));
}

static void test_call_ret(avr_t *c) {
  put_op(c, 0, 0x940E);
  put_op(c, 2, 4);
  put_op(c, 8, 0x9508);
  c->sp = 0x40FF;
  avr_step(c);
  CHECK_EQ_U(c->pc, 8);
  CHECK_EQ_U(c->sp, 0x40FD);
  avr_step(c);
  CHECK_EQ_U(c->pc, 4);
  CHECK_EQ_U(c->sp, 0x40FF);
}

static void test_rcall_ret(avr_t *c) {
  put_op(c, 0, 0xD000 | 2);
  put_op(c, 6, 0x9508);
  c->sp = 0x40FF;
  avr_step(c);
  CHECK_EQ_U(c->pc, 6);
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
}

static void test_reti_sets_I(avr_t *c) {
  c->sp = 0x40FF;
  avr_write_data(c, c->sp--, 0);
  avr_write_data(c, c->sp--, 8);
  put_op(c, 0, 0x9518);
  avr_step(c);
  CHECK_EQ_U(c->pc, 8);
  CHECK_FLAG(c, F_I, 1);
}

static void test_ijmp(avr_t *c) {
  avr_set_z(c, 0x0010);
  put_op(c, 0, 0x9409);
  avr_step(c);
  CHECK_EQ_U(c->pc, 0x0020);
}

static void test_icall(avr_t *c) {
  avr_set_z(c, 0x0020);
  c->sp = 0x40FF;
  put_op(c, 0, 0x9509);
  avr_step(c);
  CHECK_EQ_U(c->pc, 0x0040);
  CHECK_EQ_U(c->sp, 0x40FD);
}

static void test_brbs_taken(avr_t *c) {
  c->sreg = (uint8_t)(1u << F_Z);
  put_op(c, 0, 0xF000 | (2 << 3) | F_Z);
  avr_step(c);
  CHECK_EQ_U(c->pc, 6);
}

static void test_brbs_not_taken(avr_t *c) {
  c->sreg = 0;
  put_op(c, 0, 0xF000 | (2 << 3) | F_Z);
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
}

static void test_brbc_taken(avr_t *c) {
  c->sreg = 0;
  put_op(c, 0, 0xF400 | (2 << 3) | F_C);
  avr_step(c);
  CHECK_EQ_U(c->pc, 6);
}

static void test_branch_negative_offset(avr_t *c) {
  c->sreg = (uint8_t)(1u << F_Z);
  c->pc = 8;
  put_op(c, 8, 0xF000 | ((-2 & 0x7F) << 3) | F_Z);
  avr_step(c);
  CHECK_EQ_U(c->pc, 6);
}

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_rjmp_forward, &cpu);
  RUN_TEST(test_rjmp_backward, &cpu);
  RUN_TEST(test_jmp_22bit, &cpu);
  RUN_TEST(test_call_ret, &cpu);
  RUN_TEST(test_rcall_ret, &cpu);
  RUN_TEST(test_reti_sets_I, &cpu);
  RUN_TEST(test_ijmp, &cpu);
  RUN_TEST(test_icall, &cpu);
  RUN_TEST(test_brbs_taken, &cpu);
  RUN_TEST(test_brbs_not_taken, &cpu);
  RUN_TEST(test_brbc_taken, &cpu);
  RUN_TEST(test_branch_negative_offset, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("branch");
}
