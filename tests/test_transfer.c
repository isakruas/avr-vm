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

static void test_mov(avr_t *c) {
  c->R[0] = 0xAB;
  c->R[1] = 0x00;
  put_op(c, 0, 0x2C00 | ((0 & 0x10) << 5) | (1 << 4) | (0 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[1], 0xAB);
}

static void test_movw(avr_t *c) {
  c->R[0] = 0x11;
  c->R[1] = 0x22;
  c->R[2] = 0x00;
  c->R[3] = 0x00;
  put_op(c, 0, 0x0100 | (1 << 4) | 0);
  avr_step(c);
  CHECK_EQ_U(c->R[2], 0x11);
  CHECK_EQ_U(c->R[3], 0x22);
}

static void test_ldi(avr_t *c) {
  put_op(c, 0, 0xE000 | ((0x5A & 0xF0) << 4) | (0 << 4) | (0x5A & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[16], 0x5A);
}

static void test_ser(avr_t *c) {
  put_op(c, 0, 0xE000 | ((0xFF & 0xF0) << 4) | (3 << 4) | (0xFF & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[19], 0xFF);
}

static void test_lds_sts(avr_t *c) {
  avr_write_data(c, 0x200, 0x5A);
  put_op(c, 0, 0x9000 | (5 << 4));
  put_op(c, 2, 0x0200);
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x5A);
  CHECK_EQ_U(c->pc, 4);

  c->R[6] = 0xA5;
  put_op(c, 4, 0x9200 | (6 << 4));
  put_op(c, 6, 0x0300);
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x300), 0xA5);
  CHECK_EQ_U(c->pc, 8);
}

static void test_push_pop(avr_t *c) {
  c->sp = 0x40FF;
  c->R[5] = 0xDE;
  c->R[6] = 0xAD;
  put_op(c, 0, 0x920F | (5 << 4));
  put_op(c, 2, 0x920F | (6 << 4));
  put_op(c, 4, 0x900F | (7 << 4));
  put_op(c, 6, 0x900F | (8 << 4));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->sp, 0x40FD);
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->R[7], 0xAD);
  CHECK_EQ_U(c->R[8], 0xDE);
  CHECK_EQ_U(c->sp, 0x40FF);
}

static void test_in_out(avr_t *c) {
  avr_write_data(c, 0x25, 0x77);
  put_op(c, 0, 0xB000 | (((5 >> 4) & 0x3) << 9) | (10 << 4) | (5 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(c->R[10], 0x77);

  c->R[11] = 0x88;
  put_op(c, 2, 0xB800 | (((6 >> 4) & 0x3) << 9) | (11 << 4) | (6 & 0x0F));
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x26), 0x88);
}

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_mov, &cpu);
  RUN_TEST(test_movw, &cpu);
  RUN_TEST(test_ldi, &cpu);
  RUN_TEST(test_ser, &cpu);
  RUN_TEST(test_lds_sts, &cpu);
  RUN_TEST(test_push_pop, &cpu);
  RUN_TEST(test_in_out, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("transfer");
}
