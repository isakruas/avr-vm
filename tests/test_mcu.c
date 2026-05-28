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

static void test_nop(avr_t *c) {
  put_op(c, 0, 0x0000);
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
  CHECK_EQ_U(c->cycles, 1);
}

static void test_sleep(avr_t *c) {
  put_op(c, 0, 0x9588);
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
}

static void test_wdr(avr_t *c) {
  put_op(c, 0, 0x95A8);
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
}

static void test_break(avr_t *c) {
  put_op(c, 0, 0x9598);
  avr_step(c);
  CHECK_EQ_U(c->pc, 2);
  CHECK(c->running);
}

static void test_unknown_halts(avr_t *c) {
  put_op(c, 0, 0xFFFF);
  step_silent(c); /* 0xFFFF is an invalid opcode on purpose */
  CHECK(c->unknown_opcode);
  CHECK(!c->running);
}

int main(void) {
  avr_t cpu;
  avr_init(&cpu);

  RUN_TEST(test_nop, &cpu);
  RUN_TEST(test_sleep, &cpu);
  RUN_TEST(test_wdr, &cpu);
  RUN_TEST(test_break, &cpu);
  RUN_TEST(test_unknown_halts, &cpu);

  avr_free(&cpu);
  TEST_SUMMARY("mcu");
}
