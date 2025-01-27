/*
 * Copyright (c) 2020, 2021, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2021, 2025, Loongson Technology. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "compiler/disassembler.hpp"
#include "precompiled.hpp"
#include "opto/c2_MacroAssembler.hpp"
#include "opto/c2_CodeStubs.hpp"
#include "runtime/objectMonitor.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"

#define __ Disassembler::hook<C2_MacroAssembler>(__FILE__, __LINE__, &masm)->

int C2SafepointPollStub::max_size() const {
  return 4 * 4;
}

void C2SafepointPollStub::emit(C2_MacroAssembler& masm) {
  assert(SharedRuntime::polling_page_return_handler_blob() != nullptr,
         "polling page return stub not created yet");
  address stub = SharedRuntime::polling_page_return_handler_blob()->entry_point();

  __ bind(entry());
  InternalAddress safepoint_pc(masm.pc() - masm.offset() + _safepoint_offset);
  __ lea(AT, safepoint_pc);
  __ st_d(AT, Address(TREG, JavaThread::saved_exception_pc_offset()));
  __ jmp(stub, relocInfo::runtime_call_type);
}

int C2EntryBarrierStub::max_size() const {
  return 5 * 4;
}

void C2EntryBarrierStub::emit(C2_MacroAssembler& masm) {
  __ bind(entry());
  __ call_long(StubRoutines::method_entry_barrier());
  __ b(continuation());

  __ bind(guard());
  __ relocate(entry_guard_Relocation::spec());
  __ emit_int32(0);  // nmethod guard value
}

#undef __
