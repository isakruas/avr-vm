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

static void test_lsr(avr_t *c) {
  c->R[5] = 0x81;
  put_op(c, 0, 0x9406 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x40);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_N, 0);
  CHECK_FLAG(c, F_Z, 0);
  CHECK_FLAG(c, F_V, 1);
}

static void test_lsr_to_zero(avr_t *c) {
  c->R[5] = 0x01;
  put_op(c, 0, 0x9406 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x00);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_Z, 1);
}

static void test_asr_negative(avr_t *c) {
  c->R[5] = 0x80;
  put_op(c, 0, 0x9405 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xC0);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_N, 1);
}

static void test_asr_lsb(avr_t *c) {
  c->R[5] = 0x05;
  put_op(c, 0, 0x9405 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x02);
  CHECK_FLAG(c, F_C, 1);
}

static void test_ror_with_carry(avr_t *c) {
  c->R[5] = 0x01;
  c->sreg = (uint8_t)(1u << F_C);
  put_op(c, 0, 0x9407 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x80);
  CHECK_FLAG(c, F_C, 1);
  CHECK_FLAG(c, F_N, 1);
}

static void test_ror_no_carry(avr_t *c) {
  c->R[5] = 0x02;
  c->sreg = 0;
  put_op(c, 0, 0x9407 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x01);
  CHECK_FLAG(c, F_C, 0);
}

static void test_swap(avr_t *c) {
  c->R[5] = 0x5A;
  put_op(c, 0, 0x9402 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xA5);
}

static void test_lsl_via_add(avr_t *c) {
  c->R[5] = 0x41;
  put_op(c, 0, 0x0C00 | ((5 & 0x10) << 5) | (5 << 4) | (5 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x82);
  CHECK_FLAG(c, F_C, 0);
  CHECK_FLAG(c, F_N, 1);
}

static void test_rol_via_adc(avr_t *c) {
  c->R[5] = 0x81;
  c->sreg = (uint8_t)(1u << F_C);
  put_op(c, 0, 0x1C00 | ((5 & 0x10) << 5) | (5 << 4) | (5 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x03);
  CHECK_FLAG(c, F_C, 1);
}

int main(void) {
  avr_t cpu;
  if (test_init_cpu(&cpu) != 0)
    return 1;

  RUN_TEST(test_lsr, &cpu);
  RUN_TEST(test_lsr_to_zero, &cpu);
  RUN_TEST(test_asr_negative, &cpu);
  RUN_TEST(test_asr_lsb, &cpu);
  RUN_TEST(test_ror_with_carry, &cpu);
  RUN_TEST(test_ror_no_carry, &cpu);
  RUN_TEST(test_swap, &cpu);
  RUN_TEST(test_lsl_via_add, &cpu);
  RUN_TEST(test_rol_via_adc, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("shift");
}
