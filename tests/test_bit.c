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

static void test_bst(avr_t *c) {
  c->R[5] = 0x40;
  put_op(c, 0, 0xFA00 | (5 << 4) | 6);
  avr_step(c);
  CHECK_FLAG(c, F_T, 1);
}

static void test_bst_zero(avr_t *c) {
  c->R[5] = 0x00;
  c->sreg = (uint8_t)(1u << F_T);
  put_op(c, 0, 0xFA00 | (5 << 4) | 3);
  avr_step(c);
  CHECK_FLAG(c, F_T, 0);
}

static void test_bld(avr_t *c) {
  c->R[5] = 0x00;
  c->sreg = (uint8_t)(1u << F_T);
  put_op(c, 0, 0xF800 | (5 << 4) | 2);
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x04);
}

static void test_bld_clear(avr_t *c) {
  c->R[5] = 0xFF;
  c->sreg = 0;
  put_op(c, 0, 0xF800 | (5 << 4) | 7);
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x7F);
}

static void test_sbi(avr_t *c) {
  avr_write_data(c, 0x25, 0x00);
  put_op(c, 0, 0x9A00 | (5 << 3) | 3);
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x25), 0x08);
}

static void test_cbi(avr_t *c) {
  avr_write_data(c, 0x25, 0xFF);
  put_op(c, 0, 0x9800 | (5 << 3) | 3);
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x25), 0xF7);
}

static void test_sbic_skip(avr_t *c) {
  avr_write_data(c, 0x25, 0x00);
  put_op(c, 0, 0x9900 | (5 << 3) | 3);
  put_op(c, 2, 0xE000 | ((0x11 & 0xF0) << 4) | (0 << 4) | (0x11 & 0x0F));
  put_op(c, 4, 0xE000 | ((0x22 & 0xF0) << 4) | (0 << 4) | (0x22 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->pc, 4);
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x22);
}

static void test_sbic_no_skip(avr_t *c) {
  avr_write_data(c, 0x25, 0xFF);
  put_op(c, 0, 0x9900 | (5 << 3) | 3);
  put_op(c, 2, 0xE000 | ((0x11 & 0xF0) << 4) | (0 << 4) | (0x11 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x11);
}

static void test_sbis_skip(avr_t *c) {
  avr_write_data(c, 0x25, 0x08);
  put_op(c, 0, 0x9B00 | (5 << 3) | 3);
  put_op(c, 2, 0xE000 | ((0x11 & 0xF0) << 4) | (0 << 4) | (0x11 & 0x0F));
  put_op(c, 4, 0xE000 | ((0x22 & 0xF0) << 4) | (0 << 4) | (0x22 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->pc, 4);
}

static void test_sbrc_skip(avr_t *c) {
  c->R[5] = 0xFE;
  put_op(c, 0, 0xFC00 | (5 << 4) | 0);
  put_op(c, 2, 0xE000 | ((0x11 & 0xF0) << 4) | (0 << 4) | (0x11 & 0x0F));
  put_op(c, 4, 0xE000 | ((0x22 & 0xF0) << 4) | (0 << 4) | (0x22 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->pc, 4);
}

static void test_sbrs_skip(avr_t *c) {
  c->R[5] = 0x01;
  put_op(c, 0, 0xFE00 | (5 << 4) | 0);
  put_op(c, 2, 0xE000 | ((0x11 & 0xF0) << 4) | (0 << 4) | (0x11 & 0x0F));
  put_op(c, 4, 0xE000 | ((0x22 & 0xF0) << 4) | (0 << 4) | (0x22 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->pc, 4);
}

int main(void) {
  avr_t cpu;
  if (test_init_cpu(&cpu) != 0)
    return 1;

  RUN_TEST(test_bst, &cpu);
  RUN_TEST(test_bst_zero, &cpu);
  RUN_TEST(test_bld, &cpu);
  RUN_TEST(test_bld_clear, &cpu);
  RUN_TEST(test_sbi, &cpu);
  RUN_TEST(test_cbi, &cpu);
  RUN_TEST(test_sbic_skip, &cpu);
  RUN_TEST(test_sbic_no_skip, &cpu);
  RUN_TEST(test_sbis_skip, &cpu);
  RUN_TEST(test_sbrc_skip, &cpu);
  RUN_TEST(test_sbrs_skip, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("bit");
}
