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

static void test_add_basic(avr_t *c) {
  c->R[16] = 0x12;
  c->R[17] = 0x34;
  put_op(c, 0, 0x0F01 | (16 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x46);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_Z, 0);
  CHECK_FLAG(c, F_N, 0);
  CHECK_FLAG(c, F_V, 0);
  CHECK_FLAG(c, F_H, 0);
}

static void test_add_carry_out(avr_t *c) {
  c->R[0] = 0xFF;
  c->R[1] = 0x01;
  put_op(c, 0, 0x0C00 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x00);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_H, 1);
  CHECK_FLAG(c, F_V, 0);
}

static void test_add_signed_overflow(avr_t *c) {
  c->R[0] = 0x7F;
  c->R[1] = 0x01;
  put_op(c, 0, 0x0C00 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x80);
  CHECK_FLAG(c, F_V, 1);
  CHECK_FLAG(c, F_N, 1);
  CHECK_FLAG(c, F_S, 0);
  CHECK_FLAG(c, F_H, 1);
}

static void test_adc_with_carry(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x20;
  c->sreg = (uint8_t)(1u << F_C);
  put_op(c, 0, 0x1C00 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x31);
  CHECK_FLAG(c, F_C, 0);
}

static void test_sub_basic(avr_t *c) {
  c->R[0] = 0x50;
  c->R[1] = 0x20;
  put_op(c, 0, 0x1800 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x30);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_Z, 0);
}

static void test_sub_borrow(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x20;
  put_op(c, 0, 0x1800 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0xF0);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_N, 1);
}

static void test_sbc_z_preserves_when_zero(avr_t *c) {
  c->R[0] = 0x05;
  c->R[1] = 0x04;
  c->sreg = (uint8_t)((1u << F_C) | (1u << F_Z));
  put_op(c, 0, 0x0800 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x00);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_C, 0);
}

static void test_sbc_z_cleared_when_nonzero(avr_t *c) {
  c->R[0] = 0x05;
  c->R[1] = 0x03;
  c->sreg = (uint8_t)((1u << F_C) | (1u << F_Z));
  put_op(c, 0, 0x0800 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x01);
  CHECK_FLAG(c, F_Z, 0);
}

static void test_subi(avr_t *c) {
  c->R[16] = 0x50;
  put_op(c, 0, 0x5000 | ((0x20 & 0xF0) << 4) | (0 << 4) | (0x20 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x30);
}

static void test_sbci(avr_t *c) {
  c->R[17] = 0x10;
  c->sreg = (uint8_t)(1u << F_C);
  put_op(c, 0, 0x4000 | ((0x05 & 0xF0) << 4) | (1 << 4) | (0x05 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[17], 0x0A);
}

static void test_inc_wrap(avr_t *c) {
  c->R[5] = 0xFF;
  put_op(c, 0, 0x9403 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x00);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_V, 0);
}

static void test_inc_overflow(avr_t *c) {
  c->R[5] = 0x7F;
  put_op(c, 0, 0x9403 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x80);
  CHECK_FLAG(c, F_V, 1);
  CHECK_FLAG(c, F_N, 1);
}

static void test_dec_wrap(avr_t *c) {
  c->R[5] = 0x00;
  put_op(c, 0, 0x940A | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xFF);
  CHECK_FLAG(c, F_Z, 0);
  CHECK_FLAG(c, F_N, 1);
}

static void test_dec_overflow(avr_t *c) {
  c->R[5] = 0x80;
  put_op(c, 0, 0x940A | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x7F);
  CHECK_FLAG(c, F_V, 1);
  CHECK_FLAG(c, F_N, 0);
}

static void test_neg_basic(avr_t *c) {
  c->R[3] = 0x05;
  put_op(c, 0, 0x9401 | (3 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[3], 0xFB);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_Z, 0);
  CHECK_FLAG(c, F_N, 1);
}

static void test_neg_zero(avr_t *c) {
  c->R[3] = 0x00;
  put_op(c, 0, 0x9401 | (3 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[3], 0x00);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_V, 0);
}

static void test_neg_80(avr_t *c) {
  c->R[3] = 0x80;
  put_op(c, 0, 0x9401 | (3 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[3], 0x80);
  CHECK_FLAG(c, F_V, 1);
  CHECK_FLAG(c, F_C, 1);
}

static void test_com(avr_t *c) {
  c->R[4] = 0xA5;
  put_op(c, 0, 0x9400 | (4 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[4], 0x5A);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_V, 0);
  CHECK_FLAG(c, F_Z, 0);
}

static void test_adiw_basic(avr_t *c) {
  c->R[24] = 0xFF;
  c->R[25] = 0x00;
  put_op(c, 0, 0x9600 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[24], 0x00);
  CHECK_EQ_U(c->R[25], 0x01);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_Z, 0);
}

static void test_adiw_carry(avr_t *c) {
  c->R[30] = 0xFF;
  c->R[31] = 0xFF;
  put_op(c, 0, 0x9600 | (3 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[30], 0x00);
  CHECK_EQ_U(c->R[31], 0x00);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_Z, 1);
}

static void test_sbiw_basic(avr_t *c) {
  c->R[26] = 0x00;
  c->R[27] = 0x01;
  put_op(c, 0, 0x9700 | (1 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[26], 0xFF);
  CHECK_EQ_U(c->R[27], 0x00);
  CHECK_FLAG(c, F_C, 0);
}

static void test_sbiw_underflow(avr_t *c) {
  c->R[28] = 0x00;
  c->R[29] = 0x00;
  put_op(c, 0, 0x9700 | (2 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[28], 0xFF);
  CHECK_EQ_U(c->R[29], 0xFF);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_N, 1);
}

int main(void) {
  avr_t cpu;
  if (test_init_cpu(&cpu) != 0)
    return 1;

  RUN_TEST(test_add_basic, &cpu);
  RUN_TEST(test_add_carry_out, &cpu);
  RUN_TEST(test_add_signed_overflow, &cpu);
  RUN_TEST(test_adc_with_carry, &cpu);
  RUN_TEST(test_sub_basic, &cpu);
  RUN_TEST(test_sub_borrow, &cpu);
  RUN_TEST(test_sbc_z_preserves_when_zero, &cpu);
  RUN_TEST(test_sbc_z_cleared_when_nonzero, &cpu);
  RUN_TEST(test_subi, &cpu);
  RUN_TEST(test_sbci, &cpu);
  RUN_TEST(test_inc_wrap, &cpu);
  RUN_TEST(test_inc_overflow, &cpu);
  RUN_TEST(test_dec_wrap, &cpu);
  RUN_TEST(test_dec_overflow, &cpu);
  RUN_TEST(test_neg_basic, &cpu);
  RUN_TEST(test_neg_zero, &cpu);
  RUN_TEST(test_neg_80, &cpu);
  RUN_TEST(test_com, &cpu);
  RUN_TEST(test_adiw_basic, &cpu);
  RUN_TEST(test_adiw_carry, &cpu);
  RUN_TEST(test_sbiw_basic, &cpu);
  RUN_TEST(test_sbiw_underflow, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("arithmetic");
}
