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

static void test_and(avr_t *c) {
  c->R[0] = 0xF0;
  c->R[1] = 0x0F;
  put_op(c, 0, 0x2000 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x00);
  CHECK_FLAG(c, F_Z, 1);
  CHECK_FLAG(c, F_V, 0);
}

static void test_and_nonzero(avr_t *c) {
  c->R[0] = 0xFF;
  c->R[1] = 0x81;
  put_op(c, 0, 0x2000 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x81);
  CHECK_FLAG(c, F_N, 1);
  CHECK_FLAG(c, F_Z, 0);
}

static void test_or(avr_t *c) {
  c->R[0] = 0xF0;
  c->R[1] = 0x0F;
  put_op(c, 0, 0x2800 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0xFF);
  CHECK_FLAG(c, F_N, 1);
  CHECK_FLAG(c, F_V, 0);
}

static void test_eor(avr_t *c) {
  c->R[0] = 0xAA;
  c->R[1] = 0xFF;
  put_op(c, 0, 0x2400 | (0 << 4) | 1);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x55);
}

static void test_eor_clr(avr_t *c) {
  c->R[5] = 0xAA;
  put_op(c, 0, 0x2400 | (5 << 4) | ((5 & 0x10) << 5) | (5 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x00);
  CHECK_FLAG(c, F_Z, 1);
}

static void test_andi(avr_t *c) {
  c->R[16] = 0xFF;
  put_op(c, 0, 0x7000 | ((0x0F & 0xF0) << 4) | (0 << 4) | (0x0F & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x0F);
  CHECK_FLAG(c, F_N, 0);
}

static void test_ori(avr_t *c) {
  c->R[17] = 0x10;
  put_op(c, 0, 0x6000 | ((0x80 & 0xF0) << 4) | (1 << 4) | (0x80 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[17], 0x90);
  CHECK_FLAG(c, F_N, 1);
}

static void test_logic_clears_v(avr_t *c) {
  c->R[16] = 0x55;
  c->sreg = (uint8_t)(1u << F_V);
  put_op(c, 0, 0x7000 | ((0xFF & 0xF0) << 4) | (0 << 4) | (0xFF & 0x0F));
  avr_step(c);
  CHECK_FLAG(c, F_V, 0);
}

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_and, &cpu);
  RUN_TEST(test_and_nonzero, &cpu);
  RUN_TEST(test_or, &cpu);
  RUN_TEST(test_eor, &cpu);
  RUN_TEST(test_eor_clr, &cpu);
  RUN_TEST(test_andi, &cpu);
  RUN_TEST(test_ori, &cpu);
  RUN_TEST(test_logic_clears_v, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("logic");
}
