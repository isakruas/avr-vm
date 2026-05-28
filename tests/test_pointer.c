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

static void test_ld_x(avr_t *c) {
  avr_set_x(c, 0x0200);
  avr_write_data(c, 0x0200, 0x77);
  put_op(c, 0, 0x900C | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x77);
  CHECK_EQ_U(avr_get_x(c), 0x0200);
}

static void test_ld_x_postinc(avr_t *c) {
  avr_set_x(c, 0x0200);
  avr_write_data(c, 0x0200, 0xAA);
  put_op(c, 0, 0x900D | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xAA);
  CHECK_EQ_U(avr_get_x(c), 0x0201);
}

static void test_ld_x_predec(avr_t *c) {
  avr_set_x(c, 0x0201);
  avr_write_data(c, 0x0200, 0xBB);
  put_op(c, 0, 0x900E | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xBB);
  CHECK_EQ_U(avr_get_x(c), 0x0200);
}

static void test_st_x(avr_t *c) {
  avr_set_x(c, 0x0300);
  c->R[10] = 0xDE;
  put_op(c, 0, 0x920C | (10 << 4));
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x0300), 0xDE);
}

static void test_st_x_postinc(avr_t *c) {
  avr_set_x(c, 0x0300);
  c->R[10] = 0x11;
  c->R[11] = 0x22;
  put_op(c, 0, 0x920D | (10 << 4));
  put_op(c, 2, 0x920D | (11 << 4));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x0300), 0x11);
  CHECK_EQ_U(avr_read_data(c, 0x0301), 0x22);
  CHECK_EQ_U(avr_get_x(c), 0x0302);
}

static void test_st_x_predec(avr_t *c) {
  avr_set_x(c, 0x0302);
  c->R[10] = 0x33;
  put_op(c, 0, 0x920E | (10 << 4));
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x0301), 0x33);
  CHECK_EQ_U(avr_get_x(c), 0x0301);
}

static void test_ld_y_postinc(avr_t *c) {
  avr_set_y(c, 0x0210);
  avr_write_data(c, 0x0210, 0x9A);
  put_op(c, 0, 0x9009 | (6 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[6], 0x9A);
  CHECK_EQ_U(avr_get_y(c), 0x0211);
}

static void test_ld_z_predec(avr_t *c) {
  avr_set_z(c, 0x0221);
  avr_write_data(c, 0x0220, 0xC3);
  put_op(c, 0, 0x9002 | (7 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[7], 0xC3);
  CHECK_EQ_U(avr_get_z(c), 0x0220);
}

static uint16_t encode_ldd_y(uint8_t d, uint8_t q) {
  return (uint16_t)(0x8008 | ((q & 0x20) << 8) | ((q & 0x18) << 7) |
                    (q & 0x07) | (d << 4));
}

static uint16_t encode_ldd_z(uint8_t d, uint8_t q) {
  return (uint16_t)(0x8000 | ((q & 0x20) << 8) | ((q & 0x18) << 7) |
                    (q & 0x07) | (d << 4));
}

static uint16_t encode_std_y(uint8_t r, uint8_t q) {
  return (uint16_t)(0x8208 | ((q & 0x20) << 8) | ((q & 0x18) << 7) |
                    (q & 0x07) | (r << 4));
}

static uint16_t encode_std_z(uint8_t r, uint8_t q) {
  return (uint16_t)(0x8200 | ((q & 0x20) << 8) | ((q & 0x18) << 7) |
                    (q & 0x07) | (r << 4));
}

static void test_ldd_y(avr_t *c) {
  avr_set_y(c, 0x0200);
  avr_write_data(c, 0x0205, 0xEE);
  put_op(c, 0, encode_ldd_y(8, 5));
  avr_step(c);
  CHECK_EQ_U(c->R[8], 0xEE);
}

static void test_ldd_z_max(avr_t *c) {
  avr_set_z(c, 0x0200);
  avr_write_data(c, 0x0200 + 63, 0x42);
  put_op(c, 0, encode_ldd_z(9, 63));
  avr_step(c);
  CHECK_EQ_U(c->R[9], 0x42);
}

static void test_std_y(avr_t *c) {
  avr_set_y(c, 0x0200);
  c->R[12] = 0xF1;
  put_op(c, 0, encode_std_y(12, 8));
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x0208), 0xF1);
}

static void test_std_z(avr_t *c) {
  avr_set_z(c, 0x0200);
  c->R[13] = 0xF2;
  put_op(c, 0, encode_std_z(13, 16));
  avr_step(c);
  CHECK_EQ_U(avr_read_data(c, 0x0210), 0xF2);
}

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_ld_x, &cpu);
  RUN_TEST(test_ld_x_postinc, &cpu);
  RUN_TEST(test_ld_x_predec, &cpu);
  RUN_TEST(test_st_x, &cpu);
  RUN_TEST(test_st_x_postinc, &cpu);
  RUN_TEST(test_st_x_predec, &cpu);
  RUN_TEST(test_ld_y_postinc, &cpu);
  RUN_TEST(test_ld_z_predec, &cpu);
  RUN_TEST(test_ldd_y, &cpu);
  RUN_TEST(test_ldd_z_max, &cpu);
  RUN_TEST(test_std_y, &cpu);
  RUN_TEST(test_std_z, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("pointer");
}
