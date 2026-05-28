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

static void test_mul_basic(avr_t *c) {
  c->R[16] = 0x10;
  c->R[17] = 0x20;
  put_op(c, 0, 0x9C00 | ((17 & 0x10) << 5) | (16 << 4) | (17 & 0x0F));
  avr_step(c);
  CHECK_EQ_U((c->R[1] << 8) | c->R[0], 0x0200);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_Z, 0);
}

static void test_mul_zero(avr_t *c) {
  c->R[2] = 0;
  c->R[3] = 0x55;
  put_op(c, 0, 0x9C00 | (2 << 4) | 3);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0);
  CHECK_EQ_U(c->R[1], 0);
  CHECK_FLAG(c, F_Z, 1);
}

static void test_mul_carry(avr_t *c) {
  c->R[5] = 0xFF;
  c->R[6] = 0xFF;
  put_op(c, 0, 0x9C00 | (5 << 4) | 6);
  avr_step(c);
  CHECK_EQ_U((c->R[1] << 8) | c->R[0], 0xFE01);
  CHECK_FLAG(c, F_C, 1);
}

static void test_muls_pos(avr_t *c) {
  c->R[16] = 5;
  c->R[17] = 10;
  put_op(c, 0, 0x0200 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U((c->R[1] << 8) | c->R[0], 50);
}

static void test_muls_neg(avr_t *c) {
  c->R[16] = (uint8_t)-5;
  c->R[17] = 10;
  put_op(c, 0, 0x0200 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U((int16_t)((c->R[1] << 8) | c->R[0]), -50);
  CHECK_FLAG(c, F_C, 1);
}

static void test_mulsu(avr_t *c) {
  c->R[16] = (uint8_t)-2;
  c->R[17] = 100;
  put_op(c, 0, 0x0300 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U((int16_t)((c->R[1] << 8) | c->R[0]), -200);
}

static void test_fmul(avr_t *c) {
  c->R[16] = 0x80;
  c->R[17] = 0x80;
  put_op(c, 0, 0x0308 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U((c->R[1] << 8) | c->R[0], 0x8000);
  CHECK_FLAG(c, F_C, 0);
}

static void test_fmuls(avr_t *c) {
  c->R[16] = (uint8_t)-128;
  c->R[17] = (uint8_t)-128;
  put_op(c, 0, 0x0380 | (0 << 4) | 1);
  avr_step(c);
  uint16_t r = (uint16_t)((c->R[1] << 8) | c->R[0]);
  CHECK_EQ_U(r, 0x8000);
}

static void test_fmulsu(avr_t *c) {
  c->R[16] = (uint8_t)-128;
  c->R[17] = 128;
  put_op(c, 0, 0x0388 | (0 << 4) | 1);
  avr_step(c);
  uint16_t r = (uint16_t)((c->R[1] << 8) | c->R[0]);
  CHECK_EQ_U(r, 0x8000);
  CHECK_FLAG(c, F_C, 1);
}

int main(void) {
  avr_t cpu;
  if (test_init_cpu(&cpu) != 0)
    return 1;

  RUN_TEST(test_mul_basic, &cpu);
  RUN_TEST(test_mul_zero, &cpu);
  RUN_TEST(test_mul_carry, &cpu);
  RUN_TEST(test_muls_pos, &cpu);
  RUN_TEST(test_muls_neg, &cpu);
  RUN_TEST(test_mulsu, &cpu);
  RUN_TEST(test_fmul, &cpu);
  RUN_TEST(test_fmuls, &cpu);
  RUN_TEST(test_fmulsu, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("mul");
}
