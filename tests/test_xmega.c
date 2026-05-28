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

static void test_xch(avr_t *c) {
  avr_set_z(c, 0x0200);
  avr_write_data(c, 0x0200, 0xAA);
  c->R[5] = 0x55;
  put_op(c, 0, 0x9204 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xAA);
  CHECK_EQ_U(avr_read_data(c, 0x0200), 0x55);
}

static void test_las(avr_t *c) {
  avr_set_z(c, 0x0200);
  avr_write_data(c, 0x0200, 0xF0);
  c->R[5] = 0x0F;
  put_op(c, 0, 0x9205 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xF0);
  CHECK_EQ_U(avr_read_data(c, 0x0200), 0xFF);
}

static void test_lac(avr_t *c) {
  avr_set_z(c, 0x0200);
  avr_write_data(c, 0x0200, 0xFF);
  c->R[5] = 0x0F;
  put_op(c, 0, 0x9206 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xFF);
  CHECK_EQ_U(avr_read_data(c, 0x0200), 0xF0);
}

static void test_lat(avr_t *c) {
  avr_set_z(c, 0x0200);
  avr_write_data(c, 0x0200, 0xAA);
  c->R[5] = 0xF0;
  put_op(c, 0, 0x9207 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xAA);
  CHECK_EQ_U(avr_read_data(c, 0x0200), 0x5A);
}

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_xch, &cpu);
  RUN_TEST(test_las, &cpu);
  RUN_TEST(test_lac, &cpu);
  RUN_TEST(test_lat, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("xmega");
}
