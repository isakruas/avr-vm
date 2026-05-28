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

static void test_cp_equal(avr_t *c) {
  c->R[0] = 0x42;
  c->R[1] = 0x42;
  put_op(c, 0, 0x1400 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x42);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_C, 0);
}

static void test_cp_less(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x20;
  put_op(c, 0, 0x1400 | (0 << 4) | 1);
  avr_step(c);
  CHECK_FLAG(c, F_Z, 0);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_N, 1);
}

static void test_cpc_with_carry(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x0F;
  c->sreg = (uint8_t)((1u << F_C) | (1u << F_Z));
  put_op(c, 0, 0x0400 | (0 << 4) | 1);
  avr_step(c);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_C, 0);
}

static void test_cpc_z_preserved(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x10;
  c->sreg = (uint8_t)((1u << F_Z));
  put_op(c, 0, 0x0400 | (0 << 4) | 1);
  avr_step(c);
  CHECK_FLAG(c, F_Z, 1);
}

static void test_cpi(avr_t *c) {
  c->R[20] = 0x5A;
  put_op(c, 0, 0x3000 | ((0x5A & 0xF0) << 4) | (4 << 4) | (0x5A & 0x0F));
  avr_step(c);
  CHECK_FLAG(c, F_Z, 1);
}

static void test_cpse_skip_1word(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x10;
  put_op(c, 0, 0x1000 | (0 << 4) | 1);
  put_op(c, 2, 0xE000 | ((0xFF & 0xF0) << 4) | (0 << 4) | (0xFF & 0x0F));
  put_op(c, 4, 0xE000 | ((0x55 & 0xF0) << 4) | (0 << 4) | (0x55 & 0x0F));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x55);
  CHECK_EQ_U(c->pc, 6);
}

static void test_cpse_no_skip(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x11;
  put_op(c, 0, 0x1000 | (0 << 4) | 1);
  put_op(c, 2, 0xE000 | ((0xAA & 0xF0) << 4) | (0 << 4) | (0xAA & 0x0F));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0xAA);
}

static void test_cpse_skip_2word(avr_t *c) {
  c->R[0] = 0x10;
  c->R[1] = 0x10;
  put_op(c, 0, 0x1000 | (0 << 4) | 1);
  put_op(c, 2, 0x9000 | (5 << 4));
  put_op(c, 4, 0x0200);
  put_op(c, 6, 0xE000 | ((0x77 & 0xF0) << 4) | (0 << 4) | (0x77 & 0x0F));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x77);
  CHECK_EQ_U(c->pc, 8);
}

int main(void) {
  avr_t cpu;
  if (test_init_cpu(&cpu) != 0)
    return 1;

  RUN_TEST(test_cp_equal, &cpu);
  RUN_TEST(test_cp_less, &cpu);
  RUN_TEST(test_cpc_with_carry, &cpu);
  RUN_TEST(test_cpc_z_preserved, &cpu);
  RUN_TEST(test_cpi, &cpu);
  RUN_TEST(test_cpse_skip_1word, &cpu);
  RUN_TEST(test_cpse_no_skip, &cpu);
  RUN_TEST(test_cpse_skip_2word, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("compare");
}
