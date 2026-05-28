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

static void check_set_clear(avr_t *c, uint16_t set_op, uint16_t clr_op,
                            int bit) {
  c->sreg = 0;
  put_op(c, 0, set_op);
  put_op(c, 2, clr_op);
  avr_step(c);
  CHECK_FLAG(c, bit, 1);
  avr_step(c);
  CHECK_FLAG(c, bit, 0);
}

static void test_sec_clc(avr_t *c) { check_set_clear(c, 0x9408, 0x9488, F_C); }
static void test_sez_clz(avr_t *c) { check_set_clear(c, 0x9418, 0x9498, F_Z); }
static void test_sen_cln(avr_t *c) { check_set_clear(c, 0x9428, 0x94A8, F_N); }
static void test_sev_clv(avr_t *c) { check_set_clear(c, 0x9438, 0x94B8, F_V); }
static void test_ses_cls(avr_t *c) { check_set_clear(c, 0x9448, 0x94C8, F_S); }
static void test_seh_clh(avr_t *c) { check_set_clear(c, 0x9458, 0x94D8, F_H); }
static void test_set_clt(avr_t *c) { check_set_clear(c, 0x9468, 0x94E8, F_T); }
static void test_sei_cli(avr_t *c) { check_set_clear(c, 0x9478, 0x94F8, F_I); }

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_sec_clc, &cpu);
  RUN_TEST(test_sez_clz, &cpu);
  RUN_TEST(test_sen_cln, &cpu);
  RUN_TEST(test_sev_clv, &cpu);
  RUN_TEST(test_ses_cls, &cpu);
  RUN_TEST(test_seh_clh, &cpu);
  RUN_TEST(test_set_clt, &cpu);
  RUN_TEST(test_sei_cli, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("sreg");
}
