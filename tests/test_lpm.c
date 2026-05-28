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

static void test_lpm_implicit(avr_t *c) {
  c->flash[0x100] = 0xAB;
  avr_set_z(c, 0x100);
  put_op(c, 0, 0x95C8);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0xAB);
}

static void test_lpm_rd_z(avr_t *c) {
  c->flash[0x100] = 0xCD;
  avr_set_z(c, 0x100);
  put_op(c, 0, 0x9004 | (5 << 4));
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0xCD);
  CHECK_EQ_U(avr_get_z(c), 0x100);
}

static void test_lpm_rd_z_postinc(avr_t *c) {
  c->flash[0x100] = 0x11;
  c->flash[0x101] = 0x22;
  avr_set_z(c, 0x100);
  put_op(c, 0, 0x9005 | (5 << 4));
  put_op(c, 2, 0x9005 | (6 << 4));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x11);
  CHECK_EQ_U(c->R[6], 0x22);
  CHECK_EQ_U(avr_get_z(c), 0x102);
}

static void test_elpm_implicit(avr_t *c) {
  c->flash[0x10000] = 0x99;
  c->rampz = 1;
  avr_set_z(c, 0x0000);
  put_op(c, 0, 0x95D8);
  avr_step(c);
  CHECK_EQ_U(c->R[0], 0x99);
}

static void test_elpm_rd_z_postinc(avr_t *c) {
  c->flash[0x10000] = 0x55;
  c->flash[0x10001] = 0x66;
  c->rampz = 1;
  avr_set_z(c, 0x0000);
  put_op(c, 0, 0x9007 | (5 << 4));
  put_op(c, 2, 0x9007 | (6 << 4));
  avr_step(c);
  avr_step(c);
  CHECK_EQ_U(c->R[5], 0x55);
  CHECK_EQ_U(c->R[6], 0x66);
}

static void test_spm(avr_t *c) {
  c->rampz = 0;
  avr_set_z(c, 0x4000);
  c->R[0] = 0xDE;
  c->R[1] = 0xAD;
  put_op(c, 0, 0x95E8);
  avr_step(c);
  CHECK_EQ_U(c->flash[0x4000], 0xDE);
  CHECK_EQ_U(c->flash[0x4001], 0xAD);
}

int main(void) {
  avr_t cpu;
  if (test_init_cpu(&cpu) != 0)
    return 1;

  RUN_TEST(test_lpm_implicit, &cpu);
  RUN_TEST(test_lpm_rd_z, &cpu);
  RUN_TEST(test_lpm_rd_z_postinc, &cpu);
  RUN_TEST(test_elpm_implicit, &cpu);
  RUN_TEST(test_elpm_rd_z_postinc, &cpu);
  RUN_TEST(test_spm, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("lpm");
}
